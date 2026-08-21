# The openevv synthesiser driver.
#
# Turns a speech sequence into text with Eloquence annotations in it, and hands
# that to the engine layer beside this file.
#
# Prosody inside a sentence is said as an annotation rather than by setting a
# parameter, and that is the one design decision here worth explaining. A
# parameter is set on the instance and takes effect for everything queued
# behind it, so a pitch change meant for one word arrives too early. An
# annotation travels inside the text and takes effect where it sits. The
# annotations used are in the engine's own test cases and are known to match
# IBM's byte for byte, so this is the path with evidence behind it.

import os
from collections import OrderedDict

from autoSettingsUtils.driverSetting import BooleanDriverSetting, NumericDriverSetting
from logHandler import log
from speech.commands import (
	BreakCommand,
	CharacterModeCommand,
	IndexCommand,
	LangChangeCommand,
	PitchCommand,
	RateCommand,
	VolumeCommand,
)
from speech.types import SpeechSequence
from synthDriverHandler import (
	SynthDriver,
	VoiceInfo,
	synthDoneSpeaking,
	synthIndexReached,
)

from . import _openevv

#: What the engine's speed setting is worth at either end of the reader's
#: nought to a hundred. The engine will take nought to two hundred and fifty,
#: but the top of that range is far past intelligible and the bottom is a
#: crawl, so the useful stretch is mapped instead. These two numbers are the
#: ones the IBMTTS driver arrived at by ear on this same engine.
MIN_RATE = 40
MAX_RATE = 156

#: How much further the boost goes, for someone who reads faster than the
#: plain range allows.
RATE_BOOST = 1.6

#: A break is asked for in milliseconds and the engine's pause annotation is
#: not in milliseconds, so the number has to be scaled -- and by how much
#: depends on the speaking rate, since a pause is counted in something closer
#: to syllables. Measured at these five rates and interpolated between them.
BREAK_FACTORS = {10: 1, 43: 2, 60: 3, 75: 4, 85: 5}


class SynthDriver(SynthDriver):
	name = "openevv"
	description = "Eloquence (openevv)"

	supportedSettings = (
		SynthDriver.VoiceSetting(),
		SynthDriver.RateSetting(),
		SynthDriver.RateBoostSetting(),
		SynthDriver.PitchSetting(),
		SynthDriver.InflectionSetting(),
		SynthDriver.VolumeSetting(),
		# Translators: Label for a setting in voice settings dialog.
		NumericDriverSetting("headSize", _("Hea&d size"), False),
		# Translators: Label for a setting in voice settings dialog.
		NumericDriverSetting("roughness", _("Rou&ghness"), False),
		# Translators: Label for a setting in voice settings dialog.
		NumericDriverSetting("breathiness", _("Breathi&ness"), False),
		# Translators: Label for a setting in voice settings dialog.
		BooleanDriverSetting("abbreviations", _("Expand a&bbreviations"), False),
		# Translators: Label for a setting in voice settings dialog.
		BooleanDriverSetting("voiceTags", _("Allow backquote voice &tags"), False),
	)

	supportedCommands = {
		IndexCommand,
		CharacterModeCommand,
		LangChangeCommand,
		BreakCommand,
		PitchCommand,
		RateCommand,
		VolumeCommand,
	}
	supportedNotifications = {synthIndexReached, synthDoneSpeaking}

	@classmethod
	def check(cls):
		return os.path.isfile(_openevv.libraryPath())

	def __init__(self):
		self._rateBoost = False
		self._abbreviations = False
		self._voiceTags = False
		self._voice = str(_openevv.VOICE_FIRST)
		self._engine = _openevv.Engine(self._onIndexReached)
		self._engine.open()
		log.debug("openevv: engine version %s" % self._engine.version)

	def terminate(self):
		self._engine.close()

	# ---- speaking ----------------------------------------------------

	def speak(self, speechSequence: SpeechSequence):
		engine = self._engine
		batch = []
		text = []
		spelling = False
		#: Whether anything in this sequence is meant to make a sound. A
		#: sequence of nothing but commands is silent because it should be, and
		#: the engine layer is told so rather than complaining about it.
		words = False

		def flush():
			if text:
				joined = "".join(text).encode("utf-8", "replace")
				batch.append((engine.addText, (joined,)))
				del text[:]

		for item in speechSequence:
			if isinstance(item, str):
				said = self._processText(item)
				words = words or said.strip() != ""
				text.append(said)
			elif isinstance(item, IndexCommand):
				# An index has to sit between stretches of text rather than
				# inside one, so what has been gathered goes first.
				flush()
				batch.append((engine.index, (item.index,)))
			elif isinstance(item, CharacterModeCommand):
				# What the last such command asked for, so that spelling left
				# open at the end of the sequence is closed once and spelling
				# already closed is not closed again.
				spelling = item.state
				# On its own, not on the end of a stretch of text. An
				# annotation with nothing after it in the same call does not
				# take effect, which is how spelling used to leak out of one
				# utterance and into every one after it.
				flush()
				batch.append((engine.addText, (b"`ts1 " if item.state else b"`ts0 ",)))
			elif isinstance(item, BreakCommand):
				text.append(" `p%d " % self._breakToPause(item.time))
			elif isinstance(item, PitchCommand):
				text.append("`vb%d " % self._pitchToParam(item.newValue))
			elif isinstance(item, RateCommand):
				text.append("`vs%d " % self._rateToParam(item.newValue))
			elif isinstance(item, VolumeCommand):
				text.append("`vv%d " % self._volumeToParam(item.newValue))
			elif isinstance(item, LangChangeCommand):
				# US English is the only language in the library, so there is
				# nothing to switch to and nothing to say about it.
				pass
			else:
				log.error("openevv: unknown speech: %s" % item)

		flush()
		if spelling:
			batch.append((engine.addText, (b"`ts0 ",)))
		batch.append((engine.synthesize, (words,)))
		engine.post(batch)

	def _processText(self, text):
		if not self._voiceTags:
			# A backtick starts an annotation, so ordinary text carrying one
			# would be read as a command rather than spoken. Unless the reader
			# has asked for tags to go through, it becomes a space.
			text = text.replace("`", " ")
		return text

	def cancel(self):
		self._engine.cancel()

	def pause(self, switch):
		self._engine.pause(switch)

	def _onIndexReached(self, index):
		if index is None:
			synthDoneSpeaking.notify(synth=self)
		else:
			synthIndexReached.notify(synth=self, index=index)

	# ---- turning the reader's numbers into the engine's --------------

	def _rateToParam(self, percent):
		value = self._percentToParam(percent, MIN_RATE, MAX_RATE)
		if self._rateBoost:
			value = int(round(value * RATE_BOOST))
		return min(value, _openevv.VOICE_RANGE[_openevv.VOICE_SPEED][1])

	def _pitchToParam(self, percent):
		return int(percent)

	def _volumeToParam(self, percent):
		return int(percent)

	def _breakToPause(self, milliseconds):
		rates = sorted(BREAK_FACTORS)
		rate = self.rate
		if rate <= rates[0]:
			factor = BREAK_FACTORS[rates[0]]
		elif rate >= rates[-1]:
			factor = BREAK_FACTORS[rates[-1]]
		elif rate in BREAK_FACTORS:
			factor = BREAK_FACTORS[rate]
		else:
			below = [i for i, r in enumerate(rates) if r < rate][-1]
			lo, hi = rates[below], rates[below + 1]
			factor = BREAK_FACTORS[lo] + (BREAK_FACTORS[hi] - BREAK_FACTORS[lo]) * (
				rate - lo
			) / (hi - lo)
		return max(0, int(factor * milliseconds))

	# ---- the settings ------------------------------------------------

	def _get_rate(self):
		value = self._engine.voiceParams.get(_openevv.VOICE_SPEED, 0)
		if self._rateBoost:
			value = int(round(value / RATE_BOOST))
		return self._paramToPercent(value, MIN_RATE, MAX_RATE)

	def _set_rate(self, percent):
		self._post(_openevv.VOICE_SPEED, self._rateToParam(percent))

	def _get_rateBoost(self):
		return self._rateBoost

	def _set_rateBoost(self, enable):
		if enable != self._rateBoost:
			rate = self.rate
			self._rateBoost = enable
			self.rate = rate

	def _get_pitch(self):
		return self._engine.voiceParams.get(_openevv.VOICE_PITCH, 0)

	def _set_pitch(self, value):
		self._post(_openevv.VOICE_PITCH, value)

	def _get_volume(self):
		return self._engine.voiceParams.get(_openevv.VOICE_VOLUME, 0)

	def _set_volume(self, value):
		self._post(_openevv.VOICE_VOLUME, value)

	def _get_inflection(self):
		return self._engine.voiceParams.get(_openevv.VOICE_FLUCTUATION, 0)

	def _set_inflection(self, value):
		self._post(_openevv.VOICE_FLUCTUATION, value)

	def _get_headSize(self):
		return self._engine.voiceParams.get(_openevv.VOICE_HEAD_SIZE, 0)

	def _set_headSize(self, value):
		self._post(_openevv.VOICE_HEAD_SIZE, value)

	def _get_roughness(self):
		return self._engine.voiceParams.get(_openevv.VOICE_ROUGHNESS, 0)

	def _set_roughness(self, value):
		self._post(_openevv.VOICE_ROUGHNESS, value)

	def _get_breathiness(self):
		return self._engine.voiceParams.get(_openevv.VOICE_BREATHINESS, 0)

	def _set_breathiness(self, value):
		self._post(_openevv.VOICE_BREATHINESS, value)

	def _get_abbreviations(self):
		return self._abbreviations

	def _set_abbreviations(self, enable):
		self._abbreviations = enable
		# Nought turns the abbreviation dictionary on, which is the engine's
		# own sense of the setting and not a mistake here.
		self._engine.post(
			[(self._engine.setParam, (_openevv.PARAM_DICTIONARY, 0 if enable else 1))],
		)

	def _get_voiceTags(self):
		return self._voiceTags

	def _set_voiceTags(self, enable):
		self._voiceTags = enable

	def _get_availableVoices(self):
		voices = OrderedDict()
		for number, name in self._engine.voiceNames.items():
			voices[str(number)] = VoiceInfo(str(number), name, "en_US")
		return voices

	def _get_voice(self):
		return self._voice

	def _set_voice(self, value):
		if value not in self._engine.voiceNames and value not in (
			str(n) for n in self._engine.voiceNames
		):
			return
		self._voice = str(value)
		# Copying a preset over the voice in force replaces every one of its
		# eight settings, so whatever the reader had chosen is gone; NVDA sets
		# rate, pitch and the rest again after a voice change, which is what
		# puts them back.
		self._engine.post([(self._engine.copyVoice, (int(value),))])

	def _get_language(self):
		return "en_US"

	def _post(self, which, value):
		self._engine.post([(self._engine.setVoiceParam, (which, value))])

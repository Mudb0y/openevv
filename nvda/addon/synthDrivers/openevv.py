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
import re
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

#: How far an unpunctuated stretch may run before it is ended at whitespace.
#:
#: The engine cannot be interrupted -- both of the ways the interface offers
#: fault it, which the engine layer beside this file explains -- so asking for
#: silence means waiting for the utterance in flight to finish synthesising
#: into nothing. That is cheap for a line of a list and is not cheap for a
#: chat message of several thousand characters: measured on this engine, one
#: such message costs 0.83 s, and 560 characters of Arabic, which is spelled
#: out character by character, cost 1.44 s. A reader arrowing down a list
#: every 200 ms has every item that lands inside that wait dropped, which is
#: speech going silent for several objects and then catching up.
#:
#: Sentence ends are the usual boundary because the engine already pauses
#: there: one measured message was 252,010 samples whole and the same 252,010
#: samples in six pieces. This larger fallback is for a stretch with no
#: sentence end, such as the 560-character Arabic case above.
PIECE_CAP = 500

#: Where a piece may end: after a run of whitespace, so nothing is split
#: inside a word and no annotation is separated from what it applies to.
_BOUNDARY = re.compile(r"(\s+)")

#: Closing marks which may follow sentence-final punctuation.
_CLOSERS = "\"')]}\N{RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK}\N{RIGHT DOUBLE QUOTATION MARK}\N{RIGHT SINGLE QUOTATION MARK}"


def _endsSentence(text):
	"""Whether a word ends a sentence rather than a dotted number.

	A dot is the doubtful one and the doubt is not symmetrical. A boundary the
	engine would not have made costs an audible pause -- it ends a clause there,
	and that is 0.40 s of gap -- while a boundary declined costs only a longer
	wait on the next cancel. So a dot has to argue for itself.

	An abbreviation and an initial are what it fails on. "Mr. Jones" split after
	the dot measured 0.70 s longer than the same sentence whole, and "J. R. R.
	Tolkien" split at every initial measured 1.48 s longer than 3.72, which is
	nearly half again. Two tests are enough for both, in any of the nine
	languages: a word carrying a dot inside it is an abbreviation rather than a
	sentence -- "e.g.", "i.e.", "U.S." -- and so is a short word that starts with
	a capital, which is every initial and every "Mr.", "Mrs.", "Dr.", "St." and
	"Nr." there is. What that turns down as well is a short capitalised sentence
	end such as "Yes.", and turning one of those down costs nothing but a piece
	that runs to the next sentence.
	"""
	tail = text.rstrip(_CLOSERS)
	if not tail:
		return False
	if tail.endswith(("?", "!", "\N{HORIZONTAL ELLIPSIS}", "\N{IDEOGRAPHIC FULL STOP}", "\N{FULLWIDTH EXCLAMATION MARK}", "\N{FULLWIDTH QUESTION MARK}")):
		return True
	if not tail.endswith(".") or len(tail) < 2 or tail[-2].isdigit():
		return False
	word = tail[:-1]
	return "." not in word and not (len(word) <= 3 and word[:1].isupper())


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
		self._engine = _openevv.Engine(self._onIndexReached)
		self._engine.open()
		self._voice = self._voiceId(self._engine.language, _openevv.VOICE_FIRST)
		log.debug("openevv: engine version %s" % self._engine.version)

	def terminate(self):
		self._engine.close()

	# ---- speaking ----------------------------------------------------

	def speak(self, speechSequence: SpeechSequence):
		engine = self._engine
		#: The pieces to hand over, in order. Nearly every utterance a screen
		#: reader says is one piece; a long one is several, so that asking for
		#: silence waits out a piece and not the whole of it.
		pieces = []
		batch = []
		text = []
		spelling = False
		#: Prosody annotations which have not yet been restored to the reader's
		#: configured value. An opening and its restore have to stay in one queue
		#: item, or a cancel between them leaks the change into later speech.
		prosody = set()
		#: Which language the text being built is in, since a sequence may
		#: change it more than once and each change is against the last.
		speaking = self._engine.language
		#: Whether anything in this sequence is meant to make a sound. A
		#: sequence of nothing but commands is silent because it should be, and
		#: the engine layer is told so rather than complaining about it.
		words = False

		#: What this piece has to say, and how much has gathered in it. A piece
		#: is what a cancel waits for, so it is closed at the first boundary
		#: past the limit rather than at the limit exactly.
		saying = False
		gathered = 0
		sentence = False

		def flush():
			if text:
				joined = "".join(text).encode("utf-8", "replace")
				batch.append((engine.addText, (joined,)))
				del text[:]

		def endPiece():
			"""Close the piece being gathered, if it has anything in it."""
			nonlocal batch, saying, gathered
			flush()
			if batch:
				pieces.append((batch, saying))
				batch = []
			saying = False
			gathered = 0

		def mayEndPiece():
			# Voice tags are arbitrary annotations supplied inside the text; when
			# enabled their state cannot be inferred here, so retain the old single
			# queue item just as for a command whose state is visibly open.
			return not spelling and not prosody and not self._voiceTags

		for item in speechSequence:
			if isinstance(item, str):
				said = self._processText(item)
				words = words or said.strip() != ""
				# Sentence ends are free boundaries because the engine already pauses
				# there. Whitespace after the much larger cap bounds a sentence which
				# has no end of its own.
				for part in _BOUNDARY.split(said):
					if not part:
						continue
					text.append(part)
					gathered += len(part)
					if part.strip():
						saying = True
						sentence = _endsSentence(part)
					else:
						if mayEndPiece() and (sentence or gathered >= PIECE_CAP):
							endPiece()
						sentence = False
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
				if item.isDefault:
					prosody.discard(PitchCommand)
				else:
					prosody.add(PitchCommand)
				text.append("`vb%d " % self._pitchToParam(item.newValue))
			elif isinstance(item, RateCommand):
				if item.isDefault:
					prosody.discard(RateCommand)
				else:
					prosody.add(RateCommand)
				text.append("`vs%d " % self._rateToParam(item.newValue))
			elif isinstance(item, VolumeCommand):
				if item.isDefault:
					prosody.discard(VolumeCommand)
				else:
					prosody.add(VolumeCommand)
				text.append("`vv%d " % self._volumeToParam(item.newValue))
			elif isinstance(item, LangChangeCommand):
				# A document saying part of itself is in another language.
				# Where the library has that language it is switched to, in
				# the order the sequence asks for it, so a German quotation
				# in an English page is read as German rather than as
				# English with German spelling.
				#
				# The switch has to be flushed first: it is a call and not an
				# annotation, so text already handed over would otherwise be
				# spoken in the language that came after it. The preset is
				# copied again because a language change replaces all eight
				# of its settings.
				language = self._languageFor(item.lang)
				if language is not None and language != speaking:
					flush()
					batch.append((engine.setLanguage, (language,)))
					batch.append((engine.copyVoice, (self._presetNow(),)))
					speaking = language
			else:
				log.error("openevv: unknown speech: %s" % item)

		flush()
		if spelling:
			batch.append((engine.addText, (b"`ts0 ",)))
		if batch or not pieces:
			pieces.append((batch, saying or not pieces and words))

		# One queue item per piece, so that a cancel drops the pieces that
		# have not started rather than having to wait for them. Only the last
		# reports the utterance finished.
		for position, (piece, expectAudio) in enumerate(pieces):
			last = position == len(pieces) - 1
			piece.append((engine.synthesize if last else engine.synthesizePart, (expectAudio,)))
			engine.post(piece)

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
		self._engine.control(
			[(self._engine.setParam, (_openevv.PARAM_DICTIONARY, 0 if enable else 1))],
		)

	def _get_voiceTags(self):
		return self._voiceTags

	def _set_voiceTags(self, enable):
		self._voiceTags = enable

	def _get_availableVoices(self):
		"""One voice per language and preset.

		A library may have several languages in it, and each has eight
		presets of its own, so what the reader is offered is the pairs: the
		language is what a document's own language is matched against and
		the preset is what it sounds like. A build with one language in it
		offers the eight it always did, under the same identifiers as
		before, so nothing a reader had chosen is lost.
		"""
		voices = OrderedDict()
		for language in self._engine.languages:
			names = self._engine.voiceNamesFor(language)
			for number, name in names.items():
				voices[self._voiceId(language, number)] = VoiceInfo(
					self._voiceId(language, number),
					"%s - %s" % (_openevv.nameOf(language), name),
					_openevv.localeOf(language),
				)
		return voices

	def _languageFor(self, locale):
		"""Which of the library's languages a document's locale means.

		A document says `de' or `de_DE' or `de-AT'; the library has one
		German. So the whole locale is tried first, and then just the
		language part of it, and anything the library does not have answers
		nothing, which leaves the voice where it was.
		"""
		if not locale:
			return None
		want = str(locale).replace("-", "_")
		short = want.split("_")[0].lower()
		loose = None
		for language in self._engine.languages:
			have = _openevv.localeOf(language)
			if have is None:
				continue
			if have.lower() == want.lower():
				return language
			if loose is None and have.split("_")[0].lower() == short:
				loose = language
		return loose

	def _presetNow(self):
		"""Which of the eight the reader has chosen, whatever language it was
		chosen in. A language change keeps the preset and changes what it
		sounds like, which is what a person expects of a voice."""
		try:
			return self._splitVoiceId(self._voice)[1]
		except (TypeError, ValueError):
			return _openevv.VOICE_FIRST

	def _voiceId(self, language, number):
		"""What a voice is called.

		Where the library has one language the name is the preset's number
		and nothing else, which is what it has always been and what a
		reader's saved choice holds. Where it has several the language goes
		in front, because the same eight numbers mean eight different
		voices in each.
		"""
		if len(self._engine.languages) < 2:
			return str(number)
		return "%d:%d" % (language, number)

	def _splitVoiceId(self, value):
		"""(language, preset) for a voice, however it is written. A bare
		number is the language in force, which is what a configuration
		written before there was more than one holds."""
		text = str(value)
		if ":" in text:
			language, number = text.split(":", 1)
			return int(language), int(number)
		return self._engine.language, int(text)

	def _get_voice(self):
		return self._voice

	def _set_voice(self, value):
		try:
			language, number = self._splitVoiceId(value)
		except ValueError:
			return
		if language not in self._engine.languages:
			return
		if number not in self._engine.voiceNamesFor(language):
			return

		self._voice = self._voiceId(language, number)
		# Copying a preset over the voice in force replaces every one of its
		# eight settings, so whatever the reader had chosen is gone; NVDA sets
		# rate, pitch and the rest again after a voice change, which is what
		# puts them back. A language change does the same thing for the same
		# reason, which is why the preset is copied after it and not before.
		batch = []
		if language != self._engine.language:
			batch.append((self._engine.setLanguage, (language,)))
		batch.append((self._engine.copyVoice, (number,)))
		self._engine.control(batch)

	def _get_language(self):
		return _openevv.localeOf(self._engine.language)

	def _post(self, which, value):
		# As a control step, not as speech: a setting asked for while speech is
		# being cancelled -- which every keystroke does -- would otherwise be
		# thrown away with the utterances, and the reader's choice would not
		# take.
		self._engine.control([(self._engine.setVoiceParam, (which, value))])

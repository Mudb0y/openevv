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

from autoSettingsUtils.driverSetting import (
	BooleanDriverSetting,
	DriverSetting,
	NumericDriverSetting,
)
from autoSettingsUtils.utils import StringParameterInfo
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
from . import _openevv_dictionaries as dictionaries

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
#: No interrupt makes the engine abandon an utterance -- the engine layer
#: beside this file says why -- so asking for silence means waiting for the
#: utterance in flight to finish synthesising into nothing. That is cheap for a line of a list and is not cheap for a
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

#: Which of the voice's settings each prosody command moves.
_PROSODY_PARAMS = {
	PitchCommand: _openevv.VOICE_PITCH,
	RateCommand: _openevv.VOICE_SPEED,
	VolumeCommand: _openevv.VOICE_VOLUME,
}

#: Closing marks which may follow sentence-final punctuation.
_CLOSERS = "\"')]}\N{RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK}\N{RIGHT DOUBLE QUOTATION MARK}\N{RIGHT SINGLE QUOTATION MARK}"


def _endsSentence(text):
	"""Whether a word ends a sentence rather than a dotted number.

	A dot is the doubtful one and the doubt is not symmetrical. A boundary the
	engine would not have made costs an audible pause -- it ends a clause there,
	and that is 0.40 s of gap -- while a boundary declined costs only a longer
	wait on the next cancel. So a dot has to argue for itself.

	An abbreviation and an initial are what it fails on. "Mr. Jones" split after
	the dot measured 0.70 s longer than the same sentence whole with
	abbreviations expanded -- off, the engine ends a sentence there itself and
	the split costs nothing -- and "J. R. R.
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
	description = "OpenEVV"

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
		BooleanDriverSetting("phrasePrediction", _("Phrase predi&ction"), False),
		DriverSetting(
			"pauseMode",
			# Translators: Label for a setting in voice settings dialog.
			_("Shorten pa&uses"),
			False,
			defaultVal=str(_openevv.DEFAULT_PAUSE_MODE),
		),
		# Translators: Label for a setting in voice settings dialog.
		BooleanDriverSetting("voiceTags", _("Allow backquote voice &tags"), False),
		# Translators: Label for a setting in voice settings dialog.
		DriverSetting("samplerate", _("Sa&mple rate"), False),
		# Translators: Label for a setting in voice settings dialog.
		BooleanDriverSetting("wideband", _("&Wideband above 11 kHz"), False),
		# Translators: Label for a setting in voice settings dialog.
		DriverSetting("dictionarySet", _("Dictionary s&et"), False),
		BooleanDriverSetting(
			"personalDictionary",
			# Translators: Label for a setting in voice settings dialog.
			_("Use personal dictionary entrie&s"),
			False,
			defaultVal=True,
		),
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
		self._phrasePrediction = False
		self._voiceTags = False
		self._dictionarySet = dictionaries.NO_SET
		self._personalDictionary = True
		self._engine = _openevv.Engine(self._onIndexReached)
		self._engine.open()
		self._voice = self._voiceId(self._engine.language, _openevv.VOICE_FIRST)
		self._applyDictionaries()
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
		# Phrase prediction is the instance's own state and is said again at
		# the head of every utterance rather than once: an utterance that set
		# it can be dropped by a cancel before it starts, and nothing would
		# then say it never happened.
		batch = [(engine.addText, (b"`pp1 " if self._phrasePrediction else b"`pp0 ",))]
		text = []
		spelling = False
		#: Prosody annotations which have not yet been restored to the reader's
		#: configured value. An opening and its restore have to stay in one queue
		#: item, or a cancel between them leaks the change into later speech.
		prosody = set()
		#: Which language the text being built is in, since a sequence may
		#: change it more than once and each change is against the last. A
		#: sequence starts in the reader's own, because the one before put it
		#: back.
		home = self._home()
		speaking = home
		switched = False
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
				joined = _openevv.textFor(speaking, "".join(text))
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
				# spoken in the language that came after it. A command naming
				# no language is NVDA asking for the reader's own back.
				language = (self._languageFor(item.lang) if item.lang
				            else home)
				if language is not None and language != speaking:
					flush()
					batch.append((engine.selectLanguage,
					              (language, self._presetNow())))
					speaking = language
					switched = True
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

		# A document's language is its own and ends with it, whether or not
		# NVDA says so: otherwise everything after a German quotation is
		# German. Sent as a control step, since a cancel throws speech away
		# and this has to arrive whatever was cancelled.
		if switched:
			engine.control([(engine.selectLanguage, (home, self._presetNow()))])

		# The same for a rate, pitch or volume the sequence never changed back.
		# NVDA ends an utterance after each spelled character and sends the
		# change back as an utterance of its own, which the next keystroke
		# cancels before it reaches here, so a typing rate set for one
		# character would otherwise stay for everything after it.
		if prosody:
			engine.control([(
				engine.restoreVoiceParams,
				(tuple(sorted(_PROSODY_PARAMS[command] for command in prosody)),),
			)])

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

	def _get_phrasePrediction(self):
		return self._phrasePrediction

	def _set_phrasePrediction(self, enable):
		self._phrasePrediction = enable

	def _get_availablePausemodes(self):
		"""Which pauses the engine shortens, numbered as the IBMTTS and
		Eloquence 64 drivers number their setting of the same name.

		The engine pauses for as long as at a full stop wherever it finishes a
		stretch of text: at the end of every utterance, and at every change of
		voice, rate, pitch or language, which is where a capital letter is
		spelled at a raised pitch. Where the text ends in punctuation that
		pause is the sentence's; where it does not, nothing asked for it.
		"""
		return OrderedDict(
			(
				# Translators: An option of the "Shorten pauses" setting.
				("0", StringParameterInfo("0", _("Never"))),
				# Translators: An option of the "Shorten pauses" setting.
				("1", StringParameterInfo("1", _("At end of text only"))),
				# Translators: An option of the "Shorten pauses" setting.
				("2", StringParameterInfo("2", _("Always"))),
			)
		)

	def _get_pauseMode(self):
		return str(self._engine.pauseMode)

	def _set_pauseMode(self, value):
		try:
			mode = int(value)
		except (TypeError, ValueError):
			mode = None
		if mode not in _openevv.PAUSE_MODES:
			log.error("openevv: %r is not a pause mode" % (value,))
			return
		self._engine.control([(self._engine.setPauseMode, (mode,))])

	def _get_voiceTags(self):
		return self._voiceTags

	def _set_voiceTags(self, enable):
		self._voiceTags = enable

	def _get_availableSamplerates(self):
		"""What the synthesiser can be run at.

		Not the speaking rate, which NVDA already calls the rate: this is the
		sample rate. Eleven thousand and twenty five is what Eloquence has
		always sounded like and is the default; below it there is nothing to
		gain but the eight thousand IBM shipped for the telephone.

		Above it the engine still runs at eleven thousand and twenty five and
		the rate is raised from there, so the voice is the same one at every
		setting and what a higher rate buys is an audio device handed
		something it wants without resampling it again on the way out --
		unless the wideband voice is on, which keeps that voice below about
		5.4 kHz and adds a top above it.
		"""
		return OrderedDict(
			(
				str(hz),
				StringParameterInfo(
					str(hz),
					# Translators: A sample rate, shown in kilohertz.
					_("%.3g kHz") % (hz / 1000.0),
				),
			)
			for _number, hz in _openevv.SAMPLE_RATES
		)

	def _get_samplerate(self):
		return str(self._engine.sampleRate)

	def _set_samplerate(self, value):
		# Through the queue like every other setting, so it lands between
		# utterances: the player is replaced along with the rate and swapping
		# one out from under audio being fed to it is how a reader ends up
		# with no voice.
		try:
			hz = int(value)
		except (TypeError, ValueError):
			log.error("openevv: %r is not a sample rate" % (value,))
			return
		self._engine.control([(self._engine.setSampleRate, (hz,))])

	def _get_wideband(self):
		return self._engine.wideband

	def _set_wideband(self, enable):
		self._engine.control([(self._engine.setWideband, (enable,))])

	def _get_availableDictionarysets(self):
		"""The managed dictionary sets installed add-ons offer, and none.

		A set chosen and since uninstalled is still offered, as not
		installed, so that opening the dialog does not quietly change the
		choice; it comes back into force if the set does.
		"""
		sets = OrderedDict()
		# Translators: The choice of no managed dictionary set.
		sets[dictionaries.NO_SET] = StringParameterInfo(dictionaries.NO_SET, _("None"))
		installed = dictionaries.managedSets()
		for one in sorted(installed.values(), key=lambda s: s.name.lower()):
			sets[one.id] = StringParameterInfo(one.id, "%s (%s)" % (one.name, one.version))
		if self._dictionarySet not in sets:
			sets[self._dictionarySet] = StringParameterInfo(
				self._dictionarySet,
				# Translators: A chosen dictionary set that is no longer installed.
				_("%s (not installed)") % self._dictionarySet,
			)
		return sets

	def _get_dictionarySet(self):
		return self._dictionarySet

	def _set_dictionarySet(self, value):
		self._dictionarySet = str(value) if value else dictionaries.NO_SET
		self._applyDictionaries()

	def _get_personalDictionary(self):
		return self._personalDictionary

	def _set_personalDictionary(self, enable):
		self._personalDictionary = bool(enable)
		self._applyDictionaries()

	def _applyDictionaries(self):
		# Worked out here, on NVDA's thread, because finding a managed set
		# means asking NVDA for its add-ons; the engine is handed folders.
		try:
			folders = dictionaries.folders(self._dictionarySet, self._personalDictionary)
		except Exception:  # noqa: BLE001
			log.error("openevv: could not work out where dictionaries are", exc_info=True)
			return
		self._engine.control([(self._engine.setDictionaryFolders, (folders,))])

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

	def _home(self):
		"""The language the reader chose, which a document's own changes of
		language are made from and go back to."""
		try:
			return self._splitVoiceId(self._voice)[0]
		except (TypeError, ValueError):
			return self._engine.language

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

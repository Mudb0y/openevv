# The engine behind the openevv synthesiser driver.
#
# One instance of eci.dll, owned by one thread, feeding NVDA's wave player.
# The driver beside this file turns a speech sequence into text and
# annotations; everything here is about getting that text through the library
# and the samples back out in the right order.
#
# Four things about it are deliberate and are the parts to leave alone.
#
# The engine is called from one thread and one only. The library keeps its own
# synthesis thread and hands work to it, and the calls that queue work are not
# written to be entered from two threads at once. So every call goes through a
# queue to the thread made here.
#
# Nothing ever interrupts the engine, and that is not a workaround waiting to be
# replaced by something better. Cancelling stops the player and throws the
# samples away while the utterance finishes synthesising into nothing.
#
# There is no cheaper way, and this was settled by measurement rather than
# argument. All three routes the interface offers cost the same: on real
# Windows, the wall clock from asking for silence to the first samples of the
# next utterance is 477 ms letting it finish, 478 ms answering eciDataAbort from
# the callback, and 488 ms calling eciStop from the cancelling thread. A cold
# start on an idle engine is 128 ms and a cancel with nothing in flight costs
# under a millisecond, so the whole of the difference is the utterance already
# being spoken, and answering the abort stops the engine handing over buffers
# without stopping it finishing the work.
#
# The reason is in the machine rather than the interface, and docs/status.md has
# it: the language's rules build a shared structure as they go and later rules
# assume the earlier ones finished it, so there is no point at which a rule can
# be abandoned safely. Letting the unit of work finish is the only safe stop.
# Six attempts at making the engine abandon one are recorded there, including
# one that gave 459 rules their own give-up tail and still faulted.
#
# So the cost is the engine's and not this file's. What reduces it is making the
# engine faster: building the language's rules as C rather than interpreting the
# bytecode cuts it by about two thirds.
#
# Samples are held back until an index mark arrives, and then handed to the
# player with a callback attached. The engine flushes a short buffer of its own
# just before it reports a mark, so what has accumulated when the mark arrives
# is exactly the audio in front of it. That makes a mark land on the sample it
# belongs to rather than a buffer later, which is what the review cursor and
# the typing echo need.
#
# The engine's callback blocks in the player, and that is the point. A full
# player means the engine stops producing, which is how a long passage is
# synthesised at the speed it is spoken instead of all at once into memory.

import ctypes
import os
import queue
import sys
import threading
import time

import config
import nvwave
from logHandler import log

#: The formant voice runs at this rate and nothing changes it.
SAMPLE_RATE = 11025

#: How many samples the library is given room for in one go.
FRAME = 2048

#: How much audio to gather before handing any to the player, in bytes. Small
#: enough that speech starts promptly, large enough not to feed in dribbles.
FEED_AT = FRAME * 2

#: How long the engine's thread may sit inside the player before that is taken
#: as a stall rather than as back pressure.
#:
#: Blocking in the player is the design and not a fault: a full player is what
#: stops the engine running ahead of the speech. But there is one thread and
#: the player has no timeout, so a player that stops draining blocks it for
#: ever -- and with it every utterance queued behind, with nothing said to the
#: reader and nothing written down. Speech then stays stopped until something
#: else asks for silence and stops the player as a side effect.
#:
#: Longer than any buffer can honestly take to play, so back pressure never
#: trips it: the player holds a few hundred milliseconds and this is five
#: seconds.
STUCK_AFTER = 5.0

#: How often to look. Cheap enough to leave running and short enough that a
#: reader notices a recovery rather than a silence.
WATCH_EVERY = 0.5

# What the callback is told.
MSG_WAVEFORM = 0
MSG_PHONEME = 1
MSG_INDEX = 2

# What it answers.
DATA_NOT_PROCESSED = 0
DATA_PROCESSED = 1
DATA_ABORT = 2

# The engine's settings, by number.
PARAM_SYNTH_MODE = 0
PARAM_INPUT_TYPE = 1
PARAM_DICTIONARY = 3
PARAM_REAL_WORLD = 8
PARAM_LANGUAGE = 9

# A voice's, likewise.
VOICE_GENDER = 0
VOICE_HEAD_SIZE = 1
VOICE_PITCH = 2
VOICE_FLUCTUATION = 3
VOICE_ROUGHNESS = 4
VOICE_BREATHINESS = 5
VOICE_SPEED = 6
VOICE_VOLUME = 7

#: What each of those will take, which is what a value has to be clamped to
#: before it is offered: the engine refuses one out of range and answers with
#: what the setting was, which is indistinguishable from success.
VOICE_RANGE = {
	VOICE_GENDER: (0, 1),
	VOICE_HEAD_SIZE: (0, 100),
	VOICE_PITCH: (0, 100),
	VOICE_FLUCTUATION: (0, 100),
	VOICE_ROUGHNESS: (0, 100),
	VOICE_BREATHINESS: (0, 100),
	VOICE_SPEED: (0, 250),
	VOICE_VOLUME: (0, 100),
}

# What is on the queue: an utterance, which is dropped if silence has been
# asked for since it was put there, or a control step, which is not.
SPEECH = 0
CONTROL = 1

#: The eight voices the engine ships. It is asked for their names rather than
#: told them; this is only how many to ask about.
VOICE_FIRST = 1
VOICE_LAST = 8

#: What locale each of IBM's languages is, since the library answers with the
#: number and the reader wants a locale. The number is a family in the third
#: byte and a dialect in the low one -- 0x10000 is US English and 0x10001
#: British -- and these are the nine the SDK has. A library built with
#: something not here is still usable: it is offered under its number and the
#: reader is told no locale, which loses the language of a voice and nothing
#: else.
LOCALES = {
	0x10000: "en_US",
	0x10001: "en_GB",
	0x20000: "es_ES",
	0x20001: "es_MX",
	0x30000: "fr_FR",
	0x30001: "fr_CA",
	0x40000: "de_DE",
	0x50000: "it_IT",
	0x80000: "ja_JP",
}

#: And what to call it in a list of voices. NVDA shows the voice's name, so
#: the language has to be in it: there are eight presets per language and
#: their names repeat.
LANGUAGE_NAMES = {
	0x10000: "US English",
	0x10001: "British English",
	0x20000: "Castilian Spanish",
	0x20001: "Latin American Spanish",
	0x30000: "French",
	0x30001: "Canadian French",
	0x40000: "German",
	0x50000: "Italian",
	0x80000: "Japanese",
}


def localeOf(language):
	"""The locale a language is, or None where it is not one we know."""
	return LOCALES.get(language)


def nameOf(language):
	"""What to call a language in front of a person."""
	return LANGUAGE_NAMES.get(language, "0x%x" % language)

_CALLBACK = ctypes.WINFUNCTYPE(
	ctypes.c_int,
	ctypes.c_void_p,
	ctypes.c_int,
	ctypes.c_long,
	ctypes.c_void_p,
)


def libraryName():
	"""Which of the two libraries this process can load.

	Both are shipped. A screen reader is one bitness or the other and the
	library has to match, since this loads it into the reader's own process
	rather than hosting it somewhere else.
	"""
	return "eci.dll" if sys.maxsize > 2**32 else "eci32.dll"


def libraryPath():
	return os.path.join(os.path.dirname(__file__), "openevv_engine", libraryName())


class OpenEvvError(Exception):
	pass


class Engine:
	"""Owns the library, and the one thread allowed to call into it.

	It holds a thread rather than being one. Subclassing threading.Thread shares
	its namespace, and Python 3.13 keeps the thread's own handle in _handle --
	which is what this called the engine's instance handle, so joining the thread
	looked up join on an ECI handle and the add-on raised while NVDA was
	switching synthesiser away from it. Holding a thread cannot collide with
	anything the standard library adds later.
	"""

	def __init__(self, onIndex):
		self._thread = threading.Thread(
			target=self._run, name="openevv.engine", daemon=True
		)
		self._onIndex = onIndex
		self._work = queue.Queue()
		self._ready = threading.Event()
		self._failure = None
		self._instance = None
		self._dll = None
		self._buffer = (ctypes.c_short * FRAME)()
		self._callback = _CALLBACK(self._message)
		self._held = bytearray()
		self._pendingIndexes = []
		self._discarding = False
		self._produced = 0
		#: Whether the utterance in flight has already been reported
		#: finished. Nothing is in flight to begin with.
		self._finished = True
		#: When the engine's thread went into the player and what for, or None
		#: when it is not in there. Written by that thread and read by the
		#: watchdog; a stale read costs one more look round the loop.
		self._blockedSince = None
		self._blockedIn = ""
		#: Whether the reader asked for the pause. A paused player does not
		#: drain and blocking on one is meant, so the watchdog leaves it alone.
		self._paused = False
		self._watchdog = None
		self._stopWatching = threading.Event()
		self.player = None
		self.voiceNames = {}
		#: Every language the library has, and the one in force.
		self.languages = []
		self.language = None
		#: The eight presets of each language, read once on the way in.
		self.voiceNamesByLanguage = {}
		self.version = ""
		#: What each of the eight settings of the voice in force is, kept here
		#: so that the driver can answer a settings dialog without calling
		#: into the library from another thread.
		self.voiceParams = {}

	# ---- what the driver asks of it ----------------------------------

	def open(self):
		self._thread.start()
		self._ready.wait()
		if self._failure is not None:
			raise self._failure
		self._watchdog = threading.Thread(
			target=self._watch, name="openevv.watchdog", daemon=True
		)
		self._watchdog.start()

	def alive(self):
		return self._thread.is_alive()

	def close(self):
		"""Shut down, and do not leave the audio device held if it will not.

		The player is closed only once the thread is known to have stopped. It
		is the thread that feeds the player, so closing it out from under one
		still running is how a screen reader ends up with a synthesiser that
		will not speak after this one has been switched away.
		"""
		self._stopWatching.set()
		self.cancel()
		self._work.put(None)
		self._thread.join(timeout=5)

		if self._thread.is_alive():
			log.error("openevv: the engine's thread would not stop; leaving the"
			          " player open rather than closing it under one still running")
			return

		if self.player is not None:
			self.player.close()
			self.player = None

	def post(self, batch):
		"""Run these calls on the engine's thread, in this order."""
		self._work.put((SPEECH, batch))

	def control(self, batch):
		"""The same, for something that is not speech.

		A setting is asked for once and has to arrive. Queued as speech it is
		thrown away by any cancel that overtakes it -- and since every
		keystroke cancels, a voice or a rate chosen at the wrong moment simply
		did not happen, with the dialog still showing what was asked for.
		"""
		self._work.put((CONTROL, batch))

	def cancel(self):
		"""Stop now, and throw away what has not been spoken.

		Nothing here tells the engine anything, and that is the whole design.
		Both of the ways the interface offers to interrupt an utterance fault
		this engine: answering eciDataAbort from the callback, and calling
		eciStop while synthesis is running. Either one dies dereferencing a
		null in vinitloc_new, which is how the add-on crashed NVDA the first
		time it was asked for silence.

		So the samples are thrown away instead. The callback goes on answering
		normally and simply drops what it is handed, the utterance finishes
		synthesising into nothing, and the engine's state is never touched.
		What that costs is the synthesis time of the audio nobody will hear,
		and synthesis runs some eighty times faster than speech: throwing away
		eleven seconds of a sentence measures at about a seventh of a second.

		Stopping the player is what actually silences it, and it also unblocks
		the callback if it is waiting for room. Draining the queue drops the
		utterances that have not started yet.
		"""
		self._discarding = True
		if self.player is not None:
			self.player.stop()
		self._drain()
		# Cleared on the engine's own thread, so it happens after the utterance
		# being discarded has finished and before the next one starts.
		self._work.put((CONTROL, [(self._resume, ())]))

	def pause(self, switch):
		self._paused = switch
		if self.player is not None:
			self.player.pause(switch)

	def _drain(self):
		"""Drop the utterances that have not started, and only those.

		A control step is not speech and is not silenced: it is put back in the
		order it was in, along with the closing sentinel, which is not ours to
		drop either.
		"""
		keep = []
		while True:
			try:
				item = self._work.get_nowait()
			except queue.Empty:
				break
			self._work.task_done()
			if item is None or item[0] == CONTROL:
				keep.append(item)
		for item in keep:
			self._work.put(item)

	# ---- the calls themselves, all on this thread --------------------

	def addText(self, text):
		# A false answer here means the engine refused the call, and that is all
		# it means. Text the engine simply had no room for is answered as
		# success, deliberately and as IBM's own build did, so this cannot
		# report a dropped stretch. What can is the count of samples in
		# synthesize.
		if not self._dll.eciAddText(self._instance, text):
			log.debugWarning("openevv: the engine refused a stretch of text")

	def index(self, n):
		if not self._dll.eciInsertIndex(self._instance, n):
			log.debugWarning("openevv: the engine refused an index mark")

	def synthesizePart(self, expectAudio=True):
		"""Speak a piece of an utterance that is not the end of it.

		The audio goes to the player as it always does; what does not happen is
		waiting for the player to run dry and saying the utterance finished,
		because it has not.
		"""
		self._speak(expectAudio, last=False)

	def synthesize(self, expectAudio=True):
		"""Speak what has been added, and do not come back until it is done."""
		self._speak(expectAudio, last=True)

	def _speak(self, expectAudio, last):
		"""Hand what has been added to the engine and wait it out.

		eciSynthesize only starts the utterance; eciSynchronize is what drives
		it and returns once the last buffer has been handed over. Blocking here
		is wanted: it is what makes the end of an utterance a fact rather than
		something to be inferred from a mark, and the callback's own blocking in
		the player is what paces it.

		Waiting it out is also what a cancel costs, since the engine cannot be
		interrupted, which is why the driver hands long text over in pieces:
		the wait is then one piece and not the whole message.
		"""
		self._held = bytearray()
		self._pendingIndexes = []
		self._produced = 0
		self._finished = False

		if not self._dll.eciSynthesize(self._instance):
			log.error("openevv: the engine refused to speak")
			if last:
				self._finish()
			return

		self._dll.eciSynchronize(self._instance)

		if self._discarding:
			# Cancelled part way. Nothing to hand over and nothing to report:
			# whoever cancelled is not waiting to be told it finished.
			self._held = bytearray()
			self._pendingIndexes = []
			return

		if expectAudio and self._produced == 0:
			# The engine answers success for text it had no room for, so an
			# utterance that produced nothing is the only sign that something
			# was dropped. Silence with nothing said about it is the one
			# failure a screen reader cannot be diagnosed from.
			#
			# Only where there was something to say, though. An utterance of
			# nothing but annotations -- a spelling mode turned off and no words
			# with it -- is silent because it should be.
			log.warning("openevv: the engine took the text and made no audio;"
			            " something was dropped and it does not say so")

		# Every piece hands over what it gathered, or its tail is thrown away
		# by the next one; only the last waits for the player to run dry and
		# says the utterance is over.
		self._flush(last=last)
		if last:
			self._finish()

	def setParam(self, which, value):
		return self._dll.eciSetParam(self._instance, which, value)

	def getVoiceParam(self, which):
		return self._dll.eciGetVoiceParam(self._instance, 0, which)

	def setVoiceParam(self, which, value):
		lo, hi = VOICE_RANGE[which]
		value = max(lo, min(int(value), hi))
		self._dll.eciSetVoiceParam(self._instance, 0, which, value)
		self.voiceParams[which] = value

	def copyVoice(self, number):
		if not self._dll.eciCopyVoice(self._instance, number, 0):
			log.debugWarning("openevv: the engine refused voice %d" % number)
		self._readVoiceParams()

	def setLanguage(self, language):
		"""Speak another of the languages the library has.

		The engine takes this on the instance it already has -- underneath it
		is an engine change, which is the original's own arrangement -- so
		nothing is torn down. The eight presets belong to the language and are
		read again, because they are not the same numbers, and the caller has
		to put back whatever it had chosen: a language change is a voice
		change as well.
		"""
		if language == self.language or language not in self.languages:
			return False
		self.setParam(PARAM_LANGUAGE, language)
		self.language = language
		self._readVoiceNames()
		self._readVoiceParams()
		return True

	def voiceNamesFor(self, language):
		"""The eight presets of one language, whichever is in force."""
		return self.voiceNamesByLanguage.get(language, self.voiceNames)

	def _readVoiceNames(self):
		self.voiceNames = self._voiceNames()
		self.voiceNamesByLanguage[self.language] = self.voiceNames

	def _voiceNames(self):
		room = ctypes.create_string_buffer(64)
		names = {}
		for number in range(VOICE_FIRST, VOICE_LAST + 1):
			self._dll.eciGetVoiceName(self._instance, number, room)
			name = room.value.decode("latin-1").strip()
			names[number] = name or ("Voice %d" % number)
		return names

	def _readVoiceParams(self):
		for which in VOICE_RANGE:
			self.voiceParams[which] = self.getVoiceParam(which)

	def _resume(self):
		self._held = bytearray()
		self._pendingIndexes = []
		self._discarding = False

	# ---- the watchdog ------------------------------------------------

	def _enteringPlayer(self, what):
		self._blockedIn = what
		self._blockedSince = time.monotonic()

	def _leftPlayer(self):
		self._blockedSince = None

	def _watch(self):
		"""Break a stall in the player, and say that there was one.

		The engine's thread blocks in the player on purpose and usually for a
		fraction of a second. What it cannot do is tell a full player from one
		that has stopped taking audio, and there is no timeout to find out
		with, so a player in the second state holds the only thread there is
		and every utterance queued behind it. To the reader that is silence
		with nothing in the log, lasting until something else asks for silence
		and stops the player as a side effect.

		Stopping the player is what unblocks it, which is what cancelling
		already does, so the recovery here is the one the reader would
		otherwise have to find. Having abandoned the utterance, whatever was
		waiting on it is told it finished, or it waits for ever.
		"""
		while not self._stopWatching.wait(WATCH_EVERY):
			since = self._blockedSince
			# A pause is meant to stop the player draining, so blocking on one
			# is not a stall. Nor is a wait that has not gone on long enough.
			if since is None or self._paused:
				continue
			waited = time.monotonic() - since
			if waited < STUCK_AFTER:
				continue
			log.error("openevv: the engine's thread has been in the player's %s"
			          " for %.1f seconds with no pause asked for; abandoning the"
			          " utterance so speech can go on"
			          % (self._blockedIn, waited))
			self._blockedSince = None
			try:
				self.cancel()
			except Exception:  # noqa: BLE001
				log.error("openevv: the stall would not clear", exc_info=True)
			# Nobody asked for this silence, so nobody is resetting on the far
			# side of it: say the utterance finished.
			self._finish()

	# ---- the thread --------------------------------------------------

	def _run(self):
		try:
			self._start()
		except Exception as e:  # noqa: BLE001
			self._failure = e
			self._ready.set()
			return
		self._ready.set()

		while True:
			item = self._work.get()
			if item is None:
				self._work.task_done()
				break
			kind, batch = item
			if kind == SPEECH and self._discarding:
				# Asked for silence before any of this was submitted. Text the
				# engine has been given cannot be taken back -- eciClearInput
				# only empties the manual queue, which this mode never fills --
				# so an utterance is either submitted whole or not at all. Not
				# at all is free, and under rapid arrowing it is most of them.
				self._work.task_done()
				continue
			try:
				for fn, args in batch:
					fn(*args)
			except Exception:  # noqa: BLE001
				log.error("openevv: a call into the engine failed", exc_info=True)
			self._work.task_done()

		self._finishThread()

	def _start(self):
		path = libraryPath()
		if not os.path.isfile(path):
			raise OpenEvvError("openevv: there is no library at %s" % path)

		dll = ctypes.WinDLL(path)

		# Every handle said out loud. A handle is a pointer, and left to
		# itself ctypes narrows one to an int and hands the engine half an
		# address.
		dll.eciNewEx.restype = ctypes.c_void_p
		dll.eciNewEx.argtypes = [ctypes.c_int]
		dll.eciDelete.restype = ctypes.c_void_p
		dll.eciDelete.argtypes = [ctypes.c_void_p]
		dll.eciGetAvailableLanguages.argtypes = [
			ctypes.c_void_p,
			ctypes.POINTER(ctypes.c_int),
		]
		dll.eciRegisterCallback.argtypes = [
			ctypes.c_void_p,
			ctypes.c_void_p,
			ctypes.c_void_p,
		]
		dll.eciSetOutputBuffer.argtypes = [
			ctypes.c_void_p,
			ctypes.c_int,
			ctypes.c_void_p,
		]
		dll.eciSetParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
		dll.eciGetParam.argtypes = [ctypes.c_void_p, ctypes.c_int]
		dll.eciSetVoiceParam.argtypes = [
			ctypes.c_void_p,
			ctypes.c_int,
			ctypes.c_int,
			ctypes.c_int,
		]
		dll.eciGetVoiceParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
		dll.eciGetVoiceName.argtypes = [
			ctypes.c_void_p,
			ctypes.c_int,
			ctypes.c_char_p,
		]
		dll.eciCopyVoice.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
		dll.eciAddText.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
		dll.eciInsertIndex.argtypes = [ctypes.c_void_p, ctypes.c_int]
		dll.eciSynthesize.argtypes = [ctypes.c_void_p]
		dll.eciSynchronize.argtypes = [ctypes.c_void_p]
		dll.eciVersion.argtypes = [ctypes.c_char_p]

		# Asked with no room, which answers how many there are, and again with
		# room. Only US English is in the library, but ask rather than assume.
		count = ctypes.c_int(0)
		if dll.eciGetAvailableLanguages(None, ctypes.byref(count)):
			raise OpenEvvError("openevv: the library would not say what languages it has")
		if count.value < 1:
			raise OpenEvvError("openevv: the library has no language in it")
		languages = (ctypes.c_uint * count.value)()
		dll.eciGetAvailableLanguages(languages, ctypes.byref(count))

		handle = dll.eciNewEx(languages[0])
		if not handle:
			raise OpenEvvError("openevv: the library would not make an instance")

		self._dll = dll
		self._instance = handle

		dll.eciRegisterCallback(handle, self._callback, None)
		if not dll.eciSetOutputBuffer(handle, FRAME, self._buffer):
			raise OpenEvvError("openevv: the library refused a sample buffer")

		# Annotations on, because that is how prosody, spelling and pauses are
		# said inside the text. The synthesis mode is left as it came: the
		# queued mode exists for a caller that wants to set a voice between
		# stretches, and this driver says such things in the text instead.
		dll.eciSetParam(handle, PARAM_INPUT_TYPE, 1)

		room = ctypes.create_string_buffer(64)
		dll.eciVersion(room)
		self.version = room.value.decode("latin-1").strip()

		self.languages = [languages[i] for i in range(count.value)]
		self.language = self.languages[0]

		# Every language's presets, read now rather than when one is asked
		# for: the reader wants the whole list of voices before it has
		# chosen any of them, and reading a language's presets means being
		# in that language. Going through them here costs one pass on the
		# way in and nothing afterwards.
		for language in self.languages:
			if language != self.language:
				dll.eciSetParam(handle, PARAM_LANGUAGE, language)
			self.voiceNamesByLanguage[language] = self._voiceNames()
		if len(self.languages) > 1:
			dll.eciSetParam(handle, PARAM_LANGUAGE, self.language)
		self.voiceNames = self.voiceNamesByLanguage[self.language]

		self._readVoiceParams()

		self.player = nvwave.WavePlayer(
			channels=1,
			samplesPerSec=SAMPLE_RATE,
			bitsPerSample=16,
			outputDevice=config.conf["audio"]["outputDevice"],
		)

	def _finishThread(self):
		try:
			if self._instance is not None:
				self._dll.eciDelete(self._instance)
		except Exception:  # noqa: BLE001
			log.error("openevv: the engine would not shut down", exc_info=True)
		self._instance = None

	# ---- the engine's callback ---------------------------------------

	def _message(self, handle, message, param, data):
		"""What the engine hands over, and what becomes of it.

		The answer is always eciDataProcessed, even when the samples are being
		thrown away. eciDataAbort is what the interface offers for refusing the
		rest of an utterance and it faults this engine, so it is never used --
		see cancel. Nothing here changes any engine state, which is what makes
		interrupting safe.
		"""
		try:
			if self._discarding:
				return DATA_PROCESSED
			if message == MSG_WAVEFORM:
				self._produced += param
				self._held.extend(bytes(self._buffer)[: param * 2])
				if len(self._held) >= FEED_AT:
					self._flush()
			elif message == MSG_INDEX:
				# The audio in hand is what runs up to this mark, so it goes
				# now with the mark hung off the end of it.
				self._pendingIndexes.append(int(param))
				self._flush()
			return DATA_PROCESSED
		except Exception:  # noqa: BLE001
			log.error("openevv: the engine's callback failed", exc_info=True)
			return DATA_PROCESSED

	def _flush(self, last=False):
		if self._held:
			audio = bytes(self._held)
			del self._held[:]
			marks = self._pendingIndexes
			self._pendingIndexes = []
			self._enteringPlayer("feed")
			try:
				if marks:
					self.player.feed(audio, onDone=lambda m=marks: self._report(m))
				else:
					self.player.feed(audio)
			except Exception:  # noqa: BLE001
				log.error("openevv: a buffer would not play", exc_info=True)
				self._report(marks)
			finally:
				self._leftPlayer()
		else:
			# A mark with no audio in front of it still has to be reported, or
			# whatever is waiting on it waits for ever.
			self._reportIndexes()

		# Waiting for the end of the audio has to happen whether or not there
		# was anything left to hand over: an utterance ending on a mark leaves
		# nothing in hand, and saying it is finished before the last buffer has
		# played cuts the next one in over it.
		if last:
			self._enteringPlayer("idle")
			try:
				self.player.idle()
			except Exception:  # noqa: BLE001
				log.debugWarning("openevv: the player would not go idle", exc_info=True)
			finally:
				self._leftPlayer()

	def _reportIndexes(self):
		marks = self._pendingIndexes
		self._pendingIndexes = []
		self._report(marks)

	def _report(self, marks):
		for mark in marks:
			self._onIndex(mark)

	def _finish(self):
		"""Say the utterance is over, once, whoever gets there first.

		Two threads can be the one to say it. Where the watchdog breaks a
		stall inside the last player.idle(), it abandons the utterance and
		reports it finished -- and the engine's thread then comes out of
		the player anyway, past the point where it checks whether it was
		cancelled, and reports it finished as well. Two reports of one
		utterance leave NVDA's speech manager a step ahead of itself, so
		the second is dropped here rather than at either caller.
		"""
		if self._finished:
			return
		self._finished = True
		self._onIndex(None)

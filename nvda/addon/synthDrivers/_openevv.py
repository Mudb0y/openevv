# The engine behind the openevv synthesiser driver.
#
# One instance of eci.dll, owned by one thread, feeding NVDA's wave player.
# The driver beside this file turns a speech sequence into text and
# annotations; everything here is about getting that text through the library
# and the samples back out in the right order.
#
# Three things about it are deliberate and are the parts to leave alone.
#
# The engine is called from one thread and one only. The library keeps its own
# synthesis thread and hands work to it, and the calls that queue work are not
# written to be entered from two threads at once. So every call goes through a
# queue to the thread made here, including stopping, which is why cancelling
# posts a stop rather than making one.
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

#: An index of our own, put after the last of the caller's, so the callback
#: knows when an utterance's audio has all arrived. The engine carries an
#: index in a 32-bit slot and NVDA's own indices are small, so a large one
#: cannot collide.
END_MARK = 0x7FFFFFFF

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

#: The eight voices the engine ships. It is asked for their names rather than
#: told them; this is only how many to ask about.
VOICE_FIRST = 1
VOICE_LAST = 8

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


class Engine(threading.Thread):
	"""The thread that owns the library. Nothing else calls into it."""

	def __init__(self, onIndex):
		super().__init__(name="openevv.engine", daemon=True)
		self._onIndex = onIndex
		self._work = queue.Queue()
		self._ready = threading.Event()
		self._failure = None
		self._handle = None
		self._dll = None
		self._buffer = (ctypes.c_short * FRAME)()
		self._callback = _CALLBACK(self._message)
		self._held = bytearray()
		self._pendingIndexes = []
		self._stopping = False
		self.player = None
		self.voiceNames = {}
		self.version = ""
		#: What each of the eight settings of the voice in force is, kept here
		#: so that the driver can answer a settings dialog without calling
		#: into the library from another thread.
		self.voiceParams = {}

	# ---- what the driver asks of it ----------------------------------

	def open(self):
		self.start()
		self._ready.wait()
		if self._failure is not None:
			raise self._failure

	def close(self):
		self.cancel()
		self._work.put(None)
		self.join(timeout=5)
		if self.player is not None:
			self.player.close()
			self.player = None

	def post(self, batch):
		"""Run these calls on the engine's thread, in this order."""
		self._work.put(batch)

	def cancel(self):
		"""Stop now, and throw away what has not been spoken.

		The order matters. Marking the abort first means the callback gives up
		at its next crossing; stopping the player unblocks it if it is waiting
		for room; draining the queue drops utterances that have not started.
		Only then is a stop posted, so that the library's own call happens on
		the thread that owns it.
		"""
		self._stopping = True
		if self.player is not None:
			self.player.stop()
		self._drain()
		self._work.put([(self._doStop, ())])

	def pause(self, switch):
		if self.player is not None:
			self.player.pause(switch)

	def _drain(self):
		while True:
			try:
				item = self._work.get_nowait()
			except queue.Empty:
				return
			self._work.task_done()
			if item is None:
				# Closing down: put it back, it is not ours to drop.
				self._work.put(None)
				return

	# ---- the calls themselves, all on this thread --------------------

	def addText(self, text):
		if not self._dll.eciAddText(self._handle, text):
			log.debugWarning("openevv: the engine refused a stretch of text")

	def index(self, n):
		if not self._dll.eciInsertIndex(self._handle, n):
			log.debugWarning("openevv: the engine refused an index mark")

	def synthesize(self):
		self._held = bytearray()
		self._pendingIndexes = []
		self.index(END_MARK)
		if not self._dll.eciSynthesize(self._handle):
			log.error("openevv: the engine refused to speak")
			self._finish()

	def setParam(self, which, value):
		return self._dll.eciSetParam(self._handle, which, value)

	def getVoiceParam(self, which):
		return self._dll.eciGetVoiceParam(self._handle, 0, which)

	def setVoiceParam(self, which, value):
		lo, hi = VOICE_RANGE[which]
		value = max(lo, min(int(value), hi))
		self._dll.eciSetVoiceParam(self._handle, 0, which, value)
		self.voiceParams[which] = value

	def copyVoice(self, number):
		if not self._dll.eciCopyVoice(self._handle, number, 0):
			log.debugWarning("openevv: the engine refused voice %d" % number)
		self._readVoiceParams()

	def _readVoiceParams(self):
		for which in VOICE_RANGE:
			self.voiceParams[which] = self.getVoiceParam(which)

	def _doStop(self):
		self._dll.eciStop(self._handle)
		self._held = bytearray()
		self._pendingIndexes = []
		self._stopping = False

	# ---- the thread --------------------------------------------------

	def run(self):
		try:
			self._start()
		except Exception as e:  # noqa: BLE001
			self._failure = e
			self._ready.set()
			return
		self._ready.set()

		while True:
			batch = self._work.get()
			if batch is None:
				self._work.task_done()
				break
			try:
				for fn, args in batch:
					fn(*args)
			except Exception:  # noqa: BLE001
				log.error("openevv: a call into the engine failed", exc_info=True)
			self._work.task_done()

		self._finishThread()

	def _start(self):
		path = _libraryPath()
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
		dll.eciStop.argtypes = [ctypes.c_void_p]
		dll.eciSpeaking.argtypes = [ctypes.c_void_p]
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
		self._handle = handle

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
		self.version = room.value.decode("mbcs", "replace")

		for number in range(VOICE_FIRST, VOICE_LAST + 1):
			dll.eciGetVoiceName(handle, number, room)
			name = room.value.decode("mbcs", "replace").strip()
			self.voiceNames[number] = name or ("Voice %d" % number)

		self._readVoiceParams()

		self.player = nvwave.WavePlayer(
			channels=1,
			samplesPerSec=SAMPLE_RATE,
			bitsPerSample=16,
			outputDevice=config.conf["audio"]["outputDevice"],
		)

	def _finishThread(self):
		try:
			if self._handle is not None:
				self._dll.eciDelete(self._handle)
		except Exception:  # noqa: BLE001
			log.error("openevv: the engine would not shut down", exc_info=True)
		self._handle = None

	# ---- the engine's callback ---------------------------------------

	def _message(self, handle, message, param, data):
		try:
			if self._stopping:
				return DATA_ABORT
			if message == MSG_WAVEFORM:
				self._held.extend(bytes(self._buffer)[: param * 2])
				if len(self._held) >= FEED_AT:
					self._flush()
			elif message == MSG_INDEX:
				if param == END_MARK:
					self._flush(last=True)
					self._finish()
				else:
					# The audio in hand is what runs up to this mark, so it
					# goes now with the mark hung off the end of it.
					self._pendingIndexes.append(int(param))
					self._flush()
			return DATA_ABORT if self._stopping else DATA_PROCESSED
		except Exception:  # noqa: BLE001
			log.error("openevv: the engine's callback failed", exc_info=True)
			return DATA_NOT_PROCESSED

	def _flush(self, last=False):
		if self._held:
			audio = bytes(self._held)
			del self._held[:]
			marks = self._pendingIndexes
			self._pendingIndexes = []
			try:
				if marks:
					self.player.feed(audio, onDone=lambda m=marks: self._report(m))
				else:
					self.player.feed(audio)
			except Exception:  # noqa: BLE001
				log.error("openevv: a buffer would not play", exc_info=True)
				self._report(marks)
		else:
			# A mark with no audio in front of it still has to be reported, or
			# whatever is waiting on it waits for ever.
			self._reportIndexes()

		# Waiting for the end of the audio has to happen whether or not there
		# was anything left to hand over: an utterance ending on a mark leaves
		# nothing in hand, and saying it is finished before the last buffer has
		# played cuts the next one in over it.
		if last:
			try:
				self.player.idle()
			except Exception:  # noqa: BLE001
				log.debugWarning("openevv: the player would not go idle", exc_info=True)

	def _reportIndexes(self):
		marks = self._pendingIndexes
		self._pendingIndexes = []
		self._report(marks)

	def _report(self, marks):
		for mark in marks:
			self._onIndex(mark)

	def _finish(self):
		self._onIndex(None)

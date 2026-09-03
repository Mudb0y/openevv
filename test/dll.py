#!/usr/bin/env python3
"""Speak through eci.dll with ctypes, the way a screen reader add-on does.

The C harness beside this one, test/dll.c, checks that the names are exported
and that a callback survives the crossing. This checks the thing an add-on
actually depends on: that ctypes' own idea of the calling convention, of a
handle and of a callback matches what the library expects. Those are different
questions, and the second one is the one that bites -- a handle is sixty-four
bits and ctypes will happily pass thirty-two of them unless it is told.

A document in place of the text is read as SSML: the filter is registered
through the exported calls first, which is a second question again, since a
filter's entry point is a callback of the caller's and goes over as stdcall
with a pointer-to-pointer in front of it.

usage: dll.py <eci.dll> <out.wav> <text>
"""

import ctypes
import hashlib
import os
import struct
import sys
import time

FRAME = 2048


def main(path, out, text):
    # An absolute path, because since Python 3.8 ctypes does not look in the
    # working directory for a bare name. The add-on passes an absolute path for
    # the same reason, so this is what it does as well.
    dll = ctypes.WinDLL(os.path.abspath(path))

    # Everything that takes or answers a handle has to say so, or ctypes
    # truncates it to an int and the engine is handed half an address.
    dll.eciNewEx.restype = ctypes.c_void_p
    dll.eciNewEx.argtypes = [ctypes.c_int]
    dll.eciGetAvailableLanguages.argtypes = [ctypes.c_void_p,
                                             ctypes.POINTER(ctypes.c_int)]
    dll.eciRegisterCallback.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.c_void_p]
    dll.eciSetOutputBuffer.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                       ctypes.c_void_p]
    dll.eciSetParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    dll.eciInsertIndex.argtypes = [ctypes.c_void_p, ctypes.c_int]
    dll.eciAddText.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.eciSynthesize.argtypes = [ctypes.c_void_p]
    dll.eciSpeaking.argtypes = [ctypes.c_void_p]
    dll.eciDelete.restype = ctypes.c_void_p
    dll.eciDelete.argtypes = [ctypes.c_void_p]
    dll.eciRegisterFilter.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                      ctypes.c_void_p, ctypes.c_void_p,
                                      ctypes.c_int]
    dll.eciNewFilter.restype = ctypes.c_void_p
    dll.eciNewFilter.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    dll.eciActivateFilter.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    dll.eciGetFilteredText.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_char_p,
                                       ctypes.POINTER(ctypes.c_char_p)]

    buf = (ctypes.c_short * FRAME)()
    said = bytearray()

    callback = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                  ctypes.c_long, ctypes.c_void_p)

    @callback
    def on_message(h, msg, param, data):
        if msg == 0:                      # eciWaveformBuffer
            said.extend(bytes(buf)[:param * 2])
        return 1                          # eciDataProcessed

    # Asked with no room, which answers how many there are, and then with room.
    count = ctypes.c_int(0)
    if dll.eciGetAvailableLanguages(None, ctypes.byref(count)):
        raise SystemExit("dll.py: it would not say what languages it has")
    if count.value < 1:
        raise SystemExit("dll.py: it has no language in it")
    langs = (ctypes.c_uint * count.value)()
    dll.eciGetAvailableLanguages(langs, ctypes.byref(count))

    h = dll.eciNewEx(langs[0])
    if not h:
        raise SystemExit("dll.py: it would not make an instance")

    dll.eciRegisterCallback(h, on_message, None)
    if not dll.eciSetOutputBuffer(h, FRAME, buf):
        raise SystemExit("dll.py: it refused a sample buffer")
    dll.eciSetParam(h, 1, 1)              # annotations, as the add-on asks
    dll.eciInsertIndex(h, 4242)

    # A document goes through the SSML reader first, which means registering
    # the filter the library carries. What registering takes is the address
    # of a filter's entry point, and this library exports its own under the
    # name IBM's separate ssmlfilter.dll used, so the address is taken from
    # the library rather than made here -- and it goes over as a pointer to
    # a pointer, which is the shape registerFilter reads.
    if text.lstrip().startswith("<"):
        class Attrib(ctypes.Structure):
            _fields_ = [("eciFilterName", ctypes.c_char * 80),
                        ("language", ctypes.c_int)]

        entry = ctypes.cast(dll.ssmlFilterGetObject, ctypes.c_void_p)
        attrib = Attrib()
        rc = dll.eciRegisterFilter(h, 0, ctypes.byref(entry),
                                   ctypes.byref(attrib), 1)
        if rc:
            raise SystemExit("dll.py: it would not register the SSML filter")
        print("dll.py: registered [%s]"
              % attrib.eciFilterName.decode("ascii", "replace"))

        flt = dll.eciNewFilter(h, 0, 1)
        if not flt:
            raise SystemExit("dll.py: it would not make the SSML filter")
        dll.eciActivateFilter(h, flt)

        # A writable copy, because IBM's reader writes into the document it
        # is given where a numeric character reference ends.
        document = ctypes.create_string_buffer(text.encode("mbcs"))
        answer = ctypes.c_char_p(None)
        if dll.eciGetFilteredText(h, flt, document, ctypes.byref(answer)):
            raise SystemExit("dll.py: it would not read the document")
        if answer.value is None:
            raise SystemExit("dll.py: the reader answered nothing")
        text = answer.value.decode("mbcs")
        print("dll.py: read as [%s]" % text)

    if not dll.eciAddText(h, text.encode("mbcs")):
        raise SystemExit("dll.py: it refused the text")
    if not dll.eciSynthesize(h):
        raise SystemExit("dll.py: it refused to speak")

    for _ in range(30000):
        if not dll.eciSpeaking(h):
            break
        time.sleep(0.01)

    dll.eciDelete(h)

    with open(out, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(said)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, 11025, 11025 * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(said)))
        f.write(bytes(said))

    print("dll.py: %d samples through ctypes" % (len(said) // 2))
    print(hashlib.sha256(open(out, "rb").read()).hexdigest())
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3]))

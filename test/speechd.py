#!/usr/bin/env python3
"""Exercise sd_openevv through Speech Dispatcher's module protocol."""

import os
import select
import struct
import subprocess
import sys


class ModuleProcess:
    def __init__(self, binary, config):
        self.process = subprocess.Popen(
            [binary, config],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )

    def send(self, data):
        self.process.stdin.write(data.encode("utf-8"))
        self.process.stdin.flush()

    def read_line(self, timeout=15):
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if not ready:
            errorReady, _, _ = select.select([self.process.stderr], [], [], 0)
            error = self.process.stderr.read(4096) if errorReady else b""
            error = error.decode("utf-8", "replace")
            raise AssertionError(f"module timed out; stderr: {error}")
        line = self.process.stdout.readline()
        if not line:
            error = self.process.stderr.read().decode("utf-8", "replace")
            raise AssertionError(f"module closed unexpectedly; stderr: {error}")
        return line

    def expect(self, wanted):
        line = self.read_line()
        assert line == wanted, (wanted, line)

    def command_with_data(self, command, lines, acknowledgement):
        self.send(command + "\n")
        self.expect(acknowledgement)
        self.send("".join(f"{line}\n" for line in lines) + ".\n")

    def set(self, **values):
        self.command_with_data(
            "SET",
            [f"{name}={value}" for name, value in values.items()],
            b"203 OK RECEIVING SETTINGS\n",
        )
        self.expect(b"203 OK SETTINGS RECEIVED\n")

    def list_voices(self, language=None):
        command = "LIST VOICES"
        if language:
            command += " " + language
        self.send(command + "\n")
        voices = []
        while True:
            line = self.read_line()
            if line == b"200 OK VOICE LIST SENT\n":
                return voices
            voices.append(line.decode("utf-8").removeprefix("200-").strip().split("\t"))

    def speak(self, text, command="SPEAK", interrupt=None):
        self.icons = []
        self.command_with_data(command, [text], b"202 OK RECEIVING MESSAGE\n")
        self.expect(b"200 OK SPEAKING\n")
        self.expect(b"701 BEGIN\n")

        audio = bytearray()
        marks = []
        metadata = {}
        interrupted = False
        while True:
            line = self.read_line(30)
            if line.startswith(b"705-") and not line.startswith(b"705-AUDIO"):
                key, value = line[4:].decode("ascii").strip().split("=", 1)
                metadata[key] = int(value)
            elif line.startswith(b"705-AUDIO\0"):
                payload = line[len(b"705-AUDIO\0"):-1]
                decoded = bytearray()
                at = 0
                while at < len(payload):
                    if payload[at] == 0x7D:
                        at += 1
                        decoded.append(payload[at] ^ 0x20)
                    else:
                        decoded.append(payload[at])
                    at += 1
                audio.extend(decoded)
                self.expect(b"705 AUDIO\n")
                if interrupt and not interrupted:
                    self.send(interrupt + "\n")
                    interrupted = True
            elif line.startswith(b"700-"):
                marks.append(line[4:].decode("utf-8").strip())
                self.expect(b"700 INDEX MARK\n")
            elif line.startswith(b"706-"):
                self.icons.append(line[4:].decode("utf-8").strip())
                self.expect(b"706 ICON\n")
            elif line in (b"702 END\n", b"703 STOP\n", b"704 PAUSE\n"):
                return bytes(audio), metadata, marks, line.strip().decode("ascii")
            else:
                raise AssertionError(f"unexpected module response: {line!r}")

    def close(self):
        if self.process.poll() is None:
            self.send("QUIT\n")
            self.expect(b"210 OK QUIT\n")
        return self.process.wait(timeout=10)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: speechd.py <sd_openevv> <openevv.conf>")

    binary = os.path.abspath(sys.argv[1])
    config = os.path.abspath(sys.argv[2])
    module = ModuleProcess(binary, config)
    try:
        print("speechd: initialize", flush=True)
        module.send("INIT\n")
        module.expect(b"299-OpenEVV initialized\n")
        module.expect(b"299 OK LOADED SUCCESSFULLY\n")

        print("speechd: list voices", flush=True)
        voices = module.list_voices("en-US")
        assert len(voices) == 8
        assert any(voice[0] == "enus-elderly-male" for voice in voices)
        allVoices = module.list_voices()
        languages = {voice[1] for voice in allVoices}
        expectedLanguages = int(os.environ.get("OPENEVV_EXPECT_LANGUAGES", "1"))
        assert len(languages) == expectedLanguages, languages

        print("speechd: configure audio and settings", flush=True)
        module.command_with_data(
            "AUDIO",
            ["audio_output_method=server"],
            b"207 OK RECEIVING AUDIO SETTINGS\n",
        )
        module.expect(b"203 OK AUDIO INITIALIZED\n")
        module.set(
            rate="0",
            pitch="0",
            pitch_range="0",
            volume="0",
            punctuation_mode="none",
            spelling_mode="off",
            cap_let_recogn="none",
            voice="male1",
            language="en-US",
            synthesis_voice="NULL",
        )

        print("speechd: plain speech", flush=True)
        text = "OpenEVV’s quick brown fox jumps over the lazy dog."
        direct_text = "OpenEVV's quick brown fox jumps over the lazy dog."
        audio, metadata, marks, event = module.speak(text)
        assert event == "702 END"
        assert marks == []
        assert metadata["bits"] == 16
        assert metadata["num_channels"] == 1
        assert metadata["sample_rate"] == 11025
        assert audio and len(audio) % 2 == 0
        directBinary = os.environ.get(
            "OPENEVV_TEST_EVV",
            os.path.join(os.path.dirname(binary), "evv"),
        )
        direct = subprocess.check_output([
            directBinary,
            "-v", "1", "-o", "-", direct_text,
        ])
        assert direct[:4] == b"RIFF"
        assert audio == direct[44:], (
            "plain Speech Dispatcher text does not match normalized direct "
            "synthesis; Unicode punctuation or control annotations may have "
            f"been spoken incorrectly ({len(audio)} versus "
            f"{len(direct) - 44} PCM bytes)"
        )

        print("speechd: relative speech settings", flush=True)
        module.set(rate="100")
        fastAudio, _, _, event = module.speak(text)
        assert event == "702 END" and len(fastAudio) < len(audio)
        module.set(rate="0", pitch="60")
        pitchedAudio, _, _, event = module.speak(text)
        assert event == "702 END" and pitchedAudio != audio
        module.set(pitch="0", pitch_range="60")
        rangedAudio, _, _, event = module.speak(text)
        assert event == "702 END" and rangedAudio != audio
        module.set(pitch_range="0", volume="-60")
        quietAudio, _, _, event = module.speak(text)
        assert event == "702 END"
        normalPeak = max(abs(sample) for sample in struct.unpack(
            f"<{len(audio) // 2}h", audio
        ))
        quietPeak = max(abs(sample) for sample in struct.unpack(
            f"<{len(quietAudio) // 2}h", quietAudio
        ))
        assert 0 < quietPeak < normalPeak
        module.set(volume="0")

        print("speechd: punctuation settings", flush=True)
        for mode in ("none", "some", "most", "all"):
            module.set(punctuation_mode=mode)
            oneAudio, _, _, event = module.speak("left @ right [test]")
            assert event == "702 END" and oneAudio
        # Symbol expansion happens in the Speech Dispatcher server before it
        # sends text to this module, so direct protocol audio cannot prove the
        # audible difference between punctuation modes.
        module.set(punctuation_mode="none")
        plainNames, _, _, plainEvent = module.speak(
            "<speak>dash  and left paren </speak>"
        )
        retainedSymbols, _, _, retainedEvent = module.speak(
            "<speak>dash- and left paren(</speak>"
        )
        assert plainEvent == "702 END" and retainedEvent == "702 END"
        # Consecutive synthesis can vary slightly in trailing silence, so PCM
        # equality is too strict. Speaking the retained symbols nearly doubles
        # this sample; allow 25 percent timing variation around the named form.
        assert len(retainedSymbols) < len(plainNames) * 5 // 4, (
            "punctuation retained by Speech Dispatcher was spoken after its "
            f"inserted name ({len(retainedSymbols)} versus {len(plainNames)})"
        )
        print("speechd: available languages", flush=True)
        samples = {
            "de-DE": "Grüße aus Köln.",
            "en-GB": "The quick brown fox.",
            "en-US": "The quick brown fox.",
            "es-ES": "El pingüino está aquí.",
            "es-MX": "El pingüino está aquí.",
            "fr-CA": "L'été à Montréal.",
            "fr-FR": "L'été à Paris.",
            "it-IT": "Perché è così.",
        }
        for language in sorted(languages):
            print(f"speechd: language {language}", flush=True)
            module.set(language=language)
            audio, _, _, event = module.speak(samples[language])
            assert event == "702 END" and audio
        module.set(language="en-US")

        print("speechd: every advertised language and voice", flush=True)
        for name, language, _ in allVoices:
            module.set(synthesis_voice=name)
            oneAudio, _, _, event = module.speak(samples[language])
            assert event == "702 END" and oneAudio
        module.set(synthesis_voice="NULL")

        print("speechd: SSML mark", flush=True)
        audio, _, marks, event = module.speak(
            '<speak>Before <mark name="checkpoint"/> after.</speak>'
        )
        assert event == "702 END" and audio
        assert marks == ["checkpoint"]

        print("speechd: SSML text and entities", flush=True)
        audio, _, marks, event = module.speak(
            '<speak>One &amp; two<break time="100ms"/>three.</speak>'
        )
        assert event == "702 END" and audio and marks == []
        audio, _, marks, event = module.speak(
            '<speak>A <market name="wrong">tag</market> and surname="wrong".</speak>'
        )
        assert event == "702 END" and audio and marks == []

        print("speechd: exact voice", flush=True)
        module.set(synthesis_voice="enus-male1")
        maleAudio, _, _, event = module.speak(text)
        assert event == "702 END" and maleAudio
        module.set(synthesis_voice="enus-elderly-male")
        audio, _, _, event = module.speak(text)
        assert event == "702 END" and audio
        assert audio != maleAudio

        print("speechd: generic voice types", flush=True)
        module.set(synthesis_voice="NULL")
        for voice in (
            "male1", "male2", "male3", "female1", "female2", "female3",
            "child_male", "child_female",
        ):
            module.set(voice=voice)
            oneAudio, _, _, event = module.speak(text)
            assert event == "702 END" and oneAudio
        module.set(voice="male1")

        print("speechd: spelling", flush=True)
        module.set(synthesis_voice="NULL", spelling_mode="on")
        audio, _, _, event = module.speak("abc")
        assert event == "702 END" and audio
        module.set(spelling_mode="off")

        print("speechd: key", flush=True)
        audio, _, _, event = module.speak("KP_Enter", command="KEY")
        assert event == "702 END" and audio

        print("speechd: character and capitals", flush=True)
        module.set(cap_let_recogn="spell")
        upperAudio, _, _, event = module.speak("A", command="CHAR")
        assert event == "702 END" and upperAudio
        spelledText, _, _, event = module.speak("A test of McDonald and USA.")
        assert event == "702 END" and spelledText and module.icons == []
        module.set(cap_let_recogn="icon")
        iconText, _, _, event = module.speak("A École.")
        assert event == "702 END" and iconText
        assert module.icons == ["capital", "capital"]
        assert iconText != spelledText
        module.set(cap_let_recogn="none")
        plainAudio, _, _, event = module.speak("A", command="CHAR")
        assert event == "702 END" and plainAudio != upperAudio
        punctuationAudio, _, _, event = module.speak("-", command="CHAR")
        assert event == "702 END" and punctuationAudio

        print("speechd: sound icon fallback", flush=True)
        audio, _, _, event = module.speak("dialog-warning", command="SOUND_ICON")
        assert event == "702 END" and audio
        assert module.icons == ["dialog-warning"]

        print("speechd: stop", flush=True)
        long_text = " ".join([text] * 80)
        audio, _, _, event = module.speak(long_text, interrupt="STOP")
        assert event == "703 STOP" and audio

        print("speechd: pause", flush=True)
        pauseText = (
            " ".join([text] * 20)
            + '<mark name="__spd_1"/>'
            + " ".join([text] * 20)
        )
        audio, _, marks, event = module.speak(pauseText, interrupt="PAUSE")
        assert event == "704 PAUSE" and audio
        assert marks == ["__spd_1"]

        print("speechd: recovery", flush=True)
        audio, _, _, event = module.speak("After cancellation.")
        assert event == "702 END" and audio
    finally:
        result = module.close()
    assert result == 0
    print("speechd: protocol, voices, settings, audio, marks, stop and pause passed")


if __name__ == "__main__":
    main()

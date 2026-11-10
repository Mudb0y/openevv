#include "text.h"

#include <cstdio>

namespace openevv {

std::string Encode(const wchar_t* text, size_t length, UINT codePage) {
	if (!text || length == 0)
		return std::string();

	// Composed, where Windows can. A string it cannot normalise -- an
	// unpaired surrogate, say -- goes on as it came.
	std::wstring composed(text, length);
	const int room = NormalizeString(NormalizationC, text,
	                                 static_cast<int>(length), nullptr, 0);
	if (room > 0) {
		std::wstring out(static_cast<size_t>(room), L'\0');
		const int made = NormalizeString(NormalizationC, text,
		                                 static_cast<int>(length), &out[0], room);
		if (made > 0) {
			out.resize(static_cast<size_t>(made));
			composed.swap(out);
		}
	}

	const int need =
	    WideCharToMultiByte(codePage, 0, composed.data(),
	                        static_cast<int>(composed.size()), nullptr, 0,
	                        nullptr, nullptr);
	if (need <= 0)
		return std::string();
	std::string bytes(static_cast<size_t>(need), '\0');
	WideCharToMultiByte(codePage, 0, composed.data(),
	                    static_cast<int>(composed.size()), &bytes[0], need,
	                    nullptr, nullptr);
	return bytes;
}

std::wstring Decode(const std::string& bytes, UINT codePage) {
	if (bytes.empty())
		return std::wstring();
	const int need = MultiByteToWideChar(codePage, 0, bytes.data(),
	                                     static_cast<int>(bytes.size()),
	                                     nullptr, 0);
	if (need <= 0)
		return std::wstring();
	std::wstring text(static_cast<size_t>(need), L'\0');
	MultiByteToWideChar(codePage, 0, bytes.data(),
	                    static_cast<int>(bytes.size()), &text[0], need);
	return text;
}

std::string ProcessText(const wchar_t* text, size_t length, UINT codePage,
                        const Settings& settings) {
	if (settings.voiceTags || !text)
		return Encode(text, length, codePage);
	// Before encoding rather than after: in Shift-JIS a backtick's byte is
	// also the second half of some characters.
	std::wstring plain(text, length);
	for (wchar_t& c : plain)
		if (c == L'`' || c == L'\xff40')
			c = L' ';
	return Encode(plain.data(), plain.size(), codePage);
}

std::string Preamble(const Settings& settings, int pitch, int speed,
                     int volume) {
	char buffer[96];
	// `ts0 puts spelling back off; `vb `vs `vv put the voice where SAPI
	// asked for it. Each is written where it takes effect, which is here,
	// before any of the text.
	std::snprintf(buffer, sizeof buffer, "`pp%d `ts0 `vb%d `vs%d `vv%d ",
	              settings.phrasePrediction ? 1 : 0, pitch, speed, volume);
	return std::string(buffer);
}

}  // namespace openevv

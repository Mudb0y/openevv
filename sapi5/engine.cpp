/* Loading the library and speaking through it.
 *
 * Every name is asked for at run time, and every pointer it goes into has
 * the type include/eci.h gives the call. That is the point of using the
 * header rather than writing the shapes out again here: on thirty-two bits
 * a call declared with the wrong arguments leaves the stack out by the
 * difference, and on sixty-four a handle declared as an int is a pointer cut
 * in half.
 */

#include "engine.h"

#include <type_traits>

namespace openevv {
namespace {

struct Library {
	HMODULE module = nullptr;
	decltype(&eciNewEx) NewEx = nullptr;
	decltype(&eciDelete) Delete = nullptr;
	decltype(&eciVersion) Version = nullptr;
	decltype(&eciGetAvailableLanguages) GetAvailableLanguages = nullptr;
	decltype(&eciGetParam) GetParam = nullptr;
	decltype(&eciSetParam) SetParam = nullptr;
	decltype(&eciGetVoiceParam) GetVoiceParam = nullptr;
	decltype(&eciCopyVoice) CopyVoice = nullptr;
	decltype(&eciGetVoiceName) GetVoiceName = nullptr;
	decltype(&eciAddText) AddText = nullptr;
	decltype(&eciInsertIndex) InsertIndex = nullptr;
	decltype(&eciSynthesize) Synthesize = nullptr;
	decltype(&eciSynchronize) Synchronize = nullptr;
	decltype(&eciSetOutputBuffer) SetOutputBuffer = nullptr;
	decltype(&eciRegisterCallback) RegisterCallback = nullptr;
};

// The folder this module is in, with the separator on the end.
std::wstring ModuleFolder() {
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCWSTR>(&ModuleFolder), &self);
	wchar_t path[MAX_PATH] = {0};
	const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();
	std::wstring folder(path, length);
	const size_t slash = folder.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return std::wstring();
	folder.resize(slash + 1);
	return folder;
}

std::wstring EnginePath() {
	const std::wstring folder = ModuleFolder();
	if (folder.empty())
		return std::wstring();
	return folder +
	       (sizeof(void*) == 8 ? L"eci-x86_64\\eci.dll" : L"eci-x86\\eci.dll");
}

Library& Get() {
	static Library lib;
	static bool tried = false;
	if (tried)
		return lib;
	tried = true;

	const std::wstring path = EnginePath();
	if (path.empty())
		return lib;
	// The library's own folder first for anything it loads in turn, rather
	// than the folder of whichever program loaded the voice.
	lib.module = LoadLibraryExW(path.c_str(), nullptr,
	                            LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!lib.module)
		return lib;

	// A library that lacks any of these is not one the voice can drive, and
	// is treated as no library rather than discovered half way through an
	// utterance.
	bool complete = true;
	auto bind = [&](auto& field, const char* name) {
		field = reinterpret_cast<std::remove_reference_t<decltype(field)>>(
		    reinterpret_cast<void*>(GetProcAddress(lib.module, name)));
		if (!field)
			complete = false;
	};
	bind(lib.NewEx, "eciNewEx");
	bind(lib.Delete, "eciDelete");
	bind(lib.Version, "eciVersion");
	bind(lib.GetAvailableLanguages, "eciGetAvailableLanguages");
	bind(lib.GetParam, "eciGetParam");
	bind(lib.SetParam, "eciSetParam");
	bind(lib.GetVoiceParam, "eciGetVoiceParam");
	bind(lib.CopyVoice, "eciCopyVoice");
	bind(lib.GetVoiceName, "eciGetVoiceName");
	bind(lib.AddText, "eciAddText");
	bind(lib.InsertIndex, "eciInsertIndex");
	bind(lib.Synthesize, "eciSynthesize");
	bind(lib.Synchronize, "eciSynchronize");
	bind(lib.SetOutputBuffer, "eciSetOutputBuffer");
	bind(lib.RegisterCallback, "eciRegisterCallback");
	if (!complete) {
		FreeLibrary(lib.module);
		lib = Library();
	}
	return lib;
}

}  // namespace

Engine::Engine() : buffer_(kBufferSamples) {}

Engine::~Engine() {
	Close();
}

bool Engine::Available() {
	return Get().module != nullptr;
}

std::wstring Engine::LibraryPath() {
	return EnginePath();
}

std::string Engine::Version() {
	Library& lib = Get();
	if (!lib.module)
		return std::string();
	char buffer[64] = {0};
	lib.Version(buffer);
	return std::string(buffer);
}

std::vector<int> Engine::Languages() {
	Library& lib = Get();
	if (!lib.module)
		return {};
	// Asked once with no room for the count, then again with room.
	int count = 0;
	lib.GetAvailableLanguages(nullptr, &count);
	if (count <= 0)
		return {};
	std::vector<unsigned int> found(static_cast<size_t>(count));
	lib.GetAvailableLanguages(found.data(), &count);
	if (count <= 0 || count > static_cast<int>(found.size()))
		return {};
	found.resize(static_cast<size_t>(count));
	return std::vector<int>(found.begin(), found.end());
}

bool Engine::Open(int language) {
	if (handle_)
		return true;
	Library& lib = Get();
	if (!lib.module)
		return false;

	handle_ = lib.NewEx(language);
	if (!handle_)
		return false;

	lib.RegisterCallback(handle_, &Engine::Trampoline, this);
	if (!lib.SetOutputBuffer(handle_, kBufferSamples, buffer_.data())) {
		Close();
		return false;
	}
	// Annotations on: the voice says what SAPI asks for -- rate, pitch,
	// volume, spelling, silence -- by writing them into the text.
	lib.SetParam(handle_, eciInputType, 1);
	return true;
}

void Engine::Close() {
	if (handle_) {
		Get().Delete(handle_);
		handle_ = nullptr;
	}
}

int Engine::GetParam(int which) const {
	return handle_ ? Get().GetParam(handle_, which) : -1;
}

void Engine::SetParam(int which, int value) {
	if (handle_)
		Get().SetParam(handle_, which, value);
}

int Engine::GetVoiceParam(int voice, int which) const {
	return handle_ ? Get().GetVoiceParam(handle_, voice, which) : -1;
}

bool Engine::LoadPreset(int preset) {
	// Voice nought is the one that gets spoken. The presets are copied into
	// it rather than chosen.
	return handle_ && Get().CopyVoice(handle_, preset, 0) != 0;
}

std::string Engine::PresetName(int preset) const {
	if (!handle_)
		return std::string();
	// Room for the thirty bytes a name may be and then some.
	char name[64] = {0};
	if (!Get().GetVoiceName(handle_, preset, name))
		return std::string();
	return std::string(name);
}

void Engine::AddText(const std::string& text) {
	if (handle_ && !text.empty())
		Get().AddText(handle_, text.c_str());
}

void Engine::InsertMark(int mark) {
	if (handle_)
		Get().InsertIndex(handle_, mark);
}

bool Engine::Speak(Sink* sink) {
	if (!handle_)
		return false;
	Library& lib = Get();

	sink_ = sink;
	abandoned_ = false;
	InsertMark(kEndOfUtterance);
	lib.Synthesize(handle_);
	// The callbacks happen in here, and it returns when the engine has
	// finished. An utterance SAPI gave up on is run out with nothing written.
	lib.Synchronize(handle_);
	sink_ = nullptr;
	return !abandoned_;
}

int ECICALL Engine::Trampoline(ECIHand, ECIMessage message, int param,
                               void* data) {
	Engine* self = static_cast<Engine*>(data);
	return self ? self->OnMessage(message, param) : eciDataProcessed;
}

int Engine::OnMessage(ECIMessage message, int param) {
	if (!sink_ || abandoned_)
		return eciDataProcessed;
	switch (message) {
		case eciWaveformBuffer:
			if (param > 0 && !sink_->OnAudio(buffer_.data(), param))
				abandoned_ = true;
			break;
		case eciIndexReply:
			if (param != kEndOfUtterance)
				sink_->OnMark(param);
			break;
		default:
			break;
	}
	return eciDataProcessed;
}

}  // namespace openevv

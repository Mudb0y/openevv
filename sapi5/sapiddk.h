/* The part of SAPI 5 a voice is written against and mingw does not ship.
 *
 * mingw-w64 has sapi.h, which is what a program that speaks wants, and not
 * sapiddk.h, which is what a voice wants: the engine interface SAPI calls,
 * the site it calls back through, and the fragment of text it hands over.
 * They are declared here from Microsoft's published IDL, in the same order
 * and with the same identities, because the vtable is the interface -- a
 * method declared out of order is a call to the wrong function, and nothing
 * at compile time can say so.
 *
 * Only what this voice and its test use. ISpTokenUI is here too: it is in
 * Microsoft's sapi.h and not in mingw's.
 */

#pragma once

#include <windows.h>

#include <sapi.h>

/* What a site asks of the engine between writes. */
typedef enum SPVESACTIONS {
	SPVES_CONTINUE = 0,
	SPVES_ABORT = 1L << 0,
	SPVES_SKIP = 1L << 1,
	SPVES_RATE = 1L << 2,
	SPVES_VOLUME = 1L << 3
} SPVESACTIONS;

typedef enum SPVSKIPTYPE {
	SPVST_SENTENCE = 1L << 0
} SPVSKIPTYPE;

/* One piece of what is to be said, and how. A list, in the order it is to
   be spoken. */
typedef struct SPVTEXTFRAG {
	struct SPVTEXTFRAG* pNext;
	SPVSTATE State;
	LPCWSTR pTextStart;
	ULONG ulTextLen;
	ULONG ulTextSrcOffset;
} SPVTEXTFRAG;

/* The two reserved bits sapi.h sets in every interest mask, and the mask a
   site interested in everything a voice says answers with. */
#ifndef SPFEI_ALL_TTS_EVENTS
#define SPFEI_FLAGCHECK \
	((1ULL << SPEI_RESERVED1) | (1ULL << SPEI_RESERVED2))
#define SPFEI_ALL_TTS_EVENTS (0x000000000000FFFEULL | SPFEI_FLAGCHECK)
#endif

/* {9880499B-CCE9-11D2-B503-00C04F797396} */
struct ISpTTSEngineSite : public ISpEventSink {
	virtual DWORD STDMETHODCALLTYPE GetActions() = 0;
	virtual HRESULT STDMETHODCALLTYPE Write(const void* buffer, ULONG bytes,
	                                        ULONG* written) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetRate(long* rateAdjust) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVolume(USHORT* volume) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE* type,
	                                              long* items) = 0;
	virtual HRESULT STDMETHODCALLTYPE CompleteSkip(long skipped) = 0;
};
__CRT_UUID_DECL(ISpTTSEngineSite, 0x9880499b, 0xcce9, 0x11d2, 0xb5, 0x03,
                0x00, 0xc0, 0x4f, 0x79, 0x73, 0x96)

/* {A74D7C8E-4CC5-4F2F-A6EB-804DEE18500E} */
struct ISpTTSEngine : public IUnknown {
	virtual HRESULT STDMETHODCALLTYPE Speak(DWORD flags, REFGUID formatId,
	                                        const WAVEFORMATEX* waveFormat,
	                                        const SPVTEXTFRAG* fragments,
	                                        ISpTTSEngineSite* site) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetOutputFormat(
	    const GUID* targetFormatId, const WAVEFORMATEX* targetWaveFormat,
	    GUID* outputFormatId, WAVEFORMATEX** outputWaveFormat) = 0;
};
__CRT_UUID_DECL(ISpTTSEngine, 0xa74d7c8e, 0x4cc5, 0x4f2f, 0xa6, 0xeb, 0x80,
                0x4d, 0xee, 0x18, 0x50, 0x0e)

/* {F8E690F0-39CB-4843-B8D7-C84696E1119D} */
struct ISpTokenUI : public IUnknown {
	virtual HRESULT STDMETHODCALLTYPE IsUISupported(LPCWSTR type, void* extra,
	                                                ULONG extraBytes,
	                                                IUnknown* object,
	                                                BOOL* supported) = 0;
	virtual HRESULT STDMETHODCALLTYPE DisplayUI(HWND parent, LPCWSTR title,
	                                            LPCWSTR type, void* extra,
	                                            ULONG extraBytes,
	                                            ISpObjectToken* token,
	                                            IUnknown* object) = 0;
};
__CRT_UUID_DECL(ISpTokenUI, 0xf8e690f0, 0x39cb, 0x4843, 0xb8, 0xd7, 0xc8,
                0x46, 0x96, 0xe1, 0x11, 0x9d)

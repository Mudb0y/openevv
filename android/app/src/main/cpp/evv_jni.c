/* The bridge between the ECI engine and Android's synthesis callback.
 *
 * The engine hands samples over on a thread of its own and blocks in the
 * callback until the caller has taken them, so the shape here is a ring buffer
 * with a lock either side of it: the engine's thread fills it and Android's
 * synthesis thread drains it. That blocking is the point rather than a cost.
 * It paces the engine to whatever the audio track is taking, so a long
 * utterance does not have to be held in memory before any of it is heard.
 *
 * No JNI call is ever made from the engine's thread, which is why there is no
 * thread to attach and none to detach.
 */

#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>

#include "eci.h"

#define TAG "evvdroid"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* What the engine writes at a time, in samples, and what the ring holds, in
   bytes. The ring has to be bigger than one frame or the callback would wait
   for room that can never arrive. */
#define FRAME_SAMPLES 1024
#define RING_BYTES    (128 * 1024)

/* How many milliseconds the end-of-utterance wait will spend watching for the
   engine to take the work up before it stops waiting for that. */
#define START_SPINS 200

typedef struct {
	ECIHand         eci;
	pthread_mutex_t lock;
	pthread_cond_t  room;
	pthread_cond_t  filled;
	unsigned char*  ring;
	size_t          head;
	size_t          tail;
	size_t          count;
	int             aborted;
	int             started;
	int             done;
	pthread_t       waiter;
	int             waiting;
	unsigned char*  text;
	ECIDictHand     dict;
	short           frame[FRAME_SAMPLES];
} instance;

static instance* unwrap(jlong handle) {
	return (instance*)(intptr_t)handle;
}

static void put(instance* in, const unsigned char* src, size_t n) {
	size_t first = RING_BYTES - in->head;
	if (first > n)
		first = n;
	memcpy(in->ring + in->head, src, first);
	memcpy(in->ring, src + first, n - first);
	in->head = (in->head + n) % RING_BYTES;
	in->count += n;
}

static void take(instance* in, unsigned char* dst, size_t n) {
	size_t first = RING_BYTES - in->tail;
	if (first > n)
		first = n;
	memcpy(dst, in->ring + in->tail, first);
	memcpy(dst + first, in->ring, n - first);
	in->tail = (in->tail + n) % RING_BYTES;
	in->count -= n;
}

/* What says an utterance is over, which took three goes to get right.
 *
 * eciSpeaking cannot be it. It answers whether anything is outstanding right
 * now, and a reader that drains the ring faster than the engine fills it sees
 * nought between two of the engine's own messages and calls an utterance
 * finished part way through.
 *
 * eciSynchronize alone cannot be it either. eciSynthesize only queues the work
 * and answers; asked before the synthesis thread has taken the utterance up,
 * eciSynchronize finds nothing outstanding and answers at once, and then the
 * utterance is over before it began. That is not a rare window -- it cost a
 * whole utterance about one time in four.
 *
 * So the wait is in two parts: first until the engine has visibly taken the
 * work, then eciSynchronize, which is exact once there is something for it to
 * wait on. Having taken it shows either as a buffer already delivered or as
 * eciSpeaking answering yes. Text that yields no audio at all shows as
 * neither, which is what the spin limit is for; it is the only case that waits
 * it out, and it waits a fifth of a second rather than for ever.
 *
 * It has to be its own thread because the thread that reads cannot block. */
static void* wait_for_end(void* data) {
	instance* in = (instance*)data;
	struct timespec tick = { 0, 1000L * 1000L };
	int spins;
	for (spins = 0; spins < START_SPINS; spins++) {
		int begun;
		pthread_mutex_lock(&in->lock);
		begun = in->started || in->aborted;
		pthread_mutex_unlock(&in->lock);
		if (begun || eciSpeaking(in->eci))
			break;
		nanosleep(&tick, NULL);
	}
	eciSynchronize(in->eci);
	pthread_mutex_lock(&in->lock);
	in->done = 1;
	pthread_cond_broadcast(&in->filled);
	pthread_mutex_unlock(&in->lock);
	return NULL;
}

/* Marks an utterance over that never started, so that a reader waiting on the
   end condition is not waiting for a thread nothing made. */
static jboolean finished(instance* in) {
	pthread_mutex_lock(&in->lock);
	in->done = 1;
	pthread_cond_broadcast(&in->filled);
	pthread_mutex_unlock(&in->lock);
	return JNI_FALSE;
}

static void join_waiter(instance* in) {
	if (in->waiting) {
		pthread_join(in->waiter, NULL);
		in->waiting = 0;
	}
}

/* Runs on the engine's synthesis thread. It may not call back into the same
   instance, and it does not: everything it touches is ours. */
static int ECICALL on_message(ECIHand handle, ECIMessage message, int param, void* data) {
	instance* in = (instance*)data;
	size_t want;
	(void)handle;
	if (message != eciWaveformBuffer)
		return eciDataProcessed;
	want = (size_t)param * sizeof(short);
	if (want == 0)
		return eciDataProcessed;
	pthread_mutex_lock(&in->lock);
	while (!in->aborted && RING_BYTES - in->count < want)
		pthread_cond_wait(&in->room, &in->lock);
	if (in->aborted) {
		pthread_mutex_unlock(&in->lock);
		return eciDataAbort;
	}
	put(in, (const unsigned char*)in->frame, want);
	in->started = 1;
	pthread_cond_signal(&in->filled);
	pthread_mutex_unlock(&in->lock);
	return eciDataProcessed;
}

JNIEXPORT jlong JNICALL Java_org_evvdroid_EvvNative_create(JNIEnv* env, jclass cls, jint language) {
	instance* in;
	(void)env;
	(void)cls;
	in = (instance*)calloc(1, sizeof *in);
	if (in == NULL)
		return 0;
	in->ring = (unsigned char*)malloc(RING_BYTES);
	if (in->ring == NULL) {
		free(in);
		return 0;
	}
	in->eci = eciNewEx(language);
	if (in->eci == NULL_ECI_HAND) {
		LOGE("eciNewEx(%#x) gave no instance", language);
		free(in->ring);
		free(in);
		return 0;
	}
	pthread_mutex_init(&in->lock, NULL);
	pthread_cond_init(&in->room, NULL);
	pthread_cond_init(&in->filled, NULL);
	eciRegisterCallback(in->eci, on_message, in);
	if (!eciSetOutputBuffer(in->eci, FRAME_SAMPLES, in->frame)) {
		LOGE("the engine refused the output buffer");
		eciDelete(in->eci);
		free(in->ring);
		free(in);
		return 0;
	}
	return (jlong)(intptr_t)in;
}

JNIEXPORT void JNICALL Java_org_evvdroid_EvvNative_destroy(JNIEnv* env, jclass cls, jlong handle) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	if (in == NULL)
		return;
	pthread_mutex_lock(&in->lock);
	in->aborted = 1;
	pthread_cond_broadcast(&in->room);
	pthread_cond_broadcast(&in->filled);
	pthread_mutex_unlock(&in->lock);
	eciStop(in->eci);
	join_waiter(in);
	if (in->dict != NULL_DICT_HAND)
		eciDeleteDict(in->eci, in->dict);
	eciDelete(in->eci);
	pthread_cond_destroy(&in->room);
	pthread_cond_destroy(&in->filled);
	pthread_mutex_destroy(&in->lock);
	free(in->text);
	free(in->ring);
	free(in);
}

/* Hands the text over and starts the engine. It answers at once; the samples
   come out of read below.
 *
 * The text is bytes in the language's own code set rather than UTF-16, which
 * the engine allows only for Chinese, Japanese and Korean; Kotlin has already
 * turned it into that set. It is kept until the next utterance because nothing
 * here knows whether the engine copied it. */
JNIEXPORT jboolean JNICALL Java_org_evvdroid_EvvNative_speak(JNIEnv* env, jclass cls, jlong handle, jbyteArray text) {
	instance*      in = unwrap(handle);
	jsize          n;
	unsigned char* buf;
	(void)cls;
	if (in == NULL || text == NULL)
		return JNI_FALSE;
	n = (*env)->GetArrayLength(env, text);
	buf = (unsigned char*)malloc((size_t)n + 1);
	if (buf == NULL)
		return JNI_FALSE;
	(*env)->GetByteArrayRegion(env, text, 0, n, (jbyte*)buf);
	buf[n] = 0;
	join_waiter(in);
	pthread_mutex_lock(&in->lock);
	in->aborted = 0;
	in->started = 0;
	in->done = 0;
	in->head = in->tail = in->count = 0;
	pthread_mutex_unlock(&in->lock);
	free(in->text);
	in->text = buf;
	if (!eciAddText(in->eci, buf)) {
		LOGE("eciAddText refused the text");
		return finished(in);
	}
	if (!eciSynthesize(in->eci)) {
		LOGE("eciSynthesize refused");
		return finished(in);
	}
	if (pthread_create(&in->waiter, NULL, wait_for_end, in) != 0) {
		LOGE("no thread to wait on the utterance");
		eciStop(in->eci);
		return finished(in);
	}
	in->waiting = 1;
	return JNI_TRUE;
}

/* Bytes out, blocking until there are some. Answers 0 at the end of the
   utterance and -1 when it was stopped. */
JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_read(JNIEnv* env, jclass cls, jlong handle, jbyteArray dst) {
	instance*      in = unwrap(handle);
	size_t         room;
	size_t         got;
	unsigned char* scratch;
	(void)cls;
	if (in == NULL || dst == NULL)
		return -1;
	room = (size_t)(*env)->GetArrayLength(env, dst);
	if (room == 0)
		return -1;
	pthread_mutex_lock(&in->lock);
	while (in->count == 0 && !in->done && !in->aborted)
		pthread_cond_wait(&in->filled, &in->lock);
	if (in->aborted) {
		pthread_mutex_unlock(&in->lock);
		return -1;
	}
	if (in->count == 0) {
		pthread_mutex_unlock(&in->lock);
		return 0;
	}
	got = in->count < room ? in->count : room;
	scratch = (unsigned char*)malloc(got);
	if (scratch == NULL) {
		pthread_mutex_unlock(&in->lock);
		return -1;
	}
	take(in, scratch, got);
	pthread_cond_signal(&in->room);
	pthread_mutex_unlock(&in->lock);
	(*env)->SetByteArrayRegion(env, dst, 0, (jsize)got, (const jbyte*)scratch);
	free(scratch);
	return (jint)got;
}

/* Wakes the callback before asking the engine to stop, since the callback may
   be waiting for room that a stopped reader will never make. */
JNIEXPORT void JNICALL Java_org_evvdroid_EvvNative_stop(JNIEnv* env, jclass cls, jlong handle) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	if (in == NULL)
		return;
	pthread_mutex_lock(&in->lock);
	in->aborted = 1;
	in->head = in->tail = in->count = 0;
	pthread_cond_broadcast(&in->room);
	pthread_cond_broadcast(&in->filled);
	pthread_mutex_unlock(&in->lock);
	eciStop(in->eci);
}

/* The instance's own dictionary set, made the first time one is wanted. An
   instance has one set in force at a time and the three volumes live in it. */
static int ensure_dict(instance* in) {
	if (in->dict != NULL_DICT_HAND)
		return 1;
	in->dict = eciNewDict(in->eci);
	if (in->dict == NULL_DICT_HAND) {
		LOGE("the engine would not make a dictionary set");
		return 0;
	}
	/* An error code rather than a flag, so nought is success. */
	if (eciSetDict(in->eci, in->dict) != 0) {
		LOGE("the engine would not take the dictionary set");
		return 0;
	}
	return 1;
}

/* Hands a file to the engine's own dictionary loader. Answers one of the
   eciDict codes, so nought is success. */
JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_loadDictionary(JNIEnv* env, jclass cls, jlong handle, jint volume, jstring path) {
	instance*   in = unwrap(handle);
	const char* name;
	jint        answer;
	(void)cls;
	if (in == NULL || path == NULL || !ensure_dict(in))
		return -1;
	name = (*env)->GetStringUTFChars(env, path, NULL);
	if (name == NULL)
		return -1;
	answer = eciLoadDict(in->eci, in->dict, volume, name);
	(*env)->ReleaseStringUTFChars(env, path, name);
	return answer;
}

/* Teaches one word, which is the other way in and the one that does not depend
   on the engine agreeing about a file format. */
JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_teachWord(JNIEnv* env, jclass cls, jlong handle, jint volume, jbyteArray key, jbyteArray say) {
	instance*      in = unwrap(handle);
	jsize          keyLen;
	jsize          sayLen;
	unsigned char* pair;
	jint           answer;
	(void)cls;
	if (in == NULL || key == NULL || say == NULL || !ensure_dict(in))
		return -1;
	keyLen = (*env)->GetArrayLength(env, key);
	sayLen = (*env)->GetArrayLength(env, say);
	pair = (unsigned char*)malloc((size_t)keyLen + (size_t)sayLen + 2);
	if (pair == NULL)
		return -1;
	(*env)->GetByteArrayRegion(env, key, 0, keyLen, (jbyte*)pair);
	pair[keyLen] = 0;
	(*env)->GetByteArrayRegion(env, say, 0, sayLen, (jbyte*)(pair + keyLen + 1));
	pair[keyLen + 1 + sayLen] = 0;
	answer = eciUpdateDict(in->eci, in->dict, volume, pair, pair + keyLen + 1);
	free(pair);
	return answer;
}

/* What a key was taught, or nothing. This is how a test says a dictionary
   really went in rather than that a call answered nought. */
JNIEXPORT jstring JNICALL Java_org_evvdroid_EvvNative_lookUpWord(JNIEnv* env, jclass cls, jlong handle, jint volume, jbyteArray key) {
	instance*      in = unwrap(handle);
	jsize          keyLen;
	unsigned char* want;
	const char*    found;
	jstring        answer;
	(void)cls;
	if (in == NULL || key == NULL || in->dict == NULL_DICT_HAND)
		return NULL;
	keyLen = (*env)->GetArrayLength(env, key);
	want = (unsigned char*)malloc((size_t)keyLen + 1);
	if (want == NULL)
		return NULL;
	(*env)->GetByteArrayRegion(env, key, 0, keyLen, (jbyte*)want);
	want[keyLen] = 0;
	found = eciDictLookup(in->eci, in->dict, volume, want);
	answer = found == NULL ? NULL : (*env)->NewStringUTF(env, found);
	free(want);
	return answer;
}

/* Puts the set aside and takes it away, so the engine is back to the language's
   own dictionary. */
JNIEXPORT void JNICALL Java_org_evvdroid_EvvNative_forgetDictionaries(JNIEnv* env, jclass cls, jlong handle) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	if (in == NULL || in->dict == NULL_DICT_HAND)
		return;
	eciSetDict(in->eci, NULL_DICT_HAND);
	eciDeleteDict(in->eci, in->dict);
	in->dict = NULL_DICT_HAND;
}

JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_setParam(JNIEnv* env, jclass cls, jlong handle, jint param, jint value) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	return in == NULL ? -1 : eciSetParam(in->eci, param, value);
}

JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_getParam(JNIEnv* env, jclass cls, jlong handle, jint param) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	return in == NULL ? -1 : eciGetParam(in->eci, param);
}

JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_setVoiceParam(JNIEnv* env, jclass cls, jlong handle, jint voice, jint param, jint value) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	return in == NULL ? -1 : eciSetVoiceParam(in->eci, voice, param, value);
}

JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_getVoiceParam(JNIEnv* env, jclass cls, jlong handle, jint voice, jint param) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	return in == NULL ? -1 : eciGetVoiceParam(in->eci, voice, param);
}

JNIEXPORT jint JNICALL Java_org_evvdroid_EvvNative_copyVoice(JNIEnv* env, jclass cls, jlong handle, jint from, jint to) {
	instance* in = unwrap(handle);
	(void)env;
	(void)cls;
	return in == NULL ? -1 : eciCopyVoice(in->eci, from, to);
}

JNIEXPORT jintArray JNICALL Java_org_evvdroid_EvvNative_languages(JNIEnv* env, jclass cls) {
	unsigned int found[32];
	int          n = 0;
	jintArray    out;
	(void)cls;
	if (eciGetAvailableLanguages(NULL, &n) != 0 || n < 1)
		return (*env)->NewIntArray(env, 0);
	if (n > 32)
		n = 32;
	if (eciGetAvailableLanguages(found, &n) != 0)
		return (*env)->NewIntArray(env, 0);
	out = (*env)->NewIntArray(env, n);
	if (out == NULL)
		return NULL;
	(*env)->SetIntArrayRegion(env, out, 0, n, (const jint*)found);
	return out;
}

JNIEXPORT jstring JNICALL Java_org_evvdroid_EvvNative_version(JNIEnv* env, jclass cls) {
	char buffer[ECI_VERSION_LENGTH];
	(void)cls;
	memset(buffer, 0, sizeof buffer);
	eciVersion(buffer);
	buffer[sizeof buffer - 1] = 0;
	return (*env)->NewStringUTF(env, buffer);
}

/* Copyright (c) 2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <jni.h>
#include <android/log.h>

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/sync.h>
#include <mgba/core/thread.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/image.h>
#include <mgba-util/vfs.h>

#include <stdatomic.h>

#define ANDROID_LOG_TAG "mGBA"

struct AndroidCore {
	struct mCore* core;
	struct mCoreThread thread;
	mColor* video;
	unsigned width;
	unsigned height;
	atomic_uint keys;
	bool configInitialized;
	bool started;
};

static void _androidLog(const char* message) {
	__android_log_write(ANDROID_LOG_ERROR, ANDROID_LOG_TAG, message);
}

static void _androidFrameCallback(struct mCoreThread* thread) {
	struct AndroidCore* context = thread->userData;
	if (!context || !context->core) {
		return;
	}

	/* 触摸屏和手柄线程只写入原子按键掩码，实际修改核心状态在模拟线程完成。 */
	uint32_t keys = atomic_load_explicit(&context->keys, memory_order_relaxed);
	context->core->setKeys(context->core, keys);
}

static void _androidRelease(struct AndroidCore* context) {
	if (!context) {
		return;
	}
	if (context->started) {
		mCoreThreadEnd(&context->thread);
		mCoreThreadJoin(&context->thread);
		context->started = false;
	}
	if (context->core) {
		if (context->configInitialized) {
			mCoreConfigDeinit(&context->core->config);
		}
		context->core->deinit(context->core);
		context->core = NULL;
	}
	free(context->video);
	free(context);
}

static struct AndroidCore* _androidCreate(const void* rom, size_t romSize) {
	if (!rom || !romSize) {
		return NULL;
	}

	struct VFile* probe = VFileFromConstMemory(rom, romSize);
	if (!probe) {
		return NULL;
	}
	struct mCore* core = mCoreFindVF(probe);
	probe->close(probe);
	if (!core || core->platform(core) != mPLATFORM_GBA) {
		if (core) {
			core->deinit(core);
		}
		return NULL;
	}

	struct AndroidCore* context = calloc(1, sizeof(*context));
	if (!context) {
		core->deinit(core);
		return NULL;
	}
	context->core = core;
	atomic_init(&context->keys, 0);

	if (!core->init(core)) {
		_androidRelease(context);
		return NULL;
	}

	struct mCoreOptions options = {
		/* Android 首发默认使用内置 HLE BIOS，避免在应用包中分发任天堂 BIOS。 */
		.useBios = false,
		.skipBios = true,
		.audioBuffers = 2048,
		.videoSync = true,
		.audioSync = true,
		.volume = 0x100,
		.logLevel = mLOG_WARN | mLOG_ERROR | mLOG_FATAL,
	};
	mCoreInitConfig(core, "android");
	context->configInitialized = true;
	mCoreConfigLoadDefaults(&core->config, &options);
	mCoreLoadForeignConfig(core, &core->config);

	core->baseVideoSize(core, &context->width, &context->height);
	context->video = calloc((size_t) context->width * context->height, sizeof(*context->video));
	if (!context->video) {
		_androidRelease(context);
		return NULL;
	}
	core->setVideoBuffer(core, context->video, context->width);

	struct VFile* file = VFileFromConstMemory(rom, romSize);
	if (!file || !mCorePreloadVF(core, file)) {
		/* mCorePreloadVF 会负责关闭传入的 VFile；这里不能重复关闭。 */
		_androidRelease(context);
		return NULL;
	}

	context->thread.core = core;
	context->thread.userData = context;
	context->thread.frameCallback = _androidFrameCallback;
	if (!mCoreThreadStart(&context->thread)) {
		_androidRelease(context);
		return NULL;
	}
	context->started = true;
	return context;
}

JNIEXPORT jlong JNICALL Java_io_mgba_android_NativeBridge_nativeCreate(JNIEnv* env, jclass klass, jbyteArray rom) {
	UNUSED(klass);
	if (!rom) {
		return 0;
	}
	jsize size = (*env)->GetArrayLength(env, rom);
	if (size <= 0) {
		return 0;
	}
	jbyte* bytes = (*env)->GetByteArrayElements(env, rom, NULL);
	if (!bytes) {
		return 0;
	}
	struct AndroidCore* context = _androidCreate(bytes, (size_t) size);
	(*env)->ReleaseByteArrayElements(env, rom, bytes, JNI_ABORT);
	if (!context) {
		_androidLog("无法创建 GBA 核心，请确认 ROM 格式正确");
	}
	return (jlong) (uintptr_t) context;
}

JNIEXPORT void JNICALL Java_io_mgba_android_NativeBridge_nativeDestroy(JNIEnv* env, jclass klass, jlong handle) {
	UNUSED(env);
	UNUSED(klass);
	_androidRelease((struct AndroidCore*) (uintptr_t) handle);
}

JNIEXPORT void JNICALL Java_io_mgba_android_NativeBridge_nativeStop(JNIEnv* env, jclass klass, jlong handle) {
	UNUSED(env);
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	if (context && context->started) {
		/* 先唤醒等待视频帧的 JNI 调用，再由 Java 层等待视频线程退出。 */
		mCoreThreadEnd(&context->thread);
	}
}

JNIEXPORT jint JNICALL Java_io_mgba_android_NativeBridge_nativeWidth(JNIEnv* env, jclass klass, jlong handle) {
	UNUSED(env);
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	return context ? (jint) context->width : 0;
}

JNIEXPORT jint JNICALL Java_io_mgba_android_NativeBridge_nativeHeight(JNIEnv* env, jclass klass, jlong handle) {
	UNUSED(env);
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	return context ? (jint) context->height : 0;
}

JNIEXPORT jboolean JNICALL Java_io_mgba_android_NativeBridge_nativeWaitFrame(JNIEnv* env, jclass klass, jlong handle, jintArray pixels) {
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	if (!context || !context->started || !pixels) {
		return JNI_FALSE;
	}
	jsize length = (*env)->GetArrayLength(env, pixels);
	if (length < (jsize) ((size_t) context->width * context->height)) {
		return JNI_FALSE;
	}

	bool ready = mCoreSyncWaitFrameStart(&context->thread.impl->sync);
	if (ready) {
		jint* out = (*env)->GetIntArrayElements(env, pixels, NULL);
		if (out) {
			for (size_t i = 0; i < (size_t) context->width * context->height; ++i) {
				/* mGBA 默认输出为 XBGR8；Android Bitmap 使用 ARGB_8888。 */
				uint32_t color = context->video[i];
				out[i] = (jint) (0xFF000000u | ((color & 0xFFu) << 16) | (color & 0x0000FF00u) | ((color >> 16) & 0xFFu));
			}
			(*env)->ReleaseIntArrayElements(env, pixels, out, 0);
		}
	}
	/* WaitFrameStart 持有同步锁；无论是否拿到新帧都必须释放它。 */
	mCoreSyncWaitFrameEnd(&context->thread.impl->sync);
	return ready ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_io_mgba_android_NativeBridge_nativeReadAudio(JNIEnv* env, jclass klass, jlong handle, jshortArray samples, jint frames) {
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	if (!context || !context->started || !samples || frames <= 0) {
		return 0;
	}
	jsize length = (*env)->GetArrayLength(env, samples);
	if (length < frames * 2) {
		return 0;
	}
	jshort* out = (*env)->GetShortArrayElements(env, samples, NULL);
	if (!out) {
		return 0;
	}
	mCoreSyncLockAudio(&context->thread.impl->sync);
	struct mAudioBuffer* audio = context->core->getAudioBuffer(context->core);
	size_t count = mAudioBufferRead(audio, (int16_t*) out, (size_t) frames);
	mCoreSyncConsumeAudio(&context->thread.impl->sync);
	(*env)->ReleaseShortArrayElements(env, samples, out, 0);
	return (jint) count;
}

JNIEXPORT void JNICALL Java_io_mgba_android_NativeBridge_nativeSetKeys(JNIEnv* env, jclass klass, jlong handle, jint keys) {
	UNUSED(env);
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	if (context) {
		atomic_store_explicit(&context->keys, (unsigned) keys, memory_order_relaxed);
	}
}

JNIEXPORT void JNICALL Java_io_mgba_android_NativeBridge_nativePause(JNIEnv* env, jclass klass, jlong handle) {
	UNUSED(env);
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	if (context && context->started && !mCoreThreadIsPaused(&context->thread)) {
		mCoreThreadPause(&context->thread);
	}
}

JNIEXPORT void JNICALL Java_io_mgba_android_NativeBridge_nativeResume(JNIEnv* env, jclass klass, jlong handle) {
	UNUSED(env);
	UNUSED(klass);
	struct AndroidCore* context = (struct AndroidCore*) (uintptr_t) handle;
	if (context && context->started && mCoreThreadIsPaused(&context->thread)) {
		mCoreThreadUnpause(&context->thread);
	}
}


/* Copyright (c) 2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package io.mgba.android;

final class NativeBridge {
    static {
        System.loadLibrary("mgba-android");
    }

    private NativeBridge() {
    }

    static native long nativeCreate(byte[] rom);
    static native void nativeStop(long handle);
    static native void nativeDestroy(long handle);
    static native int nativeWidth(long handle);
    static native int nativeHeight(long handle);
    static native boolean nativeWaitFrame(long handle, int[] pixels);
    static native int nativeReadAudio(long handle, short[] samples, int frames);
    static native void nativeSetKeys(long handle, int keys);
    static native void nativePause(long handle);
    static native void nativeResume(long handle);
}


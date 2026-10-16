/* Copyright (c) 2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package io.mgba.android;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.net.Uri;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

// 产品 Activity 可继承此适配层；这里不得依赖广告、账号或支付 SDK。
public class MainActivity extends Activity {
    private static final int REQUEST_ROM = 1001;

    private FrameLayout root;
    private GameView gameView;
    private long core;
    private AudioTrack audioTrack;
    private Thread audioThread;
    private volatile boolean running;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        showPicker();
    }

    private void showPicker() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(android.view.Gravity.CENTER);
        Button open = new Button(this);
        open.setText("选择 GBA ROM");
        open.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/octet-stream");
            startActivityForResult(intent, REQUEST_ROM);
        });
        panel.addView(open);
        setContentView(panel);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_ROM && resultCode == RESULT_OK && data != null && data.getData() != null) {
            try {
                byte[] rom = readUri(data.getData());
                startGame(rom);
            } catch (IOException | RuntimeException e) {
                Toast.makeText(this, "无法读取 ROM：" + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }
    }

    private byte[] readUri(Uri uri) throws IOException {
        try (InputStream input = getContentResolver().openInputStream(uri);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            if (input == null) {
                throw new IOException("无法打开文件");
            }
            byte[] buffer = new byte[16 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                if (output.size() > 32 * 1024 * 1024) {
                    throw new IOException("ROM 文件过大");
                }
            }
            return output.toByteArray();
        }
    }

    private void startGame(byte[] rom) {
        stopGame();
        core = NativeBridge.nativeCreate(rom);
        if (core == 0) {
            Toast.makeText(this, "ROM 不受支持或核心初始化失败", Toast.LENGTH_LONG).show();
            showPicker();
            return;
        }
        gameView = new GameView();
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.addView(gameView, new FrameLayout.LayoutParams(-1, -1));
        setContentView(root);
        startAudio();
    }

    private void startAudio() {
        int sampleRate = 32768;
        int min = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate,
                AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT,
                Math.max(min, 8192), AudioTrack.MODE_STREAM);
        running = true;
        audioTrack.play();
        audioThread = new Thread(() -> {
            short[] samples = new short[2048];
            while (running && core != 0) {
                int frames = NativeBridge.nativeReadAudio(core, samples, samples.length / 2);
                if (frames > 0) {
                    audioTrack.write(samples, 0, frames * 2);
                } else {
                    try {
                        Thread.sleep(2);
                    } catch (InterruptedException ignored) {
                        Thread.currentThread().interrupt();
                        break;
                    }
                }
            }
        }, "mGBA-Audio");
        audioThread.start();
    }

    private void stopGame() {
        running = false;
        if (audioThread != null) {
            audioThread.interrupt();
            try {
                audioThread.join(250);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            audioThread = null;
        }
        if (audioTrack != null) {
            audioTrack.pause();
            audioTrack.flush();
            audioTrack.release();
            audioTrack = null;
        }
        if (core != 0) {
            if (gameView != null) {
                gameView.stop();
                gameView = null;
            }
            NativeBridge.nativeDestroy(core);
            core = 0;
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (core != 0) {
            NativeBridge.nativePause(core);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (core != 0) {
            NativeBridge.nativeResume(core);
        }
    }

    @Override
    protected void onDestroy() {
        stopGame();
        super.onDestroy();
    }

    private final class GameView extends View {
        private final Paint paint = new Paint();
        private final Bitmap bitmap;
        private final int[] pixels;
        private volatile boolean render = true;
        private final Thread videoThread;

        GameView() {
            super(MainActivity.this);
            int width = NativeBridge.nativeWidth(core);
            int height = NativeBridge.nativeHeight(core);
            pixels = new int[width * height];
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            paint.setFilterBitmap(false);
            setFocusable(true);
            videoThread = new Thread(() -> {
                while (render && core != 0) {
                    if (NativeBridge.nativeWaitFrame(core, pixels)) {
                        bitmap.setPixels(pixels, 0, bitmap.getWidth(), 0, 0, bitmap.getWidth(), bitmap.getHeight());
                        postInvalidateOnAnimation();
                    }
                }
            }, "mGBA-Video");
            videoThread.start();
        }

        void stop() {
            render = false;
            if (core != 0) {
                NativeBridge.nativeStop(core);
            }
            try {
                videoThread.join(500);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float scale = Math.min(getWidth() / (float) bitmap.getWidth(), getHeight() / (float) bitmap.getHeight());
            float left = (getWidth() - bitmap.getWidth() * scale) / 2f;
            float top = (getHeight() - bitmap.getHeight() * scale) / 2f;
            canvas.drawBitmap(bitmap, null, new android.graphics.RectF(left, top,
                    left + bitmap.getWidth() * scale, top + bitmap.getHeight() * scale), paint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_DOWN || event.getAction() == MotionEvent.ACTION_UP || event.getAction() == MotionEvent.ACTION_MOVE) {
                int keys = 0;
                float x = event.getX() / Math.max(1f, getWidth());
                float y = event.getY() / Math.max(1f, getHeight());
                if (x < 0.35f && y > 0.35f && y < 0.8f) keys |= 1 << 5; // Left
                if (x > 0.65f && y > 0.35f && y < 0.8f) keys |= 1 << 4; // Right
                if (y < 0.35f && x < 0.5f) keys |= 1 << 6; // Up
                if (y > 0.8f && x < 0.5f) keys |= 1 << 7; // Down
                if (x > 0.65f && y < 0.5f) keys |= 1 << 0; // A
                if (x > 0.65f && y >= 0.5f) keys |= 1 << 1; // B
                NativeBridge.nativeSetKeys(core, event.getAction() == MotionEvent.ACTION_UP ? 0 : keys);
                return true;
            }
            return true;
        }
    }
}


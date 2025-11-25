package com.livsho.android;

import android.app.Activity;
import android.os.Bundle;
import android.content.pm.PackageManager;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

class Engine {
    static {
        System.loadLibrary("engine");
    }

    public static native long startPreview(Surface surface);
    public static native void stopPreview(long handle);

    public static native void startPublishing(long handle);
    public static native void stopPublishing(long handle);

    public static native void set(long handle, int width, int height);
}

public class App extends Activity implements SurfaceHolder.Callback {

    private static final int PERMISSION_REQ = 1001;

    private SurfaceView surfaceView;
    private Surface pendingSurface = null;
    private boolean permissionGranted = false;

    private long previewHandle = 0;
    private boolean publishing = false;

    private Button publishBtn;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);

        publishBtn = new Button(this);
        publishBtn.setText("Start Publishing");
        publishBtn.setOnClickListener(v -> togglePublishing());

        // Layout
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        FrameLayout previewContainer = new FrameLayout(this);
        previewContainer.addView(surfaceView);

        root.addView(previewContainer,
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        0, 1.0f));

        root.addView(publishBtn,
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));

        setContentView(root);

        requestCameraPermission();
    }

    private void requestCameraPermission() {
        if (checkSelfPermission(android.Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) {

            requestPermissions(
                    new String[]{android.Manifest.permission.CAMERA},
                    PERMISSION_REQ
            );
        } else {
            permissionGranted = true;
            tryStartPreview();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);

        if (requestCode == PERMISSION_REQ) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
                permissionGranted = true;
                tryStartPreview();
            }
        }
    }

    private void tryStartPreview() {
        if (permissionGranted && pendingSurface != null && previewHandle == 0) {
            previewHandle = Engine.startPreview(pendingSurface);
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        pendingSurface = holder.getSurface();
        tryStartPreview();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (previewHandle != 0) {
            Engine.set(previewHandle, width, height);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        pendingSurface = null;

        if (previewHandle != 0) {
            Engine.stopPreview(previewHandle);
            previewHandle = 0;
        }
    }

    //
    // Toggle publishing state
    //
    private void togglePublishing() {
        if (previewHandle == 0) return;

        if (!publishing) {
            Engine.startPublishing(previewHandle);
            publishing = true;
            publishBtn.setText("Stop Publishing");
        } else {
            Engine.stopPublishing(previewHandle);
            publishing = false;
            publishBtn.setText("Start Publishing");
        }
    }
}

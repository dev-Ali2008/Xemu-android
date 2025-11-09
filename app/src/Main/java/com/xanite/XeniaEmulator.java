package com.xanite;

import android.app.Activity;
import android.os.Bundle;
import android.view.SurfaceView;
import android.widget.Toast;

public class XeniaEmulator extends Activity {
    static {
        System.loadLibrary("xanite");
    }
    
    private SurfaceView surfaceView;
    private native void nativeOnCreate();
    private native void nativeOnDestroy();
    private native void nativeOnPause();
    private native void nativeOnResume();
    private native void nativeOnSurfaceCreated(Surface surface);
    private native void nativeOnSurfaceChanged(int width, int height);
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        
        surfaceView = findViewById(R.id.surface_view);
        surfaceView.getHolder().addCallback(new SurfaceCallback());
        
        nativeOnCreate();
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        nativeOnDestroy();
    }
    
    @Override
    protected void onPause() {
        super.onPause();
        nativeOnPause();
    }
    
    @Override
    protected void onResume() {
        super.onResume();
        nativeOnResume();
    }
    
    // JNI 
    public void showToast(String message) {
        runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
    }
    
    public void onGameStatusChanged(int status, String message) {
        // 根据游戏状态更新用户界面‽
    }
    
    private class SurfaceCallback implements SurfaceHolder.Callback {
        public void surfaceCreated(SurfaceHolder holder) {
            nativeOnSurfaceCreated(holder.getSurface());
        }
        
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            nativeOnSurfaceChanged(width, height);
        }
        
        public void surfaceDestroyed(SurfaceHolder holder) {
            nativeOnSurfaceCreated(null);
        }
    }
  }

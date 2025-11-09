package com.xanite.util;

public class NativeBridge {
    static {
        System.loadLibrary("xanite");
    }
    
    public static native boolean loadGame(String gamePath);
    public static native boolean startEmulation();
    public static native void stopEmulation();
    public static native void pauseEmulation();
    public static native void resumeEmulation();
   
    public static native boolean saveState(String slot);
    public static native boolean loadState(String slot);
    
    public static native void updateSettings(String settingsJson);
    public static native String getSettings();
   
    public static native String getVersion();
    public static native String getDeviceInfo();
    
    public static native void takeScreenshot();
    public static native boolean isEmulationRunning();
    public static native boolean isEmulationPaused();
}

package com.xanite.model;

public class EmulatorSettings {
    private GraphicsSettings graphics;
    private AudioSettings audio;
    private ControlSettings controls;
    private SystemSettings system;
    
    public static class GraphicsSettings {
        public float resolutionScale = 1.0f;
        public boolean vsync = true;
        public int msaaLevel = 2;
    }
    
    public static class AudioSettings {
        public int audioLatency = 128;
        public float volume = 1.0f;
    }
    
    public static class ControlSettings {
        public float touchSensitivity = 1.0f;
        public boolean touchControls = true;
    }
    
    public static class SystemSettings {
        public String contentPath = "/sdcard/xenia/content";
        public String savePath = "/sdcard/xenia/saves";
    }
}

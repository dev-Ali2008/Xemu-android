package com.xanite.utils;

object EmulatorDisplayLog {
    fun display(tag: String, message: String) {
        // Hier kann das Logging z.B. an Logcat, Datei oder UI erfolgen
        android.util.Log.w("DISPLAY", "[$tag] $message")
    }
}

public class EmulatorHelper {
}

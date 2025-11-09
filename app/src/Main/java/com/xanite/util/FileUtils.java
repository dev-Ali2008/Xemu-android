package com.xanite.util;

import android.content.Context;
import android.os.Environment;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.channels.FileChannel;

public class FileUtils {
    
    public static boolean createAppDirectories() {
        String basePath = Environment.getExternalStorageDirectory() + "/xenia";
        String[] directories = {
            basePath,
            basePath + "/content",
            basePath + "/cache", 
            basePath + "/saves",
            basePath + "/screenshots",
            basePath + "/logs"
        };
        
        boolean success = true;
        for (String dir : directories) {
            File directory = new File(dir);
            if (!directory.exists()) {
                if (!directory.mkdirs()) {
                    success = false;
                }
            }
        }
        return success;
    }
    
    public static String getFileSize(File file) {
        long size = file.length();
        if (size < 1024) return size + " B";
        else if (size < 1024 * 1024) return (size / 1024) + " KB";
        else return (size / (1024 * 1024)) + " MB";
    }
    
    public static boolean copyFile(File source, File dest) {
        try (FileChannel sourceChannel = new FileInputStream(source).getChannel();
             FileChannel destChannel = new FileOutputStream(dest).getChannel()) {
            destChannel.transferFrom(sourceChannel, 0, sourceChannel.size());
            return true;
        } catch (IOException e) {
            return false;
        }
    }
}
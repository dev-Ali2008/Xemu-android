package com.xanite.service;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import com.xanite.model.GameInfo;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class GameScannerService extends Service {
    public static final String ACTION_SCAN_COMPLETE = "com.xanite.SCAN_COMPLETE";
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        scanForGames();
        return START_NOT_STICKY;
    }
    
    private void scanForGames() {
        new Thread(() -> {
            List<GameInfo> games = new ArrayList<>();
            File contentDir = new File("/sdcard/xenia/content");
            
            if (contentDir.exists()) {
                scanDirectory(contentDir, games);
            }
          
            Intent resultIntent = new Intent(ACTION_SCAN_COMPLETE);
            
            sendBroadcast(resultIntent);
        }).start();
    }
    
    private void scanDirectory(File directory, List<GameInfo> games) {
        File[] files = directory.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.isDirectory()) {
                    scanDirectory(file, games);
                } else if (isGameFile(file)) {
                    // GameInfo
                }
            }
        }
    }
    
    private boolean isGameFile(File file) {
        String name = file.getName().toLowerCase();
        return name.endsWith(".xex") || name.endsWith(".iso") || 
               name.endsWith(".xiso") || name.endsWith(".zar");
    }
    
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
          }

package com.xanite.repository;

import com.xanite.model.GameInfo;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class GameRepository {
    
    public List<GameInfo> scanForGames() {
        List<GameInfo> games = new ArrayList<>();
        File contentDir = new File("/sdcard/xenia/content");
        
        if (contentDir.exists() && contentDir.isDirectory()) {
            scanDirectory(contentDir, games);
        }
        
        return games;
    }
    
    private void scanDirectory(File directory, List<GameInfo> games) {
        File[] files = directory.listFiles();
        if (files == null) return;
        
        for (File file : files) {
            if (file.isDirectory()) {
                scanDirectory(file, games);
            } else if (isGameFile(file)) {
                GameInfo game = createGameInfo(file);
                if (game != null) {
                    games.add(game);
                }
            }
        }
    }
    
    private boolean isGameFile(File file) {
        String name = file.getName().toLowerCase();
        return name.endsWith(".xex") || name.endsWith(".iso") || 
               name.endsWith(".xiso") || name.endsWith(".zar");
    }
    
    private GameInfo createGameInfo(File file) {
        String title = file.getName().replaceFirst("[.][^.]+$", "");
        return new GameInfo(file.getAbsolutePath(), title, 0, "Unknown", getFileType(file));
    }
    
    private String getFileType(File file) {
        String name = file.getName().toLowerCase();
        if (name.endsWith(".xex")) return "XEX";
        if (name.endsWith(".iso")) return "ISO";
        if (name.endsWith(".xiso")) return "XISO";
        if (name.endsWith(".zar")) return "ZAR";
        return "Unknown";
    }
                          }

package com.xanite.model;

public class GameInfo {
    private String gamePath;
    private String titleName;
    private int titleId;
    private String region;
    private String fileType;
    
    public GameInfo(String gamePath, String titleName, int titleId, String region, String fileType) {
        this.gamePath = gamePath;
        this.titleName = titleName;
        this.titleId = titleId;
        this.region = region;
        this.fileType = fileType;
    }
    
    public String getGamePath() { return gamePath; }
    public String getTitleName() { return titleName; }
    public int getTitleId() { return titleId; }
    public String getRegion() { return region; }
    public String getFileType() { return fileType; }
}

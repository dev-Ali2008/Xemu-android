package com.xanite.viewmodel;

import androidx.lifecycle.ViewModel;
import com.xanite.model.EmulatorSettings;

public class SettingsViewModel extends ViewModel {
    private EmulatorSettings settings;
    
    public SettingsViewModel() {
     
        settings = new EmulatorSettings();
    }
    
    public EmulatorSettings getSettings() {
        return settings;
    }
    
    public void saveSettings(EmulatorSettings newSettings) {
        this.settings = newSettings;
        
    }
    
    public void resetToDefaults() {
        settings = new EmulatorSettings();
    }
}
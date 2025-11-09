package com.xanite.viewmodel;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

public class EmulatorViewModel extends ViewModel {
    private MutableLiveData<Boolean> isRunning = new MutableLiveData<>(false);
    private MutableLiveData<Boolean> isPaused = new MutableLiveData<>(false);
    private MutableLiveData<String> currentGame = new MutableLiveData<>("");
    private MutableLiveData<Float> fps = new MutableLiveData<>(0.0f);
    
    public LiveData<Boolean> getIsRunning() { return isRunning; }
    public LiveData<Boolean> getIsPaused() { return isPaused; }
    public LiveData<String> getCurrentGame() { return currentGame; }
    public LiveData<Float> getFps() { return fps; }
    
    public void setRunning(boolean running) { isRunning.setValue(running); }
    public void setPaused(boolean paused) { isPaused.setValue(paused); }
    public void setCurrentGame(String game) { currentGame.setValue(game); }
    public void setFps(float fpsValue) { fps.setValue(fpsValue); }
}

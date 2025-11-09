package com.xanite.viewmodel;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import com.xanite.model.GameInfo;
import com.xanite.repository.GameRepository;
import java.util.List;

public class GameLibraryViewModel extends ViewModel {
    private GameRepository gameRepository;
    private MutableLiveData<List<GameInfo>> games = new MutableLiveData<>();
    private MutableLiveData<Boolean> isLoading = new MutableLiveData<>(false);
    
    public GameLibraryViewModel() {
        gameRepository = new GameRepository();
    }
    
    public LiveData<List<GameInfo>> getGames() {
        return games;
    }
    
    public LiveData<Boolean> getIsLoading() {
        return isLoading;
    }
    
    public void loadGames() {
        isLoading.setValue(true);
        new Thread(() -> {
            List<GameInfo> gameList = gameRepository.scanForGames();
            games.postValue(gameList);
            isLoading.postValue(false);
        }).start();
    }
    
    public void refreshGames() {
        loadGames();
    }
}
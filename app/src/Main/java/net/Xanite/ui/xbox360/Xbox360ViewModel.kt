package com.xanite.xbox360

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import com.xanite.models.Game
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class Xbox360ViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = Xbox360Repository(application)

    val gamesList = MutableLiveData<List<Game>>()
    val errorMessage = MutableLiveData<String>()
    val successMessage = MutableLiveData<String>()
    val isLoading = MutableLiveData<Boolean>()

    fun loadGames() {
        viewModelScope.launch {
            isLoading.postValue(true)
            try {
                val games = withContext(Dispatchers.IO) {
                    repository.initialize()
                    repository.getGames()
                }
                gamesList.postValue(games)
            } catch (e: Exception) {
                errorMessage.postValue("Failed to load games: ${e.message}")
            } finally {
                isLoading.postValue(false)
            }
        }
    }

    fun addGame(uri: Uri) {
        viewModelScope.launch {
            isLoading.postValue(true)
            try {
                val result = withContext(Dispatchers.IO) {
                    repository.addGame(uri)
                }
                if (result) {
                    loadGames()
                    successMessage.postValue("Game added successfully")
                } else {
                    errorMessage.postValue("Unsupported format or invalid file")
                }
            } catch (e: Exception) {
                errorMessage.postValue("Error: ${e.message}")
            } finally {
                isLoading.postValue(false)
            }
        }
    }
}

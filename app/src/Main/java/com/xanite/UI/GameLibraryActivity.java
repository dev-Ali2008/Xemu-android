package com.xanite.ui;

import android.os.Bundle;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import com.xanite.R;
import com.xanite.adapter.GameAdapter;
import com.xanite.model.GameInfo;
import com.xanite.viewmodel.GameLibraryViewModel;

public class GameLibraryActivity extends AppCompatActivity {
    private RecyclerView gamesRecyclerView;
    private GameAdapter gameAdapter;
    private GameLibraryViewModel viewModel;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_game_library);
        
        gamesRecyclerView = findViewById(R.id.games_recycler_view);
        gamesRecyclerView.setLayoutManager(new GridLayoutManager(this, 2));
        
        gameAdapter = new GameAdapter();
        gamesRecyclerView.setAdapter(gameAdapter);
        
        loadGames();
    }
    
    private void loadGames() {
        //install and save games 
    }
}

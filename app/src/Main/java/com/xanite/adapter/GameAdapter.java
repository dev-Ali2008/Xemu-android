package com.xanite.adapter;

import android.view.LayoutInflater;
import android.view.ViewGroup;
import androidx.recyclerview.widget.RecyclerView;
import com.xanite.databinding.ItemGameBinding;
import com.xanite.model.GameInfo;
import java.util.List;

public class GameAdapter extends RecyclerView.Adapter<GameAdapter.GameViewHolder> {
    private List<GameInfo> games;
    
    public static class GameViewHolder extends RecyclerView.ViewHolder {
        ItemGameBinding binding;
        
        public GameViewHolder(ItemGameBinding binding) {
            super(binding.getRoot());
            this.binding = binding;
        }
        
        public void bind(GameInfo game) {
            binding.gameTitle.setText(game.getTitleName());
            binding.gameRegion.setText(game.getRegion());
        }
    }
    
    @Override
    public GameViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        ItemGameBinding binding = ItemGameBinding.inflate(
            LayoutInflater.from(parent.getContext()), parent, false);
        return new GameViewHolder(binding);
    }
    
    @Override
    public void onBindViewHolder(GameViewHolder holder, int position) {
        holder.bind(games.get(position));
    }
    
    @Override
    public int getItemCount() {
        return games != null ? games.size() : 0;
    }
    
    public void setGames(List<GameInfo> games) {
        this.games = games;
        notifyDataSetChanged();
    }
}

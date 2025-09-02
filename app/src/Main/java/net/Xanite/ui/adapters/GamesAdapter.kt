package com.xanite.adapters

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.xanite.R
import com.xanite.models.Game

class GamesAdapter(
    private val games: List<Game>,
    private val onItemClick: (Game) -> Unit
) : RecyclerView.Adapter<GamesAdapter.GameViewHolder>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_game, parent, false)
        return GameViewHolder(view)
    }

    override fun onBindViewHolder(holder: GameViewHolder, position: Int) {
        holder.bind(games[position])
    }

    override fun getItemCount() = games.size

    inner class GameViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val cover: ImageView = itemView.findViewById(R.id.gameCover)
        private val title: TextView = itemView.findViewById(R.id.gameTitle)
        private val size: TextView = itemView.findViewById(R.id.gameSize)

        fun bind(game: Game) {

            title.text = game.name


            size.text = game.getFormattedSize()


            cover.setImageResource(R.drawable.ic_game_placeholder)


            itemView.setOnClickListener { onItemClick(game) }
        }
    }
}

package com.xanite.xbox360

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.xanite.models.Game

class SimpleGamesAdapter(
    private val onItemClick: (Game) -> Unit
) : RecyclerView.Adapter<SimpleGamesAdapter.GameViewHolder>() {

    private val games = mutableListOf<Game>()

    class GameViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val gameName: TextView = itemView.findViewById(android.R.id.text1)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(android.R.layout.simple_list_item_1, parent, false)
        return GameViewHolder(view)
    }

    override fun onBindViewHolder(holder: GameViewHolder, position: Int) {
        val game = games[position]
        holder.gameName.text = game.name
        holder.itemView.setOnClickListener { onItemClick(game) }
    }

    override fun getItemCount() = games.size

    fun submitList(newGames: List<Game>) {
        games.clear()
        games.addAll(newGames)
        notifyDataSetChanged()
    }
}

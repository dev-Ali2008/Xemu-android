package com.xanite.xbox360

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.xanite.R
import com.xanite.models.Game

class Xbox360GamesAdapter(
    private val onItemClick: (Game) -> Unit
) : ListAdapter<Game, Xbox360GamesAdapter.GameViewHolder>(GameDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_xbox360_game, parent, false)
        return GameViewHolder(view)
    }

    override fun onBindViewHolder(holder: GameViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class GameViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val gameName: TextView = itemView.findViewById(R.id.gameName)
        private val gameDetails: TextView = itemView.findViewById(R.id.gameDetails)

        fun bind(game: Game) {

            gameName.text = game.name


         val typeText = when (game.type) {
                "XBOX360" -> "Xbox 360 Game"
                "ISO" -> "ISO Image"
                "XEX" -> "XEX Executable"
                "ZIP" -> "ZIP Archive"
                "GOD" -> "GOD Image"
                else -> "Unknown Type"
            }

            val sizeText = try {
                formatSize(game.fileSize) 
            } catch (e: Exception) {
                "Size: N/A"
            }

            gameDetails.text = "$typeText • $sizeText"
            itemView.setOnClickListener { onItemClick(game) }
        }

        private fun formatSize(size: Long): String {
            return when {
                size < 1024 -> "$size B"
                size < 1024 * 1024 -> "${size / 1024} KB"
                size < 1024 * 1024 * 1024 -> "${size / (1024 * 1024)} MB"
                else -> "${size / (1024 * 1024 * 1024)} GB"
            }
        }
    }

    class GameDiffCallback : DiffUtil.ItemCallback<Game>() {
        override fun areItemsTheSame(oldItem: Game, newItem: Game): Boolean {
            return oldItem.path == newItem.path
        }

        override fun areContentsTheSame(oldItem: Game, newItem: Game): Boolean {
            return oldItem.name == newItem.name &&
                   oldItem.type == newItem.type &&
                   oldItem.fileSize == newItem.fileSize
        }
    }
}

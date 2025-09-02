package com.xanite.xbox360

import android.app.Dialog
import android.content.Context
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.view.Window
import android.widget.FrameLayout
import android.widget.Toast
import com.xanite.R
import com.xanite.databinding.EmulatorLayoutBinding
import com.xanite.models.Game
import kotlin.random.Random

class Xbox360Emulator(private val context: Context) {
    private var dialog: Dialog? = null
    private lateinit var binding: EmulatorLayoutBinding
    private val handler = Handler(Looper.getMainLooper())
    private var colorChangeRunnable: Runnable? = null
    private var fixedColor: Int? = null
    private var startTime: Long = 0

    fun showGame(game: Game) {
        dialog = Dialog(context).apply {
            requestWindowFeature(Window.FEATURE_NO_TITLE)
            binding = EmulatorLayoutBinding.inflate(layoutInflater)
            setContentView(binding.root)
            window?.setLayout(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
            setCancelable(false)


            val gameName = game.name ?: context.getString(R.string.unknown_game)
            Toast.makeText(context, context.getString(R.string.running_game) + ": $gameName", Toast.LENGTH_SHORT).show()

            startColorAnimation()
            show()
        }
    }

    private fun startColorAnimation() {
        startTime = System.currentTimeMillis()
        colorChangeRunnable = object : Runnable {
            override fun run() {
                val currentTime = System.currentTimeMillis()
                val elapsedTime = currentTime - startTime

                if (elapsedTime >= 120000) {
                    fixedColor?.let { color ->
                        binding.mainLayout.setBackgroundColor(color)
                    } ?: run {
                        fixedColor = getRandomColor()
                        binding.mainLayout.setBackgroundColor(fixedColor!!)
                    }
                } else {
                    binding.mainLayout.setBackgroundColor(getRandomColor())
                    handler.postDelayed(this, 5000)
                }
            }
        }
        handler.post(colorChangeRunnable!!)
    }

    private fun getRandomColor(): Int {
        return Color.rgb(
            Random.nextInt(256),
            Random.nextInt(256),
            Random.nextInt(256)
        )
    }

    fun dismiss() {
        colorChangeRunnable?.let { handler.removeCallbacks(it) }
        dialog?.dismiss()
        dialog = null
    }
}

package com.xanite.settings

import android.annotation.SuppressLint
import android.graphics.PorterDuff
import android.os.Bundle
import android.view.MotionEvent
import android.view.ViewGroup
import android.widget.Button
import android.widget.RelativeLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.xanite.R
import com.xanite.overlay.PadOverlay

class EditOverlayActivity : AppCompatActivity() {

    private lateinit var btnRestart: Button
    private lateinit var btnExit: Button
    private lateinit var btnTest: Button
    private lateinit var btnSave: Button
    private lateinit var btnClose: Button
    private lateinit var saveStatus: TextView
    private lateinit var overlayView: PadOverlay
    private lateinit var scaleSeekBar: SeekBar
    private lateinit var opacitySeekBar: SeekBar
    private lateinit var scaleValueText: TextView
    private lateinit var opacityValueText: TextView

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_edit_overlay_without_overlay)


        val rootLayout = findViewById<RelativeLayout>(R.id.root_layout)
        overlayView = PadOverlay(this).apply {
            layoutParams = RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ).apply {
                addRule(RelativeLayout.ABOVE, R.id.control_panel)
            }
        }
        rootLayout.addView(overlayView, 0)


        initViews()
        setupControls()
    }

    private fun initViews() {
        btnClose = findViewById(R.id.btn_close)
        btnRestart = findViewById(R.id.btn_restart)
        btnTest = findViewById(R.id.btn_test)
        btnSave = findViewById(R.id.btn_save)
        btnExit = findViewById(R.id.btn_exit)
        scaleSeekBar = findViewById(R.id.scale_seekbar)
        opacitySeekBar = findViewById(R.id.opacity_seekbar)
        scaleValueText = findViewById(R.id.scale_value)
        opacityValueText = findViewById(R.id.opacity_value)
        saveStatus = findViewById(R.id.save_status)
    }

    private fun setupControls() {

        val prefs = getSharedPreferences("OverlayPrefs", MODE_PRIVATE)
        scaleSeekBar.progress = prefs.getInt("overlay_scale", 50)
        opacitySeekBar.progress = prefs.getInt("overlay_opacity", 60)
        scaleValueText.text = "${scaleSeekBar.progress}%"
        opacityValueText.text = "${opacitySeekBar.progress}%"


        setupButtonEffects()


        setupEventHandlers()


        setupSeekBarListeners()
    }

    private fun setupButtonEffects() {
        listOf(btnClose, btnRestart, btnTest, btnSave, btnExit).forEach { button ->
            button.setOnTouchListener { v, event ->
                when (event.action) {
                    MotionEvent.ACTION_DOWN -> {
                        v.background.setColorFilter(0x6400A0FF.toInt(), PorterDuff.Mode.SRC_ATOP)
                        v.invalidate()
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        v.background.clearColorFilter()
                        v.invalidate()
                    }
                }
                false
            }
        }
    }

    private fun setupEventHandlers() {
        btnClose.setOnClickListener { finish() }
        btnExit.setOnClickListener { finish() }

        btnRestart.setOnClickListener {
            overlayView.resetButtonConfigs()
            scaleSeekBar.progress = 50
            opacitySeekBar.progress = 60
            saveStatus.text = getString(R.string.settings_reset)
        }

        btnTest.setOnClickListener {
            testOverlayFunctionality(overlayView)
        }

        btnSave.setOnClickListener {
            getSharedPreferences("OverlayPrefs", MODE_PRIVATE).edit().apply {
                putInt("overlay_scale", scaleSeekBar.progress)
                putInt("overlay_opacity", opacitySeekBar.progress)
                apply()
            }
            saveStatus.text = getString(R.string.settings_saved)
            Toast.makeText(this, R.string.settings_saved_success, Toast.LENGTH_SHORT).show()
        }
    }

    private fun setupSeekBarListeners() {
        scaleSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                overlayView.setButtonScale(progress)
                scaleValueText.text = "$progress%"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        opacitySeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                overlayView.setButtonOpacity(progress)
                opacityValueText.text = "$progress%"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
    }

    private fun testOverlayFunctionality(overlay: PadOverlay) {
        overlay.highlightAllButtons(true)
        overlay.vibrateForTest()
        Toast.makeText(this, R.string.testing_overlay, Toast.LENGTH_SHORT).show()
        overlay.logCurrentLayout()
    }
}

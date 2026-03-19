package Ali.Xanite

import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.input.InputManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.util.DisplayMetrics
import android.util.Log
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.view.GravityCompat
import androidx.drawerlayout.widget.DrawerLayout
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLControllerManager
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : SDLActivity(), InputManager.InputDeviceListener {

    companion object {
        private const val TOTAL_SNAPSHOT_SLOTS = 5
        private const val SNAPSHOT_PREVIEW_HEADER_SIZE = 12
        private const val TAG = "MainActivity"
    }

    private data class SnapshotSlotPreview(
        val slot: Int,
        val slotLabel: String,
        val gameTitle: String,
        val thumbnail: Bitmap?,
        val timestamp: String?,
        val hasData: Boolean
    )

    private var onScreenController: OnScreenController? = null
    private var controllerBridge: ControllerInputBridge? = null
    private var isControllerVisible = false
    private var inputManager: InputManager? = null
    private var hasPhysicalController = false
    private var startButtonDown = false
    private var selectButtonDown = false
    private var comboTriggered = false
    private var currentGameId = ""
    private var currentGameName = ""

    private var screenWidth = 0
    private var screenHeight = 0
    private var screenDensity = 0f

    private lateinit var drawerLayout: DrawerLayout
    private lateinit var drawerContent: LinearLayout
    private var isDrawerSetup = false

    private var backPressedTime: Long = 0

    private external fun nativeSaveSnapshot(name: String): Boolean
    private external fun nativeLoadSnapshot(name: String): Boolean

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = getSharedPreferences("Xanite_prefs", MODE_PRIVATE)
        currentGameId = prefs.getString("current_game_id", "default") ?: "default"
        currentGameName = prefs.getString("current_game_name", "") ?: ""

        getScreenMetrics()

        Log.d("MainActivity", "Screen initialized: ${screenWidth}x${screenHeight}, Game: $currentGameName ($currentGameId)")
    }

    override fun onResume() {
        super.onResume()

        getScreenMetrics()
        hideSystemUI()

        if (!isDrawerSetup && mLayout != null) {
            mLayout?.post {
                setupDrawerLayout()
            }
        }

        mLayout?.postDelayed({
            registerVirtualController()
        }, 500)
    }

    private fun getScreenMetrics() {
        val displayMetrics = DisplayMetrics()
        windowManager.defaultDisplay.getMetrics(displayMetrics)

        screenWidth = displayMetrics.widthPixels
        screenHeight = displayMetrics.heightPixels
        screenDensity = displayMetrics.density

        if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            if (screenWidth < screenHeight) {
                val temp = screenWidth
                screenWidth = screenHeight
                screenHeight = temp
            }
        }
    }

    private fun setupDrawerLayout() {
        if (isDrawerSetup || mLayout == null) return

        try {

            drawerLayout = DrawerLayout(this).apply {
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )

                addDrawerListener(object : DrawerLayout.SimpleDrawerListener() {
                    override fun onDrawerSlide(drawerView: View, slideOffset: Float) {

                        hideSystemUI()
                    }

                    override fun onDrawerOpened(drawerView: View) {

                        backPressedTime = 0
                    }
                })
            }

            val sdlLayout = mLayout
            if (sdlLayout != null) {

                (sdlLayout.parent as? ViewGroup)?.removeView(sdlLayout)

                drawerLayout.addView(sdlLayout, DrawerLayout.LayoutParams(
                    DrawerLayout.LayoutParams.MATCH_PARENT,
                    DrawerLayout.LayoutParams.MATCH_PARENT
                ))
            }

            drawerContent = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = DrawerLayout.LayoutParams(
                    (screenWidth * 0.55).toInt(), 
                    DrawerLayout.LayoutParams.MATCH_PARENT,
                    Gravity.START
                )
                setBackgroundColor(0xDD222222.toInt()) 


                setPadding(24, 48, 24, 24)
            }

            val btnResume = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = "Resume"
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setOnClickListener {
                    drawerLayout.closeDrawer(GravityCompat.START)
                }
            }
            drawerContent.addView(btnResume)

            val btnSaveState = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = "Save State"
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setOnClickListener {
                    drawerLayout.closeDrawer(GravityCompat.START)
                    showSaveStateDialog()
                }
            }
            drawerContent.addView(btnSaveState)

            val btnLoadState = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = "Load State"
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setOnClickListener {
                    drawerLayout.closeDrawer(GravityCompat.START)
                    showLoadStateDialog()
                }
            }
            drawerContent.addView(btnLoadState)

            val btnToggleControls = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = "Hide Touch Controls"
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setOnClickListener {
                    toggleOnScreenController()
                    text = if (isControllerVisible) {
                        "Hide Touch Controls"
                    } else {
                        "Show Touch Controls"
                    }
                    drawerLayout.closeDrawer(GravityCompat.START)
                }
            }
            drawerContent.addView(btnToggleControls)


            val btnExitToLibrary = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = "Exit to Game Library"
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setOnClickListener {
                    drawerLayout.closeDrawer(GravityCompat.START)
                    exitToGameLibrary()
                }
            }
            drawerContent.addView(btnExitToLibrary)

            val btnQuitApp = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = "Quit App"
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )
                setOnClickListener {
                    drawerLayout.closeDrawer(GravityCompat.START)
                    finishAffinity()
                }
            }
            drawerContent.addView(btnQuitApp)

            drawerLayout.addView(drawerContent)

            setContentView(drawerLayout)

            drawerLayout.setDrawerLockMode(DrawerLayout.LOCK_MODE_UNLOCKED)

            isDrawerSetup = true
            Log.d("MainActivity", "Simple drawer layout setup complete with width: ${(screenWidth * 0.55).toInt()}")

            setupOnScreenController()
            setupControllerDetection()

        } catch (e: Exception) {
            Log.e("MainActivity", "Error setting up drawer: ${e.message}")
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)

        getScreenMetrics()
        hideSystemUI()

        if (isDrawerSetup && ::drawerContent.isInitialized) {
            try {
                val params = drawerContent.layoutParams as DrawerLayout.LayoutParams
                params.width = (screenWidth * 0.55).toInt()
                drawerContent.layoutParams = params
            } catch (e: Exception) {
                Log.e("MainActivity", "Error updating drawer width: ${e.message}")
            }
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUI()
            getScreenMetrics()
        }
    }

    override fun onBackPressed() {

        if (isDrawerSetup && drawerLayout.isDrawerOpen(GravityCompat.START)) {
            drawerLayout.closeDrawer(GravityCompat.START)
            return
        }

        if (backPressedTime + 2000 > System.currentTimeMillis()) {

            super.onBackPressed()
            finishAffinity()
        } else {

            backPressedTime = System.currentTimeMillis()
            Toast.makeText(this, "Press back again to exit", Toast.LENGTH_SHORT).show()
            
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK && !isGamepadKeyEvent(event)) {
            if (event.action == KeyEvent.ACTION_UP && event.repeatCount == 0) {
                onBackPressed() 
                return true
            }
            return true
        }

        if (handleGamepadMenuCombo(event)) {
            return true
        }

        if (event.keyCode == KeyEvent.KEYCODE_MENU && event.action == KeyEvent.ACTION_UP) {
            if (isDrawerSetup) {
                if (drawerLayout.isDrawerOpen(GravityCompat.START)) {
                    drawerLayout.closeDrawer(GravityCompat.START)
                } else {
                    drawerLayout.openDrawer(GravityCompat.START)
                }
            }
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    private fun hideSystemUI() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            @Suppress("DEPRECATION")
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.systemBars())
                controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            )
        }

        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN)
    }

    private fun setupOnScreenController() {
        try {
            onScreenController = OnScreenController(this).apply {
                layoutParams = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )
            }

            controllerBridge = ControllerInputBridge()
            onScreenController?.setControllerListener(controllerBridge!!)

            mLayout?.addView(onScreenController)

            isControllerVisible = true
            onScreenController?.visibility = View.VISIBLE

            registerVirtualController()
            updateControllerVisibility()

            Log.d("MainActivity", "On-screen controller setup complete")
        } catch (e: Exception) {
            Log.e("MainActivity", "Error setting up controller: ${e.message}")
        }
    }

    private fun registerVirtualController() {
        try {
            SDLControllerManager.nativeAddJoystick(
                -2,
                "PS Vita Controller",
                "Virtual on-screen controller",
                0x054C,
                0x05C4,
                false,
                0xFFFF,
                6,
                0x3F,
                0,
                0
            )
        } catch (e: Exception) {
            Log.e("MainActivity", "Failed to register virtual controller: ${e.message}")
        }
    }

    private fun setupControllerDetection() {
        inputManager = getSystemService(Context.INPUT_SERVICE) as InputManager
        inputManager?.registerInputDeviceListener(this, null)
        checkForPhysicalControllers()
    }

    private fun checkForPhysicalControllers() {
        val deviceIds = inputManager?.inputDeviceIds ?: return
        hasPhysicalController = deviceIds.any { deviceId ->
            val device = inputManager?.getInputDevice(deviceId)
            isGameController(device)
        }
        updateControllerVisibility()
    }

    private fun isGameController(device: InputDevice?): Boolean {
        if (device == null) return false
        val sources = device.sources
        return ((sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
               ((sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
    }

    private fun updateControllerVisibility() {
        val shouldShow = !hasPhysicalController
        if (shouldShow != isControllerVisible) {
            isControllerVisible = shouldShow
            onScreenController?.visibility = if (shouldShow) View.VISIBLE else View.GONE
        }
    }

    override fun onInputDeviceAdded(deviceId: Int) {
        val device = inputManager?.getInputDevice(deviceId)
        if (isGameController(device)) {
            hasPhysicalController = true
            updateControllerVisibility()
        }
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        checkForPhysicalControllers()
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        checkForPhysicalControllers()
    }

    override fun onDestroy() {
        try {
            SDLControllerManager.nativeRemoveJoystick(-2)
        } catch (e: Exception) {
            Log.e("MainActivity", "Failed to unregister virtual controller: ${e.message}")
        }
        inputManager?.unregisterInputDeviceListener(this)
        super.onDestroy()
    }

    fun toggleOnScreenController() {
        isControllerVisible = !isControllerVisible
        onScreenController?.visibility = if (isControllerVisible) View.VISIBLE else View.GONE
    }

    fun openGameMenu() {
        if (isDrawerSetup) {
            drawerLayout.openDrawer(GravityCompat.START)
        }
    }

    fun closeGameMenu() {
        if (isDrawerSetup) {
            drawerLayout.closeDrawer(GravityCompat.START)
        }
    }

    private fun handleGamepadMenuCombo(event: KeyEvent): Boolean {
        if (!isGamepadKeyEvent(event)) return false

        val isStartKey = event.keyCode == KeyEvent.KEYCODE_BUTTON_START
        val isSelectKey = event.keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
            event.keyCode == KeyEvent.KEYCODE_BACK
        if (!isStartKey && !isSelectKey) return false

        when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (isStartKey) startButtonDown = true
                if (isSelectKey) selectButtonDown = true

                if (!comboTriggered && event.repeatCount == 0 &&
                    startButtonDown && selectButtonDown) {
                    comboTriggered = true
                    openGameMenu()
                    return true
                }
            }
            KeyEvent.ACTION_UP -> {
                if (isStartKey) startButtonDown = false
                if (isSelectKey) selectButtonDown = false
                if (!startButtonDown || !selectButtonDown) comboTriggered = false
            }
        }
        return comboTriggered
    }

    private fun isGamepadKeyEvent(event: KeyEvent): Boolean {
        val source = event.source
        return ((source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
            ((source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
    }

    private fun exitToGameLibrary() {
        val intent = Intent(this, GameLibraryActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        }
        startActivity(intent)
        finish()
    }

    private fun getGameSnapshotDir(): File {
        val baseDir = File(filesDir, "xanite/snapshots")
        val gameDir = File(baseDir, currentGameId)
        if (!gameDir.exists()) {
            gameDir.mkdirs()
        }
        return gameDir
    }

    private fun slotName(slot: Int): String = "slot_$slot"

    private fun getSnapshotThumbFile(slot: Int): File = File(getGameSnapshotDir(), "${slotName(slot)}.thm")
    private fun getSnapshotInfoFile(slot: Int): File = File(getGameSnapshotDir(), "${slotName(slot)}.info")

    private fun saveSnapshotInfo(slot: Int) {
        try {
            val infoFile = getSnapshotInfoFile(slot)
            val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US).format(Date())

            val gameNameToSave = if (currentGameName.isNotEmpty()) currentGameName else "Game"
            val info = "$gameNameToSave|$timestamp"
            infoFile.writeText(info, Charsets.UTF_8)
        } catch (e: Exception) {
            Log.e(TAG, "Error saving snapshot info", e)
        }
    }

    private fun readSnapshotInfo(slot: Int): Pair<String, String> {
        val infoFile = getSnapshotInfoFile(slot)
        return if (infoFile.exists()) {
            try {
                val content = infoFile.readText(Charsets.UTF_8).trim()
                val parts = content.split('|', limit = 2)
                if (parts.size == 2) {

                    val gameName = if (parts[0].isNotEmpty() && parts[0] != "Unknown Game") parts[0] else ""
                    Pair(gameName, parts[1])
                } else {
                    Pair("", "")
                }
            } catch (e: Exception) {
                Pair("", "")
            }
        } else {
            Pair("", "") 
        }
    }

    private fun decodeSnapshotThumbnail(slot: Int): Bitmap? {
        val thumbFile = getSnapshotThumbFile(slot)
        if (!thumbFile.exists()) return null

        val bytes = runCatching { thumbFile.readBytes() }.getOrNull() ?: return null
        if (bytes.size < SNAPSHOT_PREVIEW_HEADER_SIZE) {
            return null
        }

        if (bytes[0] != 'X'.code.toByte() ||
            bytes[1] != '1'.code.toByte() ||
            bytes[2] != 'T'.code.toByte() ||
            bytes[3] != 'H'.code.toByte()) {
            return null
        }

        val header = ByteBuffer.wrap(bytes, 4, 8).order(ByteOrder.LITTLE_ENDIAN)
        val version = header.short.toInt() and 0xFFFF
        val width = header.short.toInt() and 0xFFFF
        val height = header.short.toInt() and 0xFFFF
        val channels = header.short.toInt() and 0xFFFF

        if (version != 1 || channels != 4 || width <= 0 || height <= 0) {
            return null
        }

        val pixelBytesLong = width.toLong() * height.toLong() * channels.toLong()
        if (pixelBytesLong <= 0 || pixelBytesLong > Int.MAX_VALUE) {
            return null
        }

        val pixelBytes = pixelBytesLong.toInt()
        if (bytes.size < SNAPSHOT_PREVIEW_HEADER_SIZE + pixelBytes) {
            return null
        }

        val pixels = IntArray(width * height)
        var src = SNAPSHOT_PREVIEW_HEADER_SIZE
        for (y in 0 until height) {
            val dstRow = (height - 1 - y) * width
            for (x in 0 until width) {
                val r = bytes[src].toInt() and 0xFF
                val g = bytes[src + 1].toInt() and 0xFF
                val b = bytes[src + 2].toInt() and 0xFF
                pixels[dstRow + x] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
                src += 4
            }
        }

        return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
    }

    private fun loadSnapshotSlotPreviews(): List<SnapshotSlotPreview> {
        return (1..TOTAL_SNAPSHOT_SLOTS).map { slot ->
            val (gameTitle, timestamp) = readSnapshotInfo(slot)
            val thumbnail = decodeSnapshotThumbnail(slot)
            val hasData = thumbnail != null || gameTitle.isNotEmpty()

            SnapshotSlotPreview(
                slot = slot,
                slotLabel = "Slot $slot",
                gameTitle = gameTitle,
                thumbnail = thumbnail,
                timestamp = timestamp,
                hasData = hasData
            )
        }
    }

    private fun showSaveStateDialog() {
        val previews = loadSnapshotSlotPreviews()

        val dialogView = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
        }

        val title = TextView(this).apply {
            text = "Select Save Slot"
            textSize = 20f
            setPadding(0, 0, 0, 16)
            setTextColor(0xFFFFFFFF.toInt())
        }
        dialogView.addView(title)


        val dialog = MaterialAlertDialogBuilder(this, com.google.android.material.R.style.ThemeOverlay_Material3_MaterialAlertDialog_Centered)
            .setView(dialogView)
            .setNegativeButton("Cancel", null)
            .create()

        for (preview in previews) {
            val slotView = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setBackgroundResource(android.R.drawable.list_selector_background)
            }

            val thumb = ImageView(this).apply {
                layoutParams = LinearLayout.LayoutParams(100, 75).also {
                    it.marginEnd = 16
                }
                scaleType = ImageView.ScaleType.CENTER_CROP
                if (preview.thumbnail != null) {
                    setImageBitmap(preview.thumbnail)
                } else {
                    setImageResource(android.R.drawable.ic_menu_help)
                }
            }
            slotView.addView(thumb)

            val infoLayout = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }

            val slotText = TextView(this).apply {
                text = "Slot ${preview.slot}"
                textSize = 16f
                setTextColor(0xFFFFFFFF.toInt())
            }
            infoLayout.addView(slotText)

            if (preview.hasData && preview.gameTitle.isNotEmpty()) {
                val gameText = TextView(this).apply {
                    text = preview.gameTitle
                    textSize = 14f
                    setTextColor(0xCCCCCCCC.toInt())
                }
                infoLayout.addView(gameText)

                val dateText = TextView(this).apply {
                    text = preview.timestamp ?: ""
                    textSize = 12f
                    setTextColor(0xAAAAAACC.toInt())
                }
                infoLayout.addView(dateText)
            } else {
                val emptyText = TextView(this).apply {
                    text = "Empty"
                    textSize = 14f
                    setTextColor(0xCCCCCCCC.toInt())
                }
                infoLayout.addView(emptyText)
            }

            slotView.addView(infoLayout)

            slotView.setOnClickListener {
                runSnapshotOperation(preview.slot, true)
                dialog.dismiss() 
            }

            dialogView.addView(slotView)
        }

        dialog.show()
    }

    private fun showLoadStateDialog() {
        val previews = loadSnapshotSlotPreviews()

        val dialogView = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
        }

        val title = TextView(this).apply {
            text = "Select Load Slot"
            textSize = 20f
            setPadding(0, 0, 0, 16)
            setTextColor(0xFFFFFFFF.toInt())
        }
        dialogView.addView(title)

        val dialog = MaterialAlertDialogBuilder(this, com.google.android.material.R.style.ThemeOverlay_Material3_MaterialAlertDialog_Centered)
            .setView(dialogView)
            .setNegativeButton("Cancel", null)
            .create()

        for (preview in previews) {
            val slotView = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also {
                    it.bottomMargin = 12
                }
                setBackgroundResource(android.R.drawable.list_selector_background)
            }

            val thumb = ImageView(this).apply {
                layoutParams = LinearLayout.LayoutParams(100, 75).also {
                    it.marginEnd = 16
                }
                scaleType = ImageView.ScaleType.CENTER_CROP
                if (preview.thumbnail != null) {
                    setImageBitmap(preview.thumbnail)
                } else {
                    setImageResource(android.R.drawable.ic_menu_help)
                }
            }
            slotView.addView(thumb)

            val infoLayout = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }

            val slotText = TextView(this).apply {
                text = "Slot ${preview.slot}"
                textSize = 16f
                setTextColor(0xFFFFFFFF.toInt())
            }
            infoLayout.addView(slotText)

            if (preview.hasData) {
                if (preview.gameTitle.isNotEmpty()) {
                    val gameText = TextView(this).apply {
                        text = preview.gameTitle
                        textSize = 14f
                        setTextColor(0xCCCCCCCC.toInt())
                    }
                    infoLayout.addView(gameText)
                }

                val dateText = TextView(this).apply {
                    text = preview.timestamp ?: ""
                    textSize = 12f
                    setTextColor(0xAAAAAACC.toInt())
                }
                infoLayout.addView(dateText)

                slotView.addView(infoLayout)

                slotView.setOnClickListener {
                    runSnapshotOperation(preview.slot, false)
                    dialog.dismiss() 
                }
            } else {
                val emptyText = TextView(this).apply {
                    text = "Empty - Cannot Load"
                    textSize = 14f
                    setTextColor(0xCCCCCCCC.toInt())
                }
                infoLayout.addView(emptyText)
                slotView.addView(infoLayout)


                slotView.isEnabled = false
                slotView.alpha = 0.5f
            }

            dialogView.addView(slotView)
        }

        dialog.show()
    }

    private fun runSnapshotOperation(slot: Int, save: Boolean) {
        Thread {
            val snapshotName = "${currentGameId}_slot_$slot"
            val ok = if (save) {
                nativeSaveSnapshot(snapshotName)
            } else {
                nativeLoadSnapshot(snapshotName)
            }

            runOnUiThread {
                if (ok) {
                    if (save) {
                        saveSnapshotInfo(slot)
                        Toast.makeText(this@MainActivity, "Saved to Slot $slot ✓", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(this@MainActivity, "Loaded from Slot $slot ✓", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    Toast.makeText(this@MainActivity, "Operation Failed ✗", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    override fun getLibraries(): Array<String> = arrayOf(
        "SDL2",
        "xemu"
    )
}

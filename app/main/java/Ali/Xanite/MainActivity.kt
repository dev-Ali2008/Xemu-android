package Ali.Xanite

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Typeface
import android.hardware.input.InputManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.util.TypedValue
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.view.ViewConfiguration
import android.widget.BaseAdapter
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.RelativeLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.core.widget.NestedScrollView
import com.google.android.material.button.MaterialButton
import androidx.appcompat.app.AlertDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLControllerManager
import org.libsdl.app.SDLSurface
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.max

class MainActivity : SDLActivity(), InputManager.InputDeviceListener {
  companion object {
    const val EXTRA_AUTO_LOAD_SNAPSHOT_SLOT = "Ali.Xanite.extra.AUTO_LOAD_SNAPSHOT_SLOT"
    private const val SNAPSHOT_PREVIEW_HEADER_SIZE = 12
    private const val TOTAL_SNAPSHOT_SLOTS = 10
    private const val TAG = "MainActivity"

    /**
     * Upper bound for the log flushes on the lifecycle callbacks below. These
     * run on the main thread, so the budget is deliberately far smaller than
     * the crash handler's — persisting the log tail must never become the
     * reason the app is reported as unresponsive.
     */
    private const val LIFECYCLE_LOG_FLUSH_TIMEOUT_MS = 200L
  }

  private enum class PendingCloseAction {
    NONE,
    RETURN_TO_LIBRARY,
    QUIT_APP,
  }

  private data class SnapshotSlotPreview(
    val slot: Int,
    val slotLabel: String,
    val gameTitle: String,
    val thumbnail: Bitmap?,
  )

  private var onScreenController: OnScreenController? = null
  private var controllerBridge: ControllerInputBridge? = null
  private var isControllerVisible = false
  private var virtualControllerRegistered = false
  private var inputManager: InputManager? = null
  private var hasPhysicalController = false
  private var inGameMenuDialog: AlertDialog? = null
  private var startButtonDown = false
  private var selectButtonDown = false
  private var comboTriggered = false
  private var suspendedByLifecycle = false
  private var resumeEmulationOnMenuDismiss = false
  private var startupSnapshotSlot: Int? = null
  private var startupSnapshotLoadScheduled = false
  private var pendingCloseAction = PendingCloseAction.NONE
  private lateinit var swipeUpGestureRecognizer: SwipeUpGestureRecognizer
  private var fpsTextView: TextView? = null
  private var validationTextView: TextView? = null
  private val fpsHandler = Handler(Looper.getMainLooper())
  private val fpsUpdateInterval = 1000L
  private val fpsRunnable = object : Runnable {
    override fun run() {
      val fps = nativeGetFps()
      fpsTextView?.text = "FPS: $fps"
      // Numeric FPS in logcat so perf A/B runs can be sampled from adb instead
      // of OCR'ing the overlay out of screenshots. Not gated on DEBUG: this
      // runnable only ticks while the user has the FPS overlay switched on, so
      // it is already opt-in, and release builds are the ones worth measuring
      // (debug is far slower and its numbers mean nothing).
      android.util.Log.i("xanite-fps", "fps=$fps")
      fpsHandler.postDelayed(this, fpsUpdateInterval)
    }
  }

  override fun loadLibraries() {
    super.loadLibraries()
    initializeGpuDriver()
  }

  private fun initializeGpuDriver() {
    GpuDriverHelper.init(this)
    if (!GpuDriverHelper.supportsCustomDriverLoading()) {
      android.util.Log.i(TAG, "GPU driver: custom loading not supported on this device")
      return
    }
    val override = PerGameSettingsManager.getRuntimeOverride(this, "setting_gpu_driver")
    when {
      override == null -> {
        // No per-game override — global behavior
        val driverLib = GpuDriverHelper.getInstalledDriverLibrary()
        if (driverLib != null) {
          android.util.Log.i(TAG, "GPU driver: loading custom driver=$driverLib")
          GpuDriverHelper.initializeDriver(driverLib)
        } else {
          android.util.Log.i(TAG, "GPU driver: no custom driver installed, initializing system driver via adrenotools")
          GpuDriverHelper.initializeDriver()
        }
      }
      override == "system" -> {
        android.util.Log.i(TAG, "GPU driver: per-game override → system driver")
        GpuDriverHelper.initializeDriver()
      }
      else -> {
        // ZIP basename of a specific driver in storage dir
        val zipFile = java.io.File(GpuDriverHelper.driverStorageDir, override)
        if (zipFile.exists() && GpuDriverHelper.installDriver(zipFile)) {
          val libName = GpuDriverHelper.getInstalledDriverLibrary()
          android.util.Log.i(TAG, "GPU driver: per-game override → $override (lib=$libName)")
          GpuDriverHelper.initializeDriver(libName)
        } else {
          android.util.Log.w(TAG, "GPU driver: per-game $override not found or install failed, falling back to system")
          GpuDriverHelper.initializeDriver()
        }
      }
    }
  }

  override fun createSDLSurface(context: Context): SDLSurface {
    return super.createSDLSurface(context).apply {
      layoutParams = RelativeLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT
      )
      keepScreenOn = true
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setupBootCoverOverlay()
    DebugLog.initialize(this)
    // SDLActivity.onCreate() above has already loaded the native libraries, so
    // their load addresses are final. Record them now: a native crash can only
    // report raw PCs, and the map needed to resolve them dies with the process.
    CrashReportManager.captureMemoryMapSnapshot(this)
    // The Vulkan driver is loaded later, when the renderer starts, so take a
    // second snapshot once the session is up or its frames stay unresolvable.
    CrashReportManager.scheduleMemoryMapRefresh(this)
    ControllerSettings.applyVisibleByDefaultMigration(this)
    val prefs = getSharedPreferences("xaniteog_prefs", Context.MODE_PRIVATE)
    val rendererPref = PerGameSettingsManager.getRuntimeOverride(this, "setting_renderer")
      ?: prefs.getString("setting_renderer", "vulkan")
      ?: "vulkan"
    nativeSetenv("XEMU_RENDERER", if (rendererPref == "opengl") "opengl" else "vulkan")
    nativeSetenv("XEMU_ANDROID_DEV_LOGS", if (CrashReportManager.isActiveDevLogsEnabled(this)) "1" else "0")
    // Vulkan validation layer: debug builds only (the release variant does not
    // package the layer at all), and only while debug logging is on, since it
    // costs real performance. It reports descriptor-set lifetime and threading
    // violations, which no amount of value-checking in our own code can see.
    val debugLogsOn = prefs.getBoolean(DebugLog.PREF_ENABLED, false)
    // The Vulkan validation layer only ships in debug builds.
    val validationOn = BuildConfig.VULKAN_VALIDATION_AVAILABLE && debugLogsOn
    nativeSetenv("XEMU_VK_VALIDATION", if (validationOn) "1" else "0")
    // Per-draw glGetError() probes in the GL renderer. Deliberately keyed off
    // the debug-logging preference alone, NOT off validationOn: the GL probes
    // need no bundled layer, so tying them to VULKAN_VALIDATION_AVAILABLE would
    // make them impossible to enable in a release build - which is exactly
    // where a user reporting a rendering bug is running. Always-on they cost
    // thousands of driver syncs per frame, hence the switch.
    nativeSetenv("XEMU_GL_DEBUG", if (debugLogsOn) "1" else "0")
    DebugLog.i(TAG) { "Vulkan validation layer: ${if (validationOn) "ENABLED" else "off"}" }
    OrientationLocker(this, landscapeOnly = true).enable()
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    val requestedSlot = intent?.getIntExtra(EXTRA_AUTO_LOAD_SNAPSHOT_SLOT, 0) ?: 0
    if (requestedSlot in 1..TOTAL_SNAPSHOT_SLOTS) {
      startupSnapshotSlot = requestedSlot
    }
    initializeSwipeMenuGesture()
    setupOnScreenController()
    setupFpsOverlay()
    setupValidationOverlay()
    setupControllerDetection()
    hideSystemUI()
  }

  override fun onWindowFocusChanged(hasFocus: Boolean) {
    super.onWindowFocusChanged(hasFocus)
    if (hasFocus) {
      hideSystemUI()
      updateFpsOverlayPosition()
    } else {
      // Release all on-screen inputs when the window loses focus (e.g. a system
      // gesture panel, notification shade, or dialog appears). Without this,
      // triggers can stay "pressed" if the touch UP event is never delivered.
      onScreenController?.resetAllInputs()
      controllerBridge?.reset()
    }
  }

  override fun handleBackNavigation() {
    val currentDialog = inGameMenuDialog
    if (currentDialog?.isShowing == true) {
      currentDialog.dismiss()
      return
    }
    showInGameMenu()
  }

  override fun dispatchKeyEvent(event: KeyEvent): Boolean {
    if (event.keyCode == KeyEvent.KEYCODE_BACK && !isGamepadKeyEvent(event)) {
      if (event.action == KeyEvent.ACTION_UP && event.repeatCount == 0) {
        val currentDialog = inGameMenuDialog
        if (currentDialog?.isShowing == true) {
          currentDialog.dismiss()
        } else {
          showInGameMenu()
        }
      }
      return true
    }

    if (handleGamepadMenuCombo(event)) {
      return true
    }
    return super.dispatchKeyEvent(event)
  }

  override fun dispatchTouchEvent(event: MotionEvent): Boolean {
    if (shouldHandleActivitySwipeMenu() && swipeUpGestureRecognizer.onTouchEvent(event)) {
      return true
    }
    if (!shouldHandleActivitySwipeMenu()) {
      swipeUpGestureRecognizer.reset()
    }
    return super.dispatchTouchEvent(event)
  }

  private fun hideSystemUI() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      // Android 11 (API 30) and above
      @Suppress("DEPRECATION")
      window.setDecorFitsSystemWindows(false)
      window.insetsController?.let { controller ->
        controller.hide(WindowInsets.Type.systemBars())
        controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
      }
    } else {
      // Android 10 and below
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
  }

  private fun initializeSwipeMenuGesture() {
    val swipeTouchSlop = ViewConfiguration.get(this).scaledTouchSlop.toFloat()
    swipeUpGestureRecognizer = SwipeUpGestureRecognizer(
      minDistancePx = {
        max(currentGestureHostHeight() * 0.14f, swipeTouchSlop * 8f)
      },
      touchSlopPx = { swipeTouchSlop },
      canStartAt = { _, y ->
        y >= currentGestureHostHeight() * 0.35f
      },
      onTriggered = {
        onScreenController?.resetAllInputs()
        showInGameMenu()
      },
    )
  }

  private fun setupOnScreenController() {
    val prefs = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
    onScreenController = OnScreenController(this).apply {
      layoutParams = FrameLayout.LayoutParams(
        FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT
      )
      alpha = prefs.getFloat("setting_controller_opacity", 0.85f).coerceIn(0.25f, 1f)
      scaleFactor = prefs.getFloat("setting_controller_scale", 1.0f)
      vibrationEnabled = prefs.getBoolean("setting_vibration", true)
    }

    // Create input bridge
    controllerBridge = ControllerInputBridge()
    onScreenController?.setControllerListener(controllerBridge!!)
    onScreenController?.onMenuButtonTapped = { showInGameMenu() }

    // Add to layout
    mLayout?.addView(onScreenController)

    // Check for existing controllers and show/hide accordingly
    updateControllerVisibility()
  }

  private var bootCoverView: View? = null
  private val bootCoverHandler = Handler(Looper.getMainLooper())
  private val bootCoverTimeoutRunnable = Runnable { hideBootCover() }

  /**
   * Solid black overlay shown from the instant this Activity is created,
   * covering whatever the display surface shows before the guest has
   * actually rendered anything — nv2a's framebuffer starts out with
   * uninitialized VRAM content, and some BIOS/MCPX combinations briefly
   * show static/noise before their boot animation even on real hardware.
   * Removed the moment xemu_android.cpp's native code reports the first
   * real hardware-rendered frame via onFirstHardwareFrameRendered() below.
   *
   * The 20s fallback timeout means a genuine boot stall (nv2a never
   * engaging at all) just ends up showing a plain black screen — the same
   * thing you'd see without this overlay — rather than getting stuck
   * behind a cover that never lifts.
   */
  private fun setupBootCoverOverlay() {
    val cover = View(this).apply {
      setBackgroundColor(Color.BLACK)
    }
    val params = RelativeLayout.LayoutParams(
      RelativeLayout.LayoutParams.MATCH_PARENT,
      RelativeLayout.LayoutParams.MATCH_PARENT
    )
    mLayout?.addView(cover, params)
    bootCoverView = cover
    bootCoverHandler.postDelayed(bootCoverTimeoutRunnable, 20_000L)
  }

  private fun hideBootCover() {
    bootCoverHandler.removeCallbacks(bootCoverTimeoutRunnable)
    val cover = bootCoverView ?: return
    bootCoverView = null
    cover.animate()
      .alpha(0f)
      .setDuration(200)
      .withEndAction { cover.visibility = View.GONE }
      .start()
  }

  /**
   * Called from native code (xemu_android.cpp, via JNI) the instant nv2a
   * produces its first real frame. This runs on the emulator's render
   * thread, not the UI thread, so the actual view work is dispatched back
   * via runOnUiThread — touching a View off the main thread would crash.
   */
  fun onFirstHardwareFrameRendered() {
    runOnUiThread { hideBootCover() }
  }

  private fun setupFpsOverlay() {
    fpsTextView = TextView(this).apply {
      text = "FPS: --"
      setTextColor(ContextCompat.getColor(this@MainActivity, R.color.xaniteog_green))
      setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
      typeface = Typeface.MONOSPACE
      setShadowLayer(2f, 1f, 1f, Color.BLACK)
      setPadding(16, 8, 16, 8)
      setBackgroundColor(Color.argb(100, 0, 0, 0))
      maxLines = 1
      visibility = View.GONE
    }
    val params = RelativeLayout.LayoutParams(
      RelativeLayout.LayoutParams.WRAP_CONTENT,
      RelativeLayout.LayoutParams.WRAP_CONTENT
    ).apply {
      addRule(RelativeLayout.ALIGN_PARENT_TOP)
      addRule(RelativeLayout.CENTER_HORIZONTAL)
    }
    mLayout?.addView(fpsTextView, params)
    updateFpsOverlayPosition()
  }

  /**
   * Top-left readout of the Vulkan validation layer state.
   *
   * Reports what is actually true rather than what was requested: the layer
   * only does anything if the .so is really packaged in this build AND debug
   * logging is on, and those fail independently. Without this you cannot tell
   * a clean run from one where the layer silently never loaded.
   *
   * Debug builds only - there is no layer in a release artifact to report on.
   */
  private fun setupValidationOverlay() {
    if (!BuildConfig.VULKAN_VALIDATION_AVAILABLE) {
      return
    }

    val layerPresent = runCatching {
      File(applicationInfo.nativeLibraryDir, "libVkLayer_khronos_validation.so").isFile
    }.getOrDefault(false)
    val debugLogsOn = getSharedPreferences("xaniteog_prefs", Context.MODE_PRIVATE)
      .getBoolean(DebugLog.PREF_ENABLED, false)

    val (label, colour) = when {
      !layerPresent -> "VALIDATION: MISSING (layer not in build)" to Color.RED
      !debugLogsOn -> "VALIDATION: OFF (enable Debug Logs)" to Color.YELLOW
      else -> "VALIDATION: ON" to Color.GREEN
    }
    DebugLog.i(TAG) { "Validation overlay: $label" }

    validationTextView = TextView(this).apply {
      text = label
      setTextColor(colour)
      setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
      typeface = Typeface.MONOSPACE
      setShadowLayer(2f, 1f, 1f, Color.BLACK)
      setPadding(16, 8, 16, 8)
      setBackgroundColor(Color.argb(140, 0, 0, 0))
      maxLines = 1
    }
    val params = RelativeLayout.LayoutParams(
      RelativeLayout.LayoutParams.WRAP_CONTENT,
      RelativeLayout.LayoutParams.WRAP_CONTENT,
    ).apply {
      addRule(RelativeLayout.ALIGN_PARENT_TOP)
      addRule(RelativeLayout.ALIGN_PARENT_LEFT)
      topMargin = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP, 8f, resources.displayMetrics
      ).toInt()
      leftMargin = topMargin
    }
    mLayout?.addView(validationTextView, params)
  }

  private fun updateFpsOverlayPosition() {
    val fpsView = fpsTextView ?: return

    fpsView.post {
      val topMargin = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP,
        8f,
        resources.displayMetrics
      ).toInt()

      val params = fpsView.layoutParams as? RelativeLayout.LayoutParams ?: return@post
      params.leftMargin = 0
      params.topMargin = topMargin
      fpsView.layoutParams = params
    }
  }

  override fun onResume() {
    super.onResume()
    if (suspendedByLifecycle) {
      nativeResumeEmulation()
      suspendedByLifecycle = false
    }
    OrientationLocker(this, landscapeOnly = true).enable()
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    mLayout?.keepScreenOn = true
    
    // Register virtual controller after SDL is initialized
    // Use a delay to ensure SDL is fully ready
    mLayout?.postDelayed({
      registerVirtualController()
    }, 1000)

    val showFps = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
      .getBoolean("show_fps", false)
    fpsTextView?.visibility = if (showFps) View.VISIBLE else View.GONE
    fpsHandler.removeCallbacks(fpsRunnable)
    if (showFps) {
      fpsHandler.postDelayed(fpsRunnable, fpsUpdateInterval)
    }

    updateFpsOverlayPosition()
    scheduleStartupSnapshotLoadIfRequested()
  }

  override fun onPause() {
    fpsHandler.removeCallbacks(fpsRunnable)
    swipeUpGestureRecognizer.reset()
    onScreenController?.resetAllInputs()
    controllerBridge?.reset()
    resumeEmulationOnMenuDismiss = false
    suspendedByLifecycle = true
    nativePauseEmulation()
    super.onPause()
  }

  private fun resumeEmulationIfSafe() {
    val destroyed =
      Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1 && isDestroyed
    if (!suspendedByLifecycle && !isFinishing && !destroyed) {
      nativeResumeEmulation()
    }
  }

  private fun scheduleStartupSnapshotLoadIfRequested() {
    val slot = startupSnapshotSlot ?: return
    if (startupSnapshotLoadScheduled) {
      return
    }
    startupSnapshotLoadScheduled = true

    val hostView = mLayout ?: window.decorView
    hostView.postDelayed({
      Thread {
        val ok = nativeLoadSnapshot(slotName(slot))
        runOnUiThread {
          if (ok) {
            writeSnapshotTitleFallback(slot)
          }
          val msg = if (ok) {
            getString(R.string.snapshot_loaded, slot)
          } else {
            getString(R.string.snapshot_load_failed, slot)
          }
          Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        }
      }.start()
      startupSnapshotSlot = null
    }, 2500)
  }

  private fun registerVirtualController() {
    if (virtualControllerRegistered) return
    try {
      // Register the virtual on-screen controller as a joystick device
      // Device ID: -2, Name: "On-Screen Controller"
      SDLControllerManager.nativeAddJoystick(
        -2, // device_id
        "On-Screen Controller", // name
        "Virtual touchscreen controller", // desc
        0x045e, // vendor_id (Microsoft)
        0x028e, // product_id (Xbox 360 Controller)
        false, // is_accelerometer
        0xFFFF, // button_mask (all buttons)
        6, // naxes (left X/Y, right X/Y, left trigger, right trigger)
        0x3F, // axis_mask (6 axes)
        0, // nhats
        0  // nballs
      )
      virtualControllerRegistered = true
      DebugLog.d("MainActivity") { "Virtual controller registered successfully" }
    } catch (e: Exception) {
      DebugLog.e("MainActivity", e) { "Failed to register virtual controller: ${e.message}" }
    }
  }

  private fun setupControllerDetection() {
    inputManager = getSystemService(Context.INPUT_SERVICE) as InputManager
    inputManager?.registerInputDeviceListener(this, null)
    
    // Check for already connected controllers
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
    
    // Check if device is a gamepad or joystick
    return ((sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
           ((sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
  }

  private fun updateControllerVisibility() {
    val enabledByUser = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
      .getBoolean(ControllerSettings.KEY_SHOW_CONTROLLER, true)
    // The on-screen controller is visible by default and remains controlled
    // by the explicit setting. Connecting a physical controller must not
    // silently override that choice; users can hide the overlay themselves.
    val shouldShow = enabledByUser

    // Always apply the state to the newly-created View. Android Views start
    // VISIBLE while isControllerVisible starts false, so the old transition-
    // only check ignored a saved "hidden" preference on every game launch.
    isControllerVisible = shouldShow
    swipeUpGestureRecognizer.reset()
    onScreenController?.visibility = if (shouldShow) View.VISIBLE else View.GONE
  }

  // InputDeviceListener callbacks
  override fun onInputDeviceAdded(deviceId: Int) {
    val device = inputManager?.getInputDevice(deviceId)
    if (isGameController(device)) {
      hasPhysicalController = true
      updateControllerVisibility()
    }
  }

  override fun onInputDeviceRemoved(deviceId: Int) {
    // Recheck all devices to see if any controllers remain
    checkForPhysicalControllers()
  }

  override fun onInputDeviceChanged(deviceId: Int) {
    // Recheck all devices in case configuration changed
    checkForPhysicalControllers()
  }

  override fun onDestroy() {
    DebugLog.i(TAG) { "onDestroy()" }
    // The emulator process can be killed outright once this activity goes
    // away (low memory, native crash, user swipe). Get whatever is buffered
    // onto disk while we still have a thread to do it on. Short timeout:
    // this runs on the main thread and must not risk an ANR.
    DebugLog.flushBlocking(LIFECYCLE_LOG_FLUSH_TIMEOUT_MS)
    pendingCloseAction = PendingCloseAction.NONE
    fpsHandler.removeCallbacks(fpsRunnable)
    swipeUpGestureRecognizer.reset()
    resumeEmulationOnMenuDismiss = false
    inGameMenuDialog?.dismiss()
    inGameMenuDialog = null

    inputManager?.unregisterInputDeviceListener(this)
    if (virtualControllerRegistered) {
      runCatching { SDLControllerManager.nativeRemoveJoystick(-2) }
      virtualControllerRegistered = false
    }
    window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    super.onDestroy()
  }

  override fun onUserLeaveHint() {
    DebugLog.i(TAG) { "onUserLeaveHint()" }
    super.onUserLeaveHint()
  }

  override fun onTrimMemory(level: Int) {
    DebugLog.i(TAG) { "onTrimMemory(level=$level)" }
    // Memory pressure is a common precursor to the process being killed
    // without any further callback, so persist the log tail now.
    DebugLog.flushBlocking(LIFECYCLE_LOG_FLUSH_TIMEOUT_MS)
    super.onTrimMemory(level)
  }

  override fun onLowMemory() {
    DebugLog.w(TAG) { "onLowMemory()" }
    DebugLog.flushBlocking(LIFECYCLE_LOG_FLUSH_TIMEOUT_MS)
    super.onLowMemory()
  }

  // Manual control methods (for settings/preferences)
  fun toggleOnScreenController() {
    isControllerVisible = !isControllerVisible
    onScreenController?.visibility = if (isControllerVisible) View.VISIBLE else View.GONE
  }

  fun showOnScreenController() {
    isControllerVisible = true
    onScreenController?.visibility = View.VISIBLE
  }

  fun hideOnScreenController() {
    isControllerVisible = false
    onScreenController?.visibility = View.GONE
  }

  fun forceUpdateControllerVisibility() {
    checkForPhysicalControllers()
  }

  private fun handleGamepadMenuCombo(event: KeyEvent): Boolean {
    if (!isGamepadKeyEvent(event)) {
      return false
    }

    val isStartKey = event.keyCode == KeyEvent.KEYCODE_BUTTON_START
    val isSelectKey = event.keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
      event.keyCode == KeyEvent.KEYCODE_BACK
    if (!isStartKey && !isSelectKey) {
      return false
    }

    when (event.action) {
      KeyEvent.ACTION_DOWN -> {
        if (isStartKey) {
          startButtonDown = true
        }
        if (isSelectKey) {
          selectButtonDown = true
        }

        if (!comboTriggered && event.repeatCount == 0 &&
          startButtonDown && selectButtonDown) {
          comboTriggered = true
          showInGameMenu()
          return true
        }
      }
      KeyEvent.ACTION_UP -> {
        if (isStartKey) {
          startButtonDown = false
        }
        if (isSelectKey) {
          selectButtonDown = false
        }
        if (!startButtonDown || !selectButtonDown) {
          comboTriggered = false
        }
      }
    }

    return comboTriggered
  }

  private fun isGamepadKeyEvent(event: KeyEvent): Boolean {
    val source = event.source
    return ((source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
      ((source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
  }

  private external fun nativeSaveSnapshot(name: String): Boolean
  private external fun nativeLoadSnapshot(name: String): Boolean
  private external fun nativeRebootSystem()
  private external fun nativeGetFps(): Int
  private external fun nativePauseEmulation()
  private external fun nativeResumeEmulation()
  private external fun nativeSetReturnToLibraryOnExit(enable: Boolean)
  private external fun nativeExitEmulation()

  private fun slotName(slot: Int) = "android_slot_$slot"

  private fun snapshotPreviewDir(): File = File(filesDir, "x1box/snapshots")

  private fun snapshotPreviewDirs(): List<File> {
    val dirs = ArrayList<File>(2)
    dirs.add(snapshotPreviewDir())
    getExternalFilesDir(null)?.let { dirs.add(File(it, "x1box/snapshots")) }
    return dirs.distinctBy { it.absolutePath }
  }

  private fun slotNameAliases(slot: Int): List<String> {
    val aliases = linkedSetOf(
      slotName(slot),
      "slot_$slot",
      "slot$slot",
      "snapshot_$slot",
    )
    return aliases.toList()
  }

  private fun resolveSnapshotPreviewFile(slot: Int, extension: String): File? {
    for (dir in snapshotPreviewDirs()) {
      for (name in slotNameAliases(slot)) {
        val file = File(dir, "$name.$extension")
        if (file.isFile) {
          return file
        }
      }
    }
    return null
  }

  private fun snapshotPreviewTitleFile(slot: Int): File =
    File(snapshotPreviewDir(), "${slotName(slot)}.title")

  private fun extractDisplayName(rawName: String?): String? {
    if (rawName.isNullOrBlank()) {
      return null
    }
    val decoded = Uri.decode(rawName)
    val leaf = decoded.substringAfterLast('/').substringAfterLast(':')
    if (leaf.isBlank()) {
      return null
    }
    val stem = leaf.substringBeforeLast('.', leaf).trim()
    return stem.takeIf { it.isNotEmpty() }
  }

  private fun fallbackCurrentGameName(): String {
    val prefs = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
    val pathName = extractDisplayName(prefs.getString("dvdPath", null)?.let { File(it).name })
    if (!pathName.isNullOrEmpty()) {
      return pathName
    }
    val uriName = extractDisplayName(prefs.getString("dvdUri", null))
    if (!uriName.isNullOrEmpty()) {
      return uriName
    }
    return getString(R.string.snapshot_unknown_game)
  }

  private fun writeSnapshotTitleFallback(slot: Int) {
    val file = snapshotPreviewTitleFile(slot)
    if (runCatching { file.exists() && file.readText(Charsets.UTF_8).trim().isNotEmpty() }.getOrDefault(false)) {
      return
    }

    val title = fallbackCurrentGameName().trim()
    if (title.isEmpty() || title == getString(R.string.snapshot_unknown_game)) {
      return
    }

    runCatching {
      val dir = snapshotPreviewDir()
      if (!dir.exists()) {
        dir.mkdirs()
      }
      file.writeText(title, Charsets.UTF_8)
    }
  }

  private fun readSnapshotGameTitle(slot: Int): String {
    val title = runCatching {
      val file = resolveSnapshotPreviewFile(slot, "title")
      if (file != null && file.exists()) {
        file.readText(Charsets.UTF_8).trim()
      } else {
        ""
      }
    }.getOrDefault("")

    if (title.isNotEmpty()) {
      return title
    }

    return if (resolveSnapshotPreviewFile(slot, "thm") != null) {
      fallbackCurrentGameName()
    } else {
      getString(R.string.snapshot_empty_slot)
    }
  }

  private fun decodeSnapshotThumbnail(slot: Int): Bitmap? {
    val sourceFile = resolveSnapshotPreviewFile(slot, "thm") ?: return null
    val bytes = runCatching { sourceFile.readBytes() }.getOrNull() ?: return null
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
      SnapshotSlotPreview(
        slot = slot,
        slotLabel = getString(R.string.snapshot_slot_label, slot),
        gameTitle = readSnapshotGameTitle(slot),
        thumbnail = decodeSnapshotThumbnail(slot),
      )
    }
  }

  private fun showSnapshotPreviewDialog(preview: SnapshotSlotPreview) {
    val bitmap = preview.thumbnail ?: return
    val image = ImageView(this).apply {
      setImageBitmap(bitmap)
      adjustViewBounds = true
      scaleType = ImageView.ScaleType.FIT_CENTER
      setPadding(16, 16, 16, 16)
    }

    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(getString(R.string.snapshot_preview_title, preview.slot, preview.gameTitle))
      .setView(image)
      .setPositiveButton(android.R.string.ok, null)
      .show()
  }

  private fun runSnapshotOperation(slot: Int, save: Boolean) {
    Thread {
      val ok = if (save) nativeSaveSnapshot(slotName(slot)) else nativeLoadSnapshot(slotName(slot))
      runOnUiThread {
        if (ok) {
          writeSnapshotTitleFallback(slot)
        }
        val msg = if (save) {
          if (ok) getString(R.string.snapshot_saved, slot) else getString(R.string.snapshot_save_failed)
        } else {
          if (ok) getString(R.string.snapshot_loaded, slot) else getString(R.string.snapshot_load_failed, slot)
        }
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        hideSystemUI()
        resumeEmulationIfSafe()
      }
    }.start()
  }

  private fun showSnapshotSlotDialog(save: Boolean) {
    val previews = loadSnapshotSlotPreviews()
    val listView = ListView(this)
    lateinit var dialog: AlertDialog
    var operationStarted = false

    val adapter = object : BaseAdapter() {
      override fun getCount(): Int = previews.size
      override fun getItem(position: Int): SnapshotSlotPreview = previews[position]
      override fun getItemId(position: Int): Long = previews[position].slot.toLong()

      override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val view = convertView ?: layoutInflater.inflate(R.layout.item_snapshot_slot, parent, false)
        val preview = getItem(position)

        val slotLabel = view.findViewById<TextView>(R.id.snapshot_slot_label)
        val gameTitle = view.findViewById<TextView>(R.id.snapshot_game_title)
        val previewHint = view.findViewById<TextView>(R.id.snapshot_preview_hint)
        val thumbnail = view.findViewById<ImageView>(R.id.snapshot_thumbnail)

        slotLabel.text = preview.slotLabel
        gameTitle.text = preview.gameTitle

        if (preview.thumbnail != null) {
          thumbnail.setImageBitmap(preview.thumbnail)
          previewHint.text = getString(R.string.snapshot_preview_tap_hint)
          previewHint.visibility = View.VISIBLE
          thumbnail.setOnClickListener {
            showSnapshotPreviewDialog(preview)
          }
        } else {
          thumbnail.setImageResource(R.drawable.ic_xaniteog_image_placeholder)
          previewHint.text = getString(R.string.snapshot_preview_unavailable)
          previewHint.visibility = View.VISIBLE
          thumbnail.setOnClickListener(null)
        }

        return view
      }
    }

    listView.adapter = adapter
    listView.setOnItemClickListener { _, _, position, _ ->
      val slot = previews[position].slot
      operationStarted = true
      dialog.dismiss()
      runSnapshotOperation(slot, save)
    }

    dialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(
        if (save) getString(R.string.snapshot_select_save_slot)
        else getString(R.string.snapshot_select_load_slot)
      )
      .setView(listView)
      .setNegativeButton(android.R.string.cancel, null)
      .setOnDismissListener {
        hideSystemUI()
        if (!operationStarted) {
          resumeEmulationIfSafe()
        }
      }
      .create()

    dialog.show()
  }

  private fun showSaveStateDialog() {
    showSnapshotSlotDialog(save = true)
  }

  private fun showLoadStateDialog() {
    showSnapshotSlotDialog(save = false)
  }

  private fun showRebootSystemConfirmation() {
    var confirmed = false
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.in_game_menu_reboot_title)
      .setMessage(R.string.in_game_menu_reboot_message)
      .setPositiveButton(R.string.in_game_menu_reboot_action) { _, _ ->
        confirmed = true
        onScreenController?.resetAllInputs()
        nativeRebootSystem()
      }
      .setNegativeButton(android.R.string.cancel, null)
      .setOnDismissListener {
        hideSystemUI()
        if (!confirmed) {
          resumeEmulationIfSafe()
        }
      }
      .show()
  }

  private fun showInGameMenu() {
    swipeUpGestureRecognizer.reset()
    if (inGameMenuDialog?.isShowing == true) {
      return
    }
    nativePauseEmulation()
    resumeEmulationOnMenuDismiss = true

    val dp = resources.displayMetrics.density
    val verticalButtonSpacing = (8 * dp).toInt()
    val horizontalButtonSpacing = (8 * dp).toInt()
    lateinit var dialog: androidx.appcompat.app.AlertDialog
    data class MenuButtonSpec(
      val label: String,
      val resumeAfterDismiss: Boolean = true,
      val action: () -> Unit,
    )

    fun createMenuButton(spec: MenuButtonSpec): MaterialButton {
      return MaterialButton(
        this@MainActivity,
        null,
        com.google.android.material.R.attr.materialButtonOutlinedStyle
      ).apply {
        text = spec.label
        gravity = Gravity.CENTER
        textAlignment = View.TEXT_ALIGNMENT_CENTER
        isSingleLine = false
        maxLines = 2
        setOnClickListener {
          resumeEmulationOnMenuDismiss = spec.resumeAfterDismiss
          dialog.dismiss()
          spec.action()
        }
      }
    }

    fun addSingleButton(parent: LinearLayout, spec: MenuButtonSpec) {
      parent.addView(createMenuButton(spec).apply {
        layoutParams = LinearLayout.LayoutParams(
          LinearLayout.LayoutParams.MATCH_PARENT,
          LinearLayout.LayoutParams.WRAP_CONTENT
        ).also { lp ->
          lp.bottomMargin = verticalButtonSpacing
        }
      })
    }

    fun addButtonRow(parent: LinearLayout, left: MenuButtonSpec, right: MenuButtonSpec) {
      parent.addView(
        LinearLayout(this).apply {
          orientation = LinearLayout.HORIZONTAL
          layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
          ).also { lp ->
            lp.bottomMargin = verticalButtonSpacing
          }
          addView(createMenuButton(left).apply {
            layoutParams = LinearLayout.LayoutParams(
              0,
              LinearLayout.LayoutParams.WRAP_CONTENT,
              1f
            ).also { lp ->
              lp.marginEnd = horizontalButtonSpacing
            }
          })
          addView(createMenuButton(right).apply {
            layoutParams = LinearLayout.LayoutParams(
              0,
              LinearLayout.LayoutParams.WRAP_CONTENT,
              1f
            )
          })
        }
      )
    }

    val resumeButton = MenuButtonSpec(getString(R.string.in_game_menu_resume)) { /* Resume */ }
    val touchControlsButton = MenuButtonSpec(
      if (isControllerVisible) {
        getString(R.string.in_game_menu_hide_touch_controls)
      } else {
        getString(R.string.in_game_menu_show_touch_controls)
      }
    ) {
      toggleOnScreenController()
    }
    val saveStateButton = MenuButtonSpec(
      getString(R.string.in_game_menu_save_state),
      resumeAfterDismiss = false
    ) {
      showSaveStateDialog()
    }
    val loadStateButton = MenuButtonSpec(
      getString(R.string.in_game_menu_load_state),
      resumeAfterDismiss = false
    ) {
      showLoadStateDialog()
    }
    val rebootButton = MenuButtonSpec(
      getString(R.string.in_game_menu_reboot_system),
      resumeAfterDismiss = false
    ) {
      showRebootSystemConfirmation()
    }
    val exitToLibraryButton = MenuButtonSpec(
      getString(R.string.in_game_menu_exit_to_library),
      resumeAfterDismiss = false
    ) {
      exitToGameLibrary()
    }
    val quitAppButton = MenuButtonSpec(
      getString(R.string.in_game_menu_quit_app),
      resumeAfterDismiss = false
    ) {
      quitApp()
    }

    val buttonList = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding((20 * dp).toInt(), (12 * dp).toInt(), (20 * dp).toInt(), 0)
      addSingleButton(this, resumeButton)
      addSingleButton(this, touchControlsButton)
      addButtonRow(this, saveStateButton, loadStateButton)
      addSingleButton(this, rebootButton)
      addButtonRow(this, exitToLibraryButton, quitAppButton)
    }

    val scrollContainer = NestedScrollView(this).apply {
      isFillViewport = true
      overScrollMode = View.OVER_SCROLL_IF_CONTENT_SCROLLS
      addView(
        buttonList,
        ViewGroup.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.WRAP_CONTENT
        )
      )
    }

    dialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(getString(R.string.in_game_menu_title))
      .setView(scrollContainer)
      .setOnDismissListener {
        inGameMenuDialog = null
        hideSystemUI()
        if (resumeEmulationOnMenuDismiss) {
          resumeEmulationIfSafe()
        }
        resumeEmulationOnMenuDismiss = false
      }
      .create()

    inGameMenuDialog = dialog
    dialog.show()
  }

  private fun shouldHandleActivitySwipeMenu(): Boolean {
    return !isControllerVisible && inGameMenuDialog?.isShowing != true
  }

  private fun currentGestureHostHeight(): Float {
    val hostHeight = mLayout?.height ?: window.decorView.height
    return max(hostHeight, 1).toFloat()
  }

  private fun exitToGameLibrary() {
    requestClose(PendingCloseAction.RETURN_TO_LIBRARY)
  }

  private fun quitApp() {
    requestClose(PendingCloseAction.QUIT_APP)
  }

  private fun requestClose(action: PendingCloseAction) {
    if (pendingCloseAction != PendingCloseAction.NONE) {
      return
    }
    pendingCloseAction = action
    nativeSetReturnToLibraryOnExit(action == PendingCloseAction.RETURN_TO_LIBRARY)
    if (action == PendingCloseAction.RETURN_TO_LIBRARY) {
      // Launch the library activity in a fresh task BEFORE tearing down the
      // emulator. The native side calls _exit() once QEMU stops, which kills
      // this process — Android will then bring the already-queued
      // GameLibraryActivity to the foreground in its own process.
      val intent = Intent(applicationContext, GameLibraryActivity::class.java).apply {
        addFlags(
          Intent.FLAG_ACTIVITY_NEW_TASK or
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
            Intent.FLAG_ACTIVITY_SINGLE_TOP
        )
      }
      applicationContext.startActivity(intent)
    }
    nativeExitEmulation()
    if (action == PendingCloseAction.QUIT_APP) {
      moveTaskToBack(true)
    }
  }

  override fun getLibraries(): Array<String> = arrayOf(
    "SDL2",
    "xemu",
  )
}

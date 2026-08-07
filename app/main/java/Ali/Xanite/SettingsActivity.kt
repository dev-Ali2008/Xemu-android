package Ali.Xanite

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.text.format.Formatter
import android.util.TypedValue
import android.view.View
import android.widget.ArrayAdapter
import android.widget.AutoCompleteTextView
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.documentfile.provider.DocumentFile
import androidx.core.widget.TextViewCompat
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.textfield.TextInputLayout
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedOutputStream
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.Deflater
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

class SettingsActivity : BaseActivity() {
  companion object {
    private const val PREFS_NAME = "xaniteog_prefs"
    private const val PREF_ADVANCED_EXPERIMENTAL_EXPANDED = "settings_advanced_experimental_expanded"
    private const val PREF_HRTF = "setting_hrtf"
    private const val PREF_HRTF_DEFAULT_OFF_MIGRATED = "setting_hrtf_default_off_migrated_v1"
    private const val PREF_SETTINGS_MIGRATED_V2 = "settings_migrated_v2"
    private const val PREF_SETTINGS_MIGRATED_V3 = "settings_migrated_v3"
    private const val MANAGED_FILES_ARCHIVE_PREFIX = "xaniteog-backup-"
    private const val PREFERENCES_ARCHIVE_ENTRY = "settings/preferences.json"
    private const val PREFERENCES_BACKUP_FORMAT = "xaniteog-portable-settings"
    private const val PREFERENCES_BACKUP_VERSION = 1
    private const val MAX_PREFERENCES_BACKUP_BYTES = 8 * 1024 * 1024
    private const val SNAPSHOT_ARCHIVE_PREFIX = "save-states/"
    private val BACKUP_PREFERENCE_STORES = listOf(
      PREFS_NAME,
      "xaniteog_per_game_settings",
      "xbox_live_prefs",
    )
    private val NON_PORTABLE_PREFERENCE_KEYS = setOf(
      "mcpxPath", "mcpxUri",
      "flashPath", "flashUri",
      "hddPath", "hddUri",
      "dvdPath", "dvdUri",
      "gamesFolderUri", "gpuDriverUri",
      "insigniaSetupUri",
    )
    private val MANAGED_EMULATOR_FILE_ORDER = listOf(
      "mcpx.bin",
      "flash.bin",
      "eeprom.bin",
      "hdd.img",
      "xaniteog.toml",
    )
    private val MANAGED_EMULATOR_FILE_NAMES = MANAGED_EMULATOR_FILE_ORDER.toSet()
  }

  private val prefs by lazy { getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE) }

  private data class EepromLanguageOption(
    val value: XboxEepromEditor.Language,
    val labelRes: Int,
  )

  private data class EepromVideoOption(
    val value: XboxEepromEditor.VideoStandard,
    val labelRes: Int,
  )

  private data class EepromAspectRatioOption(
    val value: XboxEepromEditor.AspectRatio,
    val labelRes: Int,
  )

  private data class EepromRefreshRateOption(
    val value: XboxEepromEditor.RefreshRate,
    val labelRes: Int,
  )

  private data class UiOrientationOption(
    val value: OrientationPreferences.UiOrientation,
    val labelRes: Int,
  )

  private data class GameOrientationOption(
    val value: OrientationPreferences.GameOrientation,
    val labelRes: Int,
  )

  private data class CacheClearResult(
    val deletedEntries: Int,
    val hadFailures: Boolean,
  )


  private data class DashboardImportPlan(
    val hddFile: File,
    val workingDir: File,
    val sourceDir: File,
    val backupDir: File,
    val summary: String,
    val bootNote: String?,
    val bootAliasCreated: Boolean,
    val retailBootReady: Boolean,
  )

  private data class DashboardBootPreparation(
    val note: String?,
    val aliasCreated: Boolean,
    val retailBootReady: Boolean,
  )

  private val eepromLanguageOptions = listOf(
    EepromLanguageOption(XboxEepromEditor.Language.ENGLISH, R.string.settings_eeprom_language_english),
    EepromLanguageOption(XboxEepromEditor.Language.JAPANESE, R.string.settings_eeprom_language_japanese),
    EepromLanguageOption(XboxEepromEditor.Language.GERMAN, R.string.settings_eeprom_language_german),
    EepromLanguageOption(XboxEepromEditor.Language.FRENCH, R.string.settings_eeprom_language_french),
    EepromLanguageOption(XboxEepromEditor.Language.SPANISH, R.string.settings_eeprom_language_spanish),
    EepromLanguageOption(XboxEepromEditor.Language.ITALIAN, R.string.settings_eeprom_language_italian),
    EepromLanguageOption(XboxEepromEditor.Language.KOREAN, R.string.settings_eeprom_language_korean),
    EepromLanguageOption(XboxEepromEditor.Language.CHINESE, R.string.settings_eeprom_language_chinese),
    EepromLanguageOption(XboxEepromEditor.Language.PORTUGUESE, R.string.settings_eeprom_language_portuguese),
  )

  private val eepromVideoOptions = listOf(
    EepromVideoOption(XboxEepromEditor.VideoStandard.NTSC_M, R.string.settings_eeprom_video_standard_ntsc_m),
    EepromVideoOption(XboxEepromEditor.VideoStandard.NTSC_J, R.string.settings_eeprom_video_standard_ntsc_j),
    EepromVideoOption(XboxEepromEditor.VideoStandard.PAL_I, R.string.settings_eeprom_video_standard_pal_i),
    EepromVideoOption(XboxEepromEditor.VideoStandard.PAL_M, R.string.settings_eeprom_video_standard_pal_m),
  )

  private val eepromAspectRatioOptions = listOf(
    EepromAspectRatioOption(XboxEepromEditor.AspectRatio.NORMAL, R.string.settings_eeprom_aspect_ratio_normal),
    EepromAspectRatioOption(XboxEepromEditor.AspectRatio.WIDESCREEN, R.string.settings_eeprom_aspect_ratio_widescreen),
    EepromAspectRatioOption(XboxEepromEditor.AspectRatio.LETTERBOX, R.string.settings_eeprom_aspect_ratio_letterbox),
  )

  private val eepromRefreshRateOptions = listOf(
    EepromRefreshRateOption(XboxEepromEditor.RefreshRate.DEFAULT, R.string.settings_eeprom_refresh_rate_default),
    EepromRefreshRateOption(XboxEepromEditor.RefreshRate.HZ_60, R.string.settings_eeprom_refresh_rate_60),
    EepromRefreshRateOption(XboxEepromEditor.RefreshRate.HZ_50, R.string.settings_eeprom_refresh_rate_50),
  )

  private val uiOrientationOptions = listOf(
    UiOrientationOption(OrientationPreferences.UiOrientation.FOLLOW_DEVICE, R.string.settings_orientation_follow_device),
    UiOrientationOption(OrientationPreferences.UiOrientation.PORTRAIT, R.string.settings_orientation_portrait),
    UiOrientationOption(OrientationPreferences.UiOrientation.REVERSE_PORTRAIT, R.string.settings_orientation_reverse_portrait),
    UiOrientationOption(OrientationPreferences.UiOrientation.LANDSCAPE, R.string.settings_orientation_landscape),
    UiOrientationOption(OrientationPreferences.UiOrientation.REVERSE_LANDSCAPE, R.string.settings_orientation_reverse_landscape),
  )

  private val gameOrientationOptions = listOf(
    GameOrientationOption(OrientationPreferences.GameOrientation.FOLLOW_DEVICE, R.string.settings_orientation_follow_device),
    GameOrientationOption(OrientationPreferences.GameOrientation.LANDSCAPE, R.string.settings_orientation_landscape),
    GameOrientationOption(OrientationPreferences.GameOrientation.REVERSE_LANDSCAPE, R.string.settings_orientation_reverse_landscape),
  )

  private var isInitializingHdd = false
  private var isImportingDashboard = false
  private var isImportingEmulatorFiles = false
  private var isExportingEmulatorFiles = false
  private var isChangingSaveHdd = false
  private lateinit var btnImportEmulatorFiles: MaterialButton
  private lateinit var btnExportEmulatorFiles: MaterialButton
  private lateinit var btnChangeSaveHdd: MaterialButton
  private lateinit var textSaveStorageLocation: TextView
  private lateinit var switchDebugLogs: MaterialSwitch
  private lateinit var switchActiveDevLogs: MaterialSwitch

  /**
   * persistSettings() is a local function inside onCreate() (it closes over
   * a number of view references that are themselves local to onCreate), so
   * onBackPressed() — which must be a class member, not local — can't call
   * it directly. onCreate() assigns this to a closure that can, right next
   * to where persistSettings() itself is defined.
   */
  private var confirmExitOnBack: () -> Unit = { finishWithTransition() }
  private lateinit var driverStatusText: TextView
  private lateinit var gpuNotSupportedText: TextView
  private lateinit var btnInstallDriver: MaterialButton
  private lateinit var btnSelectDriver: MaterialButton
  private lateinit var btnResetDriver: MaterialButton
  private lateinit var tvEepromStatus: TextView
  private lateinit var tvHddToolsStatus: TextView
  private lateinit var btnToggleAdvancedExperimental: MaterialButton
  private lateinit var btnImportDashboard: MaterialButton
  private lateinit var layoutAdvancedExperimentalContent: LinearLayout
  private lateinit var dropdownUiOrientation: AutoCompleteTextView
  private lateinit var dropdownAppLanguage: AutoCompleteTextView
  private lateinit var dropdownGameOrientation: AutoCompleteTextView
  private lateinit var inputEepromLanguage: TextInputLayout
  private lateinit var inputEepromVideoStandard: TextInputLayout
  private lateinit var inputEepromAspectRatio: TextInputLayout
  private lateinit var inputEepromRefreshRate: TextInputLayout
  private lateinit var dropdownEepromLanguage: AutoCompleteTextView
  private lateinit var dropdownEepromVideoStandard: AutoCompleteTextView
  private lateinit var dropdownEepromAspectRatio: AutoCompleteTextView
  private lateinit var dropdownEepromRefreshRate: AutoCompleteTextView
  private lateinit var switchEeprom480p: MaterialSwitch
  private lateinit var switchEeprom720p: MaterialSwitch
  private lateinit var switchEeprom1080i: MaterialSwitch

  private var selectedEepromLanguage = XboxEepromEditor.Language.ENGLISH
  private var selectedEepromVideoStandard = XboxEepromEditor.VideoStandard.NTSC_M
  private var selectedEepromAspectRatio = XboxEepromEditor.AspectRatio.NORMAL
  private var selectedEepromRefreshRate = XboxEepromEditor.RefreshRate.DEFAULT
  private var selectedUiOrientation = OrientationPreferences.UiOrientation.FOLLOW_DEVICE
  private var selectedGameOrientation = OrientationPreferences.GameOrientation.FOLLOW_DEVICE
  private var eepromEditable = false
  private var eepromMissing = false
  private var eepromError = false

  private val pickDriverZip =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
      if (uri != null) installDriverFromUri(uri)
    }

  private val pickDashboardZip =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
      uri ?: return@registerForActivityResult
      persistUriPermission(uri)
      if (!isZipSelection(uri)) {
        Toast.makeText(this, R.string.settings_dashboard_import_pick_zip_error, Toast.LENGTH_LONG).show()
        return@registerForActivityResult
      }
      prepareDashboardImportFromZip(uri)
    }

  private val pickDashboardFolder =
    registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
      uri ?: return@registerForActivityResult
      persistUriPermission(uri)
      prepareDashboardImportFromFolder(uri)
    }

  private val exportDebugLogDocument =
    registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri: Uri? ->
      uri ?: return@registerForActivityResult
      exportDebugLog(uri)
    }

  private val importEmulatorFilesZip =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
      uri ?: return@registerForActivityResult
      persistUriPermission(uri)
      if (!isZipSelection(uri)) {
        Toast.makeText(this, R.string.settings_import_emulator_files_pick_zip_error, Toast.LENGTH_LONG).show()
        return@registerForActivityResult
      }
      importManagedFilesFromZip(uri)
    }

  private val importEmulatorFilesDocuments =
    registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
      if (uris.isEmpty()) {
        return@registerForActivityResult
      }
      uris.forEach(::persistUriPermission)
      importManagedFilesFromDocuments(uris)
    }

  private val exportEmulatorFilesDocument =
    registerForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri: Uri? ->
      uri ?: return@registerForActivityResult
      exportManagedFiles(uri)
    }

  private val pickSaveHddImage =
    registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
      uri ?: return@registerForActivityResult
      persistUriPermission(uri)
      showChangeSaveHddConfirmation(uri)
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
      override fun handleOnBackPressed() {
        confirmExitOnBack()
      }
    })
    DebugLog.initialize(this)
    ControllerSettings.applyVisibleByDefaultMigration(this)
    applyHrtfDefaultOffMigration()
    applySettingsMigrationV2()
    applySettingsMigrationV3()
    setContentView(R.layout.activity_settings)
    enableFullScreen()
    EdgeToEdgeHelper.enable(this)
    EdgeToEdgeHelper.applySystemBarPadding(findViewById(R.id.settings_scroll))

    val tabBtnVideo = findViewById<View>(R.id.tab_btn_video)
    val tabBtnAudio = findViewById<View>(R.id.tab_btn_audio)
    val tabBtnSystem = findViewById<View>(R.id.tab_btn_system)
    val tabBtnRendering = findViewById<View>(R.id.tab_btn_rendering)
    val tabBtnInput = findViewById<View>(R.id.tab_btn_input)
    val tabBtnMaintenance = findViewById<View>(R.id.tab_btn_maintenance)
    val tabBtnShortcuts = findViewById<View>(R.id.tab_btn_shortcuts)
    val contentVideo = findViewById<View>(R.id.tab_content_video)
    val contentAudio = findViewById<View>(R.id.tab_content_audio)
    val contentSystem = findViewById<View>(R.id.tab_content_system)
    val contentRendering = findViewById<View>(R.id.tab_content_rendering)
    val contentInput = findViewById<View>(R.id.tab_content_input)
    val contentMaintenance = findViewById<View>(R.id.tab_content_maintenance)
    val contentShortcuts = findViewById<View>(R.id.tab_content_shortcuts)

    val allTabs = listOf(tabBtnVideo, tabBtnAudio, tabBtnSystem, tabBtnRendering, tabBtnInput, tabBtnMaintenance, tabBtnShortcuts)
    val allContent = listOf(contentVideo, contentAudio, contentSystem, contentRendering, contentInput, contentMaintenance, contentShortcuts)

    fun selectTab(selected: Int) {
      allContent.forEachIndexed { i, v ->
        if (i == selected) {
          v.visibility = View.VISIBLE
          v.alpha = 0f
          v.translationY = 20f
          v.animate()
            .alpha(1f)
            .translationY(0f)
            .setDuration(250)
            .setInterpolator(android.view.animation.DecelerateInterpolator())
            .start()
        } else {
          v.visibility = View.GONE
        }
      }
      allTabs.forEachIndexed { i, btn -> btn.isSelected = (i == selected) }
    }

    allTabs.forEachIndexed { i, btn -> btn.setOnClickListener { selectTab(i) } }
    selectTab(0)

    val toggleGraphicsApi = findViewById<MaterialButtonToggleGroup>(R.id.toggle_graphics_api)
    val toggleFiltering   = findViewById<MaterialButtonToggleGroup>(R.id.toggle_filtering)
    val toggleScale       = findViewById<MaterialButtonToggleGroup>(R.id.toggle_resolution_scale)
    val btn0_5x           = findViewById<MaterialButton>(R.id.btn_scale_0_5x)
    val btn1x             = findViewById<MaterialButton>(R.id.btn_scale_1x)
    val btn2x             = findViewById<MaterialButton>(R.id.btn_scale_2x)
    val btn3x             = findViewById<MaterialButton>(R.id.btn_scale_3x)
    val btn4x             = findViewById<MaterialButton>(R.id.btn_scale_4x)
    val btn5x             = findViewById<MaterialButton>(R.id.btn_scale_5x)
    val toggleDisplayMode = findViewById<MaterialButtonToggleGroup>(R.id.toggle_display_mode)
    val toggleSystemMemory = findViewById<MaterialButtonToggleGroup>(R.id.toggle_system_memory)
    val toggleThread      = findViewById<MaterialButtonToggleGroup>(R.id.toggle_tcg_thread)
    val btnMulti          = findViewById<MaterialButton>(R.id.btn_thread_multi)
    val btnSingle         = findViewById<MaterialButton>(R.id.btn_thread_single)
    val toggleCpuSpeed    = findViewById<MaterialButtonToggleGroup>(R.id.toggle_cpu_speed)
    val toggleTextureQuality = findViewById<MaterialButtonToggleGroup>(R.id.toggle_texture_quality)
    val resolutionButtons = listOf(btn0_5x, btn1x, btn2x, btn3x, btn4x, btn5x)
    val resolutionLabels = intArrayOf(
      R.string.settings_resolution_scale_0_5x_compact,
      R.string.settings_resolution_scale_1x_compact,
      R.string.settings_resolution_scale_2x_compact,
      R.string.settings_resolution_scale_3x_compact,
      R.string.settings_resolution_scale_4x_compact,
      R.string.settings_resolution_scale_5x_compact,
    )
    val compactPadding = (2 * resources.displayMetrics.density).toInt()
    resolutionButtons.forEachIndexed { index, button ->
      button.text = getString(resolutionLabels[index])
      button.minWidth = 0
      button.setPadding(compactPadding, button.paddingTop, compactPadding, button.paddingBottom)
      TextViewCompat.setAutoSizeTextTypeUniformWithConfiguration(
        button, 8, 11, 1, TypedValue.COMPLEX_UNIT_SP
      )
    }
    listOf<MaterialButton>(
      findViewById(R.id.btn_tex_lowend),
      findViewById(R.id.btn_tex_low),
      findViewById(R.id.btn_tex_normal),
      findViewById(R.id.btn_tex_high),
    ).forEach { button ->
      button.minWidth = 0
      button.setPadding(compactPadding, button.paddingTop, compactPadding, button.paddingBottom)
      TextViewCompat.setAutoSizeTextTypeUniformWithConfiguration(
        button, 7, 11, 1, TypedValue.COMPLEX_UNIT_SP
      )
    }
    val switchDsp         = findViewById<MaterialSwitch>(R.id.switch_use_dsp)
    val switchDspJit      = findViewById<MaterialSwitch>(R.id.switch_use_dsp_jit)
    val switchHrtf        = findViewById<MaterialSwitch>(R.id.switch_hrtf)
    val switchShaders     = findViewById<MaterialSwitch>(R.id.switch_cache_shaders)
    val switchFpu         = findViewById<MaterialSwitch>(R.id.switch_hard_fpu)
    val switchVsync       = findViewById<MaterialSwitch>(R.id.switch_vsync)
    val switchSkipBootAnim = findViewById<MaterialSwitch>(R.id.switch_skip_boot_anim)
    val switchDrawReorder  = findViewById<MaterialSwitch>(R.id.switch_draw_reorder)
    val switchDrawMerge    = findViewById<MaterialSwitch>(R.id.switch_draw_merge)
    val switchAsyncCompile = findViewById<MaterialSwitch>(R.id.switch_async_compile)

    val switchShowFps      = findViewById<MaterialSwitch>(R.id.switch_show_fps)
    switchDebugLogs      = findViewById(R.id.switch_debug_logs)
    switchActiveDevLogs  = findViewById(R.id.switch_active_dev_logs)
    val toggleAudioDriver = findViewById<MaterialButtonToggleGroup>(R.id.toggle_audio_driver)
    val btnSave           = findViewById<android.widget.LinearLayout>(R.id.btn_a_save_settings)
    val btnRedoSetup      = findViewById<MaterialButton>(R.id.btn_redo_setup_wizard)
    val btnClearCache     = findViewById<MaterialButton>(R.id.btn_clear_system_cache)
    val btnPrivacyPolicy  = findViewById<MaterialButton>(R.id.btn_privacy_policy)
    val btnExportDebugLog = findViewById<MaterialButton>(R.id.btn_export_debug_log)
    val btnCopyDebugLog   = findViewById<MaterialButton>(R.id.btn_copy_debug_log)
    val btnClearDebugLog  = findViewById<MaterialButton>(R.id.btn_clear_debug_log)
    val crashStatusText   = findViewById<TextView>(R.id.text_last_crash_status)
    val btnCopyCrashLog   = findViewById<MaterialButton>(R.id.btn_copy_crash_log)
    val btnDismissCrashLog = findViewById<MaterialButton>(R.id.btn_dismiss_crash_log)
    val sessionStatusText  = findViewById<TextView>(R.id.text_last_session_status)
    val btnCopySessionLog  = findViewById<MaterialButton>(R.id.btn_copy_session_log)
    val btnDismissSessionLog = findViewById<MaterialButton>(R.id.btn_dismiss_session_log)
    btnImportEmulatorFiles = findViewById(R.id.btn_import_emulator_files)
    btnExportEmulatorFiles = findViewById(R.id.btn_export_emulator_files)
    btnChangeSaveHdd = findViewById(R.id.btn_change_save_hdd)
    textSaveStorageLocation = findViewById(R.id.text_save_storage_location)
    val btnInitializeRetailHdd = findViewById<MaterialButton>(R.id.btn_initialize_retail_hdd)
    btnToggleAdvancedExperimental = findViewById(R.id.btn_toggle_advanced_experimental)
    btnImportDashboard   = findViewById(R.id.btn_import_dashboard)
    layoutAdvancedExperimentalContent = findViewById(R.id.layout_advanced_experimental_content)
    dropdownUiOrientation = findViewById(R.id.dropdown_app_orientation)
    dropdownAppLanguage = findViewById(R.id.dropdown_app_language)
    dropdownGameOrientation = findViewById(R.id.dropdown_game_orientation)
    driverStatusText      = findViewById(R.id.settings_gpu_driver_status)
    gpuNotSupportedText   = findViewById(R.id.settings_gpu_not_supported)
    btnInstallDriver      = findViewById(R.id.btn_install_driver)
    btnSelectDriver       = findViewById(R.id.btn_select_driver)
    btnResetDriver        = findViewById(R.id.btn_reset_driver)
    tvEepromStatus        = findViewById(R.id.tv_eeprom_status)
    tvHddToolsStatus      = findViewById(R.id.tv_hdd_tools_status)
    inputEepromLanguage   = findViewById(R.id.input_eeprom_language)
    inputEepromVideoStandard = findViewById(R.id.input_eeprom_video_standard)
    inputEepromAspectRatio = findViewById(R.id.input_eeprom_aspect_ratio)
    inputEepromRefreshRate = findViewById(R.id.input_eeprom_refresh_rate)
    dropdownEepromLanguage = findViewById(R.id.dropdown_eeprom_language)
    dropdownEepromVideoStandard = findViewById(R.id.dropdown_eeprom_video_standard)
    dropdownEepromAspectRatio = findViewById(R.id.dropdown_eeprom_aspect_ratio)
    dropdownEepromRefreshRate = findViewById(R.id.dropdown_eeprom_refresh_rate)
    switchEeprom480p = findViewById(R.id.switch_eeprom_480p)
    switchEeprom720p = findViewById(R.id.switch_eeprom_720p)
    switchEeprom1080i = findViewById(R.id.switch_eeprom_1080i)

    val switchMouseEnabled = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_mouse_enabled)
    val switchMouseRawInput = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_mouse_raw_input)
    val switchMouseInvertY = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_mouse_invert_y)
    val seekMouseSensitivity = findViewById<SeekBar>(R.id.seek_mouse_sensitivity)
    val tvMouseSensitivity = findViewById<TextView>(R.id.tv_mouse_sensitivity_value)
    val seekMouseSmoothness = findViewById<SeekBar>(R.id.seek_mouse_smoothness)
    val tvMouseSmoothness = findViewById<TextView>(R.id.tv_mouse_smoothness_value)
    val switchKeyboardEnabled = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_keyboard_enabled)
    val switchJoystickEnabled = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_joystick_enabled)
    val switchJoystickInvertX = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_joystick_invert_x)
    val switchJoystickInvertY = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_joystick_invert_y)
    val seekJoystickDeadzone = findViewById<SeekBar>(R.id.seek_joystick_deadzone)
    val tvJoystickDeadzone = findViewById<TextView>(R.id.tv_joystick_deadzone_value)
    val seekJoystickSensitivity = findViewById<SeekBar>(R.id.seek_joystick_sensitivity)
    val tvJoystickSensitivity = findViewById<TextView>(R.id.tv_joystick_sensitivity_value)
    val switchShowController = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_show_controller)
    val switchVibration = findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_vibration)
    val seekControllerOpacity = findViewById<SeekBar>(R.id.seek_controller_opacity)
    val tvControllerOpacity = findViewById<TextView>(R.id.tv_controller_opacity_value)
    val seekControllerScale = findViewById<SeekBar>(R.id.seek_controller_scale)
    val tvControllerScale = findViewById<TextView>(R.id.tv_controller_scale_value)

    updateEmulatorFilesActionState()

    // Load current values
    val renderer = prefs.getString("setting_renderer", "vulkan") ?: "vulkan"
    if (renderer == "opengl") {
      toggleGraphicsApi.check(R.id.btn_renderer_opengl)
    } else {
      toggleGraphicsApi.check(R.id.btn_renderer_vulkan)
    }

    val filtering = prefs.getString("setting_filtering", "linear") ?: "linear"
    if (filtering == "nearest") {
      toggleFiltering.check(R.id.btn_filtering_nearest)
    } else {
      toggleFiltering.check(R.id.btn_filtering_linear)
    }

    when (prefs.getInt("setting_surface_scale", 1)) {
      0    -> toggleScale.check(R.id.btn_scale_0_5x)
      2    -> toggleScale.check(R.id.btn_scale_2x)
      3    -> toggleScale.check(R.id.btn_scale_3x)
      4    -> toggleScale.check(R.id.btn_scale_4x)
      5    -> toggleScale.check(R.id.btn_scale_5x)
      else -> toggleScale.check(R.id.btn_scale_1x)
    }

    val displayMode = prefs.getInt("setting_display_mode", 0)
    when (displayMode) {
      1    -> toggleDisplayMode.check(R.id.btn_display_4_3)
      2    -> toggleDisplayMode.check(R.id.btn_display_16_9)
      else -> toggleDisplayMode.check(R.id.btn_display_stretch)
    }

    setupOrientationControls()
    setUiOrientationSelection(OrientationPreferences.getUiOrientation(this))
    setGameOrientationSelection(OrientationPreferences.getGameOrientation(this))
    setupLanguageControls()
    setLanguageSelection(LocaleHelper.getCurrentLocaleCode(this))

    if (prefs.getInt("setting_frame_rate_limit", 60) != 60) {
      prefs.edit().putInt("setting_frame_rate_limit", 60).apply()
    }

    val systemMemoryMiB = prefs.getInt("setting_system_memory_mib", 64)
    when (systemMemoryMiB) {
      128  -> toggleSystemMemory.check(R.id.btn_memory_128)
      else -> toggleSystemMemory.check(R.id.btn_memory_64)
    }

    GpuDriverHelper.init(this)
    val supportsCustomDriver = GpuDriverHelper.supportsCustomDriverLoading()
    if (!supportsCustomDriver) {
      gpuNotSupportedText.visibility = View.VISIBLE
      btnInstallDriver.isEnabled = false
      btnSelectDriver.isEnabled = false
      btnResetDriver.isEnabled = false
    }
    refreshDriverStatus()

    btnInstallDriver.setOnClickListener {
      pickDriverZip.launch(arrayOf("application/zip", "application/octet-stream"))
    }
    btnSelectDriver.setOnClickListener { showDriverSelectionDialog() }
    btnResetDriver.setOnClickListener { confirmResetDriver() }

    val tcgThread = prefs.getString("setting_tcg_thread", "multi") ?: "multi"
    if (tcgThread == "single") {
      toggleThread.check(R.id.btn_thread_single)
    } else {
      toggleThread.check(R.id.btn_thread_multi)
    }

    // Keyboard & Mouse
    seekMouseSensitivity.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
      override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
        val value = 1.0f + progress / 10f
        tvMouseSensitivity.text = String.format(java.util.Locale.US, "%.1f", value)
      }
      override fun onStartTrackingTouch(seek: SeekBar?) {}
      override fun onStopTrackingTouch(seek: SeekBar?) {}
    })
    seekMouseSmoothness.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
      override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
        val value = progress / 100f
        tvMouseSmoothness.text = String.format(java.util.Locale.US, "%.2f", value)
      }
      override fun onStartTrackingTouch(seek: SeekBar?) {}
      override fun onStopTrackingTouch(seek: SeekBar?) {}
    })
    // Joystick
    seekJoystickDeadzone.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
      override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
        val value = 0.02f + progress / 1000f
        tvJoystickDeadzone.text = String.format(java.util.Locale.US, "%.2f", value)
      }
      override fun onStartTrackingTouch(seek: SeekBar?) {}
      override fun onStopTrackingTouch(seek: SeekBar?) {}
    })
    seekJoystickSensitivity.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
      override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
        val value = progress / 100f
        tvJoystickSensitivity.text = String.format(java.util.Locale.US, "%.1f", value)
      }
      override fun onStartTrackingTouch(seek: SeekBar?) {}
      override fun onStopTrackingTouch(seek: SeekBar?) {}
    })
    // Appearance
    seekControllerOpacity.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
      override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
        val value = progress / 100f
        tvControllerOpacity.text = String.format(java.util.Locale.US, "%.2f", value)
      }
      override fun onStartTrackingTouch(seek: SeekBar?) {}
      override fun onStopTrackingTouch(seek: SeekBar?) {}
    })
    seekControllerScale.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
      override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
        val value = 0.5f + progress / 100f
        tvControllerScale.text = String.format(java.util.Locale.US, "%.1f", value)
      }
      override fun onStartTrackingTouch(seek: SeekBar?) {}
      override fun onStopTrackingTouch(seek: SeekBar?) {}
    })

    // Load new settings
    switchMouseEnabled.isChecked = prefs.getBoolean("setting_mouse_enabled", true)
    switchMouseRawInput.isChecked = prefs.getBoolean("setting_mouse_raw_input", true)
    switchMouseInvertY.isChecked = prefs.getBoolean("setting_mouse_invert_y", false)
    val mouseSens = prefs.getFloat("setting_mouse_sensitivity", 8.0f)
    seekMouseSensitivity.progress = ((mouseSens - 1.0f) * 10f).toInt().coerceIn(0, 490)
    tvMouseSensitivity.text = String.format(java.util.Locale.US, "%.1f", mouseSens)
    val mouseSmooth = prefs.getFloat("setting_mouse_smoothness", 0.25f)
    seekMouseSmoothness.progress = (mouseSmooth * 100f).toInt().coerceIn(0, 100)
    tvMouseSmoothness.text = String.format(java.util.Locale.US, "%.2f", mouseSmooth)
    switchKeyboardEnabled.isChecked = prefs.getBoolean("setting_keyboard_enabled", true)
    switchJoystickEnabled.isChecked = prefs.getBoolean("setting_joystick_enabled", true)
    switchJoystickInvertX.isChecked = prefs.getBoolean("setting_joystick_invert_x", false)
    switchJoystickInvertY.isChecked = prefs.getBoolean("setting_joystick_invert_y", false)
    val joystickDeadzone = prefs.getFloat("setting_joystick_deadzone", 0.12f)
    seekJoystickDeadzone.progress = ((joystickDeadzone - 0.02f) * 1000f).toInt().coerceIn(0, 330)
    tvJoystickDeadzone.text = String.format(java.util.Locale.US, "%.2f", joystickDeadzone)
    val joySens = prefs.getFloat("setting_joystick_sensitivity", 1.0f)
    seekJoystickSensitivity.progress = (joySens * 100f).toInt().coerceIn(0, 200)
    tvJoystickSensitivity.text = String.format(java.util.Locale.US, "%.1f", joySens)
    switchShowController.isChecked =
      prefs.getBoolean(ControllerSettings.KEY_SHOW_CONTROLLER, true)
    switchVibration.isChecked = prefs.getBoolean("setting_vibration", true)
    val opacity = prefs.getFloat("setting_controller_opacity", 0.85f)
    seekControllerOpacity.progress = (opacity * 100f).toInt().coerceIn(0, 100)
    tvControllerOpacity.text = String.format(java.util.Locale.US, "%.2f", opacity)
    val scale = prefs.getFloat("setting_controller_scale", 1.0f)
    seekControllerScale.progress = ((scale - 0.5f) * 100f).toInt().coerceIn(0, 150)
    tvControllerScale.text = String.format(java.util.Locale.US, "%.1f", scale)

    val cpuSpeed = prefs.getInt("setting_cpu_speed_mhz", 733)
    when (cpuSpeed) {
      1400 -> toggleCpuSpeed.check(R.id.btn_cpu_1400)
      else -> toggleCpuSpeed.check(R.id.btn_cpu_733)
    }

    val texQuality = prefs.getInt("setting_texture_quality", 2)
    when (texQuality) {
      0 -> toggleTextureQuality.check(R.id.btn_tex_lowend)
      1 -> toggleTextureQuality.check(R.id.btn_tex_low)
      3 -> toggleTextureQuality.check(R.id.btn_tex_high)
      else -> toggleTextureQuality.check(R.id.btn_tex_normal)
    }

    switchDsp.isChecked     = prefs.getBoolean("setting_use_dsp", false)
    switchDspJit.isChecked  = prefs.getBoolean("setting_use_dsp_jit", true)
    switchHrtf.isChecked    = prefs.getBoolean(PREF_HRTF, false)
    switchShaders.isChecked = prefs.getBoolean("setting_cache_shaders", true)
    switchFpu.isChecked     = prefs.getBoolean("setting_hard_fpu", true)
    switchVsync.isChecked   = prefs.getBoolean("setting_vsync", false)
    switchSkipBootAnim.isChecked =
      prefs.getBoolean("setting_skip_boot_anim", true)
    switchDrawReorder.isChecked  = prefs.getBoolean("draw_reorder", true)
    switchDrawMerge.isChecked    = prefs.getBoolean("draw_merge", true)
    switchAsyncCompile.isChecked = prefs.getBoolean("async_compile", false)
    switchShowFps.isChecked      = prefs.getBoolean("show_fps", false)
    switchDebugLogs.isChecked =
      prefs.getBoolean(DebugLog.PREF_ENABLED, false)
    switchActiveDevLogs.isChecked =
      prefs.getBoolean(CrashReportManager.PREF_ACTIVE_DEV_LOGS, false)
    val audioDriver = prefs.getString("setting_audio_driver", "openslES") ?: "openslES"
    when (audioDriver) {
      "aaudio"  -> toggleAudioDriver.check(R.id.btn_audio_aaudio)
      "dummy"   -> toggleAudioDriver.check(R.id.btn_audio_disabled)
      else      -> toggleAudioDriver.check(R.id.btn_audio_opensles)
    }

    btnRedoSetup.setOnClickListener {
      prefs.edit().putBoolean("setup_complete", false).apply()
      startActivity(Intent(this, SetupWizardActivity::class.java))
      overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
      finish()
    }

    setupEepromEditor()
    refreshHddToolsPreview(btnInitializeRetailHdd)
    setAdvancedExperimentalExpanded(
      prefs.getBoolean(PREF_ADVANCED_EXPERIMENTAL_EXPANDED, false)
    )
    btnToggleAdvancedExperimental.setOnClickListener {
      setAdvancedExperimentalExpanded(layoutAdvancedExperimentalContent.visibility != View.VISIBLE)
    }
    btnImportDashboard.setOnClickListener {
      showDashboardImportSourcePicker()
    }

    fun persistSettings(): Pair<Int, Int> {
      val selectedDisplayMode = when (toggleDisplayMode.checkedButtonId) {
        R.id.btn_display_4_3  -> 1
        R.id.btn_display_16_9 -> 2
        else                   -> 0
      }
      val selectedScale = when (toggleScale.checkedButtonId) {
        R.id.btn_scale_0_5x -> 0
        R.id.btn_scale_2x   -> 2
        R.id.btn_scale_3x   -> 3
        R.id.btn_scale_4x   -> 4
        R.id.btn_scale_5x   -> 5
        else                -> 1
      }
      val selectedThread = when (toggleThread.checkedButtonId) {
        R.id.btn_thread_single -> "single"
        else                   -> "multi"
      }
      val selectedCpuSpeedMhz = when (toggleCpuSpeed.checkedButtonId) {
        R.id.btn_cpu_1400 -> 1400
        else              -> 733
      }
      val selectedTexQuality = when (toggleTextureQuality.checkedButtonId) {
        R.id.btn_tex_lowend -> 0
        R.id.btn_tex_low    -> 1
        R.id.btn_tex_high   -> 3
        else                -> 2
      }
      val selectedSystemMemoryMiB = when (toggleSystemMemory.checkedButtonId) {
        R.id.btn_memory_128 -> 128
        else                -> 64
      }
      val selectedAudioDriver = when (toggleAudioDriver.checkedButtonId) {
        R.id.btn_audio_aaudio    -> "aaudio"
        R.id.btn_audio_disabled  -> "dummy"
        else                     -> "openslES"
      }
      val selectedRenderer = when (toggleGraphicsApi.checkedButtonId) {
        R.id.btn_renderer_opengl -> "opengl"
        else                     -> "vulkan"
      }
      val selectedFiltering = when (toggleFiltering.checkedButtonId) {
        R.id.btn_filtering_nearest -> "nearest"
        else                       -> "linear"
      }
      val wasDebugLoggingEnabled = prefs.getBoolean(DebugLog.PREF_ENABLED, false)
      val enableDebugLogs = switchDebugLogs.isChecked

      val mouseSens = 1.0f + findViewById<SeekBar>(R.id.seek_mouse_sensitivity).progress / 10f
      val mouseSmooth = findViewById<SeekBar>(R.id.seek_mouse_smoothness).progress / 100f
      val joyDeadzone = 0.02f + findViewById<SeekBar>(R.id.seek_joystick_deadzone).progress / 1000f
      val joySens = findViewById<SeekBar>(R.id.seek_joystick_sensitivity).progress / 100f
      val opacity = findViewById<SeekBar>(R.id.seek_controller_opacity).progress / 100f
      val ctrlScale = 0.5f + findViewById<SeekBar>(R.id.seek_controller_scale).progress / 100f

      val edit = prefs.edit()
        .putInt("setting_display_mode", selectedDisplayMode)
        .putInt("setting_surface_scale", selectedScale)
        .putInt("setting_frame_rate_limit", 60)
        .putInt("setting_system_memory_mib", selectedSystemMemoryMiB)
        .putString(OrientationPreferences.PREF_UI_ORIENTATION, selectedUiOrientation.prefValue)
        .putString(OrientationPreferences.PREF_GAME_ORIENTATION, selectedGameOrientation.prefValue)
        .putString("setting_tcg_thread", selectedThread)
        .putInt("setting_cpu_speed_mhz", selectedCpuSpeedMhz)
        .putInt("setting_texture_quality", selectedTexQuality)
        .putBoolean("setting_use_dsp", switchDsp.isChecked)
        .putBoolean("setting_use_dsp_jit", switchDspJit.isChecked)
        .putBoolean(PREF_HRTF, switchHrtf.isChecked)
        .putBoolean("setting_cache_shaders", switchShaders.isChecked)
        .putBoolean("setting_hard_fpu", switchFpu.isChecked)
        .putBoolean("setting_vsync", switchVsync.isChecked)
        .putBoolean("setting_skip_boot_anim", switchSkipBootAnim.isChecked)
        .putBoolean("draw_reorder", switchDrawReorder.isChecked)
        .putBoolean("draw_merge", switchDrawMerge.isChecked)
        .putBoolean("async_compile", switchAsyncCompile.isChecked)
        .putBoolean("show_fps", switchShowFps.isChecked)
        .putBoolean(DebugLog.PREF_ENABLED, enableDebugLogs)
        .putBoolean(CrashReportManager.PREF_ACTIVE_DEV_LOGS, switchActiveDevLogs.isChecked)
        .putString("setting_audio_driver", selectedAudioDriver)
        .putString("setting_filtering", selectedFiltering)
        .putString("setting_renderer", selectedRenderer)
        .putBoolean("setting_mouse_enabled", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_mouse_enabled).isChecked)
        .putBoolean("setting_mouse_raw_input", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_mouse_raw_input).isChecked)
        .putBoolean("setting_mouse_invert_y", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_mouse_invert_y).isChecked)
        .putFloat("setting_mouse_sensitivity", mouseSens)
        .putFloat("setting_mouse_smoothness", mouseSmooth)
        .putBoolean("setting_keyboard_enabled", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_keyboard_enabled).isChecked)
        .putBoolean("setting_joystick_enabled", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_joystick_enabled).isChecked)
        .putBoolean("setting_joystick_invert_x", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_joystick_invert_x).isChecked)
        .putBoolean("setting_joystick_invert_y", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_joystick_invert_y).isChecked)
        .putFloat("setting_joystick_deadzone", joyDeadzone)
        .putFloat("setting_joystick_sensitivity", joySens)
        .putBoolean(ControllerSettings.KEY_SHOW_CONTROLLER, findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_show_controller).isChecked)
        .putBoolean("setting_vibration", findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.switch_vibration).isChecked)
        .putFloat("setting_controller_opacity", opacity)
        .putFloat("setting_controller_scale", ctrlScale)

      edit.apply()
      DebugLog.setEnabled(
        context = this@SettingsActivity,
        value = enableDebugLogs,
        resetLogs = enableDebugLogs != wasDebugLoggingEnabled
      )

      return applyEepromEdits()
    }

    /**
     * Same scope as persistSettings() above (both local to onCreate) so it
     * can call it directly. Used by the close button, the on-screen B/Back
     * button, and (via confirmExitOnBack) the system back gesture — all
     * three used to discard whatever the user had changed with no warning.
     */
    fun confirmExitIfNeeded() {
      MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
        .setTitle(R.string.settings_save_changes_title)
        .setPositiveButton(android.R.string.yes) { _, _ ->
          try {
            val toastResult = persistSettings()
            Toast.makeText(this, toastResult.first, toastResult.second).show()
          } catch (error: Exception) {
            Toast.makeText(
              this,
              "Failed to save settings: ${error.message ?: error.javaClass.simpleName}",
              Toast.LENGTH_LONG
            ).show()
          }
          finishWithTransition()
        }
        .setNegativeButton(android.R.string.no) { _, _ ->
          finishWithTransition()
        }
        .show()
    }
    confirmExitOnBack = { confirmExitIfNeeded() }

    btnClearCache.setOnClickListener {
      showClearCacheConfirmation()
    }
    btnPrivacyPolicy.setOnClickListener {
      startActivity(Intent(this, PrivacyPolicyActivity::class.java))
    }
    setUpAccountAndLicense()
    btnExportDebugLog.setOnClickListener {
      if (!DebugLog.hasAnyLog(this)) {
        Toast.makeText(this, R.string.settings_export_debug_log_empty, Toast.LENGTH_LONG).show()
        return@setOnClickListener
      }
      exportDebugLogDocument.launch(DebugLog.exportDefaultFileName())
    }
    btnCopyDebugLog.setOnClickListener {
      if (DebugLog.copyToClipboard(this)) {
        Toast.makeText(this, R.string.settings_copy_debug_log_success, Toast.LENGTH_SHORT).show()
      } else {
        Toast.makeText(this, R.string.settings_copy_debug_log_empty, Toast.LENGTH_LONG).show()
      }
    }
    btnClearDebugLog.setOnClickListener {
      if (!DebugLog.hasAnyLog(this)) {
        Toast.makeText(this, R.string.settings_clear_debug_log_empty, Toast.LENGTH_SHORT).show()
        return@setOnClickListener
      }
      DebugLog.resetLogs(this)
      Toast.makeText(this, R.string.settings_clear_debug_log_success, Toast.LENGTH_SHORT).show()
    }
    btnCopyCrashLog.setOnClickListener {
      if (CrashReportManager.copyLatestCrashToClipboard(this)) {
        Toast.makeText(this, R.string.settings_copy_crash_log_success, Toast.LENGTH_SHORT).show()
      } else {
        Toast.makeText(this, R.string.settings_last_crash_none, Toast.LENGTH_SHORT).show()
      }
    }
    btnDismissCrashLog.setOnClickListener {
      CrashReportManager.clearAll(this)
      refreshCrashSection(crashStatusText, btnCopyCrashLog, btnDismissCrashLog)
    }
    btnCopySessionLog.setOnClickListener {
      if (CrashReportManager.copyLatestSessionLogToClipboard(this)) {
        Toast.makeText(this, R.string.settings_copy_session_log_success, Toast.LENGTH_SHORT).show()
      } else {
        Toast.makeText(this, R.string.settings_last_session_none, Toast.LENGTH_SHORT).show()
      }
    }
    btnDismissSessionLog.setOnClickListener {
      CrashReportManager.clearSessionLogs(this)
      refreshSessionSection(sessionStatusText, btnCopySessionLog, btnDismissSessionLog)
    }
    refreshCrashSection(crashStatusText, btnCopyCrashLog, btnDismissCrashLog)
    refreshSessionSection(sessionStatusText, btnCopySessionLog, btnDismissSessionLog)
    refreshSaveStorageLocation()
    btnChangeSaveHdd.setOnClickListener {
      if (!isBackupOperationBusy()) {
        pickSaveHddImage.launch(arrayOf("application/octet-stream", "*/*"))
      }
    }
    btnImportEmulatorFiles.setOnClickListener {
      if (isBackupOperationBusy()) {
        return@setOnClickListener
      }
      showManagedFilesImportWarning()
    }
    btnExportEmulatorFiles.setOnClickListener {
      if (isBackupOperationBusy()) {
        return@setOnClickListener
      }
      exportEmulatorFilesDocument.launch(defaultManagedFilesArchiveName())
    }

    btnInitializeRetailHdd.setOnClickListener {
      showInitializeHddLayoutPicker(btnInitializeRetailHdd)
    }

        btnSave.setOnClickListener {
      try {
        val toastResult = persistSettings()
        Toast.makeText(this, toastResult.first, toastResult.second).show()
        finishWithTransition()
      } catch (error: Exception) {
        Toast.makeText(
          this,
          "Failed to save settings: ${error.message ?: error.javaClass.simpleName}",
          Toast.LENGTH_LONG
        ).show()
      }
    }

    findViewById<View>(R.id.btn_settings_close).setOnClickListener { confirmExitIfNeeded() }
    findViewById<View>(R.id.btn_b_back_settings).setOnClickListener { confirmExitIfNeeded() }

  }

  // --- Account & License (Gold only) ----------------------------------------

  /**
   * Wires the activation card. On green the card stays gone, so the whole
   * section is invisible rather than showing an inapplicable "not activated".
   */
  private fun setUpAccountAndLicense() {
    val card = findViewById<View>(R.id.card_account_license) ?: return
    if (!BuildConfig.GOLD_LICENSING_REQUIRED) {
      card.visibility = View.GONE
      return
    }
    card.visibility = View.VISIBLE
    renderLicenseStatus()

    findViewById<MaterialButton>(R.id.btn_refresh_license).setOnClickListener {
      it.isEnabled = false
      Toast.makeText(this, R.string.account_refreshing, Toast.LENGTH_SHORT).show()
      ActivationClient.refresh(this) { result ->
        runOnUiThread {
          it.isEnabled = true
          val msg = if (result is ActivationClient.Result.Success) {
            R.string.account_refresh_ok
          } else {
            R.string.account_refresh_failed
          }
          Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
          renderLicenseStatus()
        }
      }
    }

    findViewById<MaterialButton>(R.id.btn_unbind_device).setOnClickListener {
      confirmUnbindDevice()
    }
  }

  private fun renderLicenseStatus() {
    val view = findViewById<TextView>(R.id.text_account_status) ?: return
    val info = LicenseGate.info(this)
    view.text = if (info == null) {
      getString(R.string.activation_error_generic)
    } else {
      buildString {
        append(getString(R.string.account_status_label)).append(": ")
        append(getString(R.string.account_status_activated)).append('\n')
        append(getString(R.string.account_patreon_id_label)).append(": ")
        append(info.patreonId).append('\n')
        append(getString(R.string.account_slots_label)).append(": ")
        append(getString(R.string.account_slots_value, info.slotsUsed, info.maxSlots))
      }
    }
  }

  private fun confirmUnbindDevice() {
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.account_unbind_title)
      // Mirrors MAX_UNBINDS_PER_WINDOW / UNBIND_WINDOW_DAYS on the activation
      // worker; the server is authoritative and will still return "cooldown".
      .setMessage(getString(R.string.account_unbind_message, 3, 30))
      .setNegativeButton(R.string.account_unbind_cancel, null)
      .setPositiveButton(R.string.account_unbind_confirm) { _, _ ->
        ActivationClient.unbind(this) { result ->
          runOnUiThread {
            when (result) {
              is ActivationClient.Result.Success -> {
                Toast.makeText(this, R.string.account_unbind_ok, Toast.LENGTH_SHORT).show()
                // Slot released: drop straight back to the hard block.
                startActivity(ActivationGatewayActivity.intent(this))
                finish()
              }
              is ActivationClient.Result.Denied ->
                Toast.makeText(
                  this,
                  if (result.reason == "cooldown") getString(R.string.account_unbind_cooldown, 30)
                  else getString(R.string.account_unbind_failed),
                  Toast.LENGTH_LONG,
                ).show()
              is ActivationClient.Result.Failed ->
                Toast.makeText(this, R.string.account_unbind_failed, Toast.LENGTH_LONG).show()
            }
          }
        }
      }
      .show()
  }

  private fun applyHrtfDefaultOffMigration() {
    if (prefs.getBoolean(PREF_HRTF_DEFAULT_OFF_MIGRATED, false)) {
      return
    }

    prefs.edit()
      .putBoolean(PREF_HRTF, false)
      .putBoolean(PREF_HRTF_DEFAULT_OFF_MIGRATED, true)
      .apply()
  }

  private fun applySettingsMigrationV2() {
    if (prefs.getBoolean(PREF_SETTINGS_MIGRATED_V2, false)) {
      return
    }

    val editor = prefs.edit()

    if (!prefs.contains("setting_skip_boot_anim")) editor.putBoolean("setting_skip_boot_anim", true)
    if (!prefs.contains("draw_reorder")) editor.putBoolean("draw_reorder", true)
    if (!prefs.contains("draw_merge")) editor.putBoolean("draw_merge", true)
    if (!prefs.contains("async_compile")) editor.putBoolean("async_compile", false)
    if (!prefs.contains("setting_cache_shaders")) editor.putBoolean("setting_cache_shaders", true)
    if (!prefs.contains("setting_hard_fpu")) editor.putBoolean("setting_hard_fpu", true)
    if (!prefs.contains("setting_vsync")) editor.putBoolean("setting_vsync", false)
    if (!prefs.contains("setting_use_dsp")) editor.putBoolean("setting_use_dsp", false)
    if (!prefs.contains("setting_hrtf")) editor.putBoolean("setting_hrtf", false)
    if (!prefs.contains("setting_network_enable")) editor.putBoolean("setting_network_enable", false)
    if (!prefs.contains("setting_renderer")) editor.putString("setting_renderer", "vulkan")
    if (!prefs.contains("setting_filtering")) editor.putString("setting_filtering", "nearest")
    if (!prefs.contains("setting_tcg_thread")) editor.putString("setting_tcg_thread", "multi")
    if (!prefs.contains("setting_audio_driver")) editor.putString("setting_audio_driver", "openslES")
    if (!prefs.contains("setting_surface_scale")) editor.putInt("setting_surface_scale", 1)
    if (!prefs.contains("setting_display_mode")) editor.putInt("setting_display_mode", 0)
    if (!prefs.contains("setting_system_memory_mib")) editor.putInt("setting_system_memory_mib", 64)
    if (!prefs.contains("tcg_tb_size")) editor.putInt("tcg_tb_size", 256)

    editor.putBoolean(PREF_SETTINGS_MIGRATED_V2, true).apply()
  }

  private fun applySettingsMigrationV3() {
    if (prefs.getBoolean(PREF_SETTINGS_MIGRATED_V3, false)) {
      return
    }

    prefs.edit()
      .putBoolean("setting_use_dsp_jit", prefs.getBoolean("setting_use_dsp_jit", true))
      .putBoolean(PREF_SETTINGS_MIGRATED_V3, true)
      .apply()
  }

  private fun installDriverFromUri(uri: Uri) {
    Thread {
      val success = GpuDriverHelper.installDriverFromUri(this, uri)
      runOnUiThread {
        if (success) {
          MaterialAlertDialogBuilder(this)
            .setTitle(R.string.settings_gpu_driver_installed)
            .setPositiveButton(android.R.string.ok, null)
            .show()
          refreshDriverStatus()
        } else {
          MaterialAlertDialogBuilder(this)
            .setTitle(R.string.settings_gpu_driver_install_failed)
            .setPositiveButton(android.R.string.ok, null)
            .show()
        }
      }
    }.start()
  }

  private fun showDriverSelectionDialog() {
    val drivers = GpuDriverHelper.getAvailableDrivers()
    if (drivers.isEmpty()) {
      MaterialAlertDialogBuilder(this)
        .setTitle(R.string.settings_gpu_driver_none_available)
        .setPositiveButton(android.R.string.ok, null)
        .show()
      return
    }
    val labels = drivers.map { driver ->
      buildString {
        append(driver.name ?: "Unknown")
        if (!driver.description.isNullOrBlank()) { append("\n"); append(driver.description) }
        if (!driver.author.isNullOrBlank()) { append("\nby "); append(driver.author) }
      }
    }.toTypedArray()
    MaterialAlertDialogBuilder(this)
      .setTitle(R.string.settings_gpu_driver_select_title)
      .setItems(labels) { _, which ->
        val selected = drivers[which]
        if (selected.path != null) {
          val zipFile = File(selected.path)
          val success = GpuDriverHelper.installDriver(zipFile)
          if (success) {
            MaterialAlertDialogBuilder(this)
              .setTitle(R.string.settings_gpu_driver_installed)
              .setPositiveButton(android.R.string.ok, null)
              .show()
            refreshDriverStatus()
          } else {
            MaterialAlertDialogBuilder(this)
              .setTitle(R.string.settings_gpu_driver_install_failed)
              .setPositiveButton(android.R.string.ok, null)
              .show()
          }
        }
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun confirmResetDriver() {
    MaterialAlertDialogBuilder(this)
      .setTitle(R.string.settings_gpu_driver_reset_title)
      .setMessage(R.string.settings_gpu_driver_reset_message)
      .setPositiveButton(R.string.settings_gpu_driver_reset) { _, _ ->
        GpuDriverHelper.installDefaultDriver()
        MaterialAlertDialogBuilder(this)
          .setTitle(R.string.settings_gpu_driver_reset_done)
          .setPositiveButton(android.R.string.ok, null)
          .show()
        refreshDriverStatus()
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun refreshDriverStatus() {
    val name = GpuDriverHelper.getInstalledDriverName()
    driverStatusText.text = if (name != null) {
      getString(R.string.settings_gpu_driver_active, name)
    } else {
      getString(R.string.settings_gpu_driver_system)
    }
  }

  private fun updateEmulatorFilesActionState() {
    if (!::btnImportEmulatorFiles.isInitialized ||
      !::btnExportEmulatorFiles.isInitialized ||
      !::btnChangeSaveHdd.isInitialized
    ) {
      return
    }
    val enabled = !isBackupOperationBusy()
    btnImportEmulatorFiles.isEnabled = enabled
    btnExportEmulatorFiles.isEnabled = enabled
    btnChangeSaveHdd.isEnabled = enabled
  }

  private fun isBackupOperationBusy(): Boolean {
    return isImportingEmulatorFiles || isExportingEmulatorFiles || isChangingSaveHdd
  }

  private fun refreshSaveStorageLocation() {
    if (!::textSaveStorageLocation.isInitialized) return
    val location = resolveManagedExportSource("hdd.img")?.absolutePath
      ?: prefs.getString("hddUri", null)?.let { raw ->
        val uri = runCatching { Uri.parse(raw) }.getOrNull()
        uri?.let(::getFileName) ?: uri?.lastPathSegment ?: raw
      }
      ?: getString(R.string.settings_save_storage_missing)
    textSaveStorageLocation.text = getString(R.string.settings_save_storage_location, location)
  }

  private fun showChangeSaveHddConfirmation(uri: Uri) {
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_change_save_hdd_action)
      .setMessage(R.string.settings_change_save_hdd_message)
      .setPositiveButton(android.R.string.ok) { _, _ -> replaceSaveHddFromUri(uri) }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun replaceSaveHddFromUri(uri: Uri) {
    if (isBackupOperationBusy()) return
    isChangingSaveHdd = true
    updateEmulatorFilesActionState()
    Toast.makeText(this, R.string.settings_change_save_hdd_working, Toast.LENGTH_SHORT).show()

    Thread {
      val result = runCatching {
        val target = resolveManagedImportTarget("hdd.img")
        val temporary = File(target.parentFile, "${target.name}.importing")
        temporary.delete()
        copyUriToFile(uri, temporary)
        if (!temporary.isFile || temporary.length() == 0L) {
          temporary.delete()
          throw IOException("The selected HDD image is empty.")
        }

        val previous = File(target.parentFile, "${target.name}.previous")
        previous.delete()
        if (target.exists() && !target.renameTo(previous)) {
          temporary.delete()
          throw IOException("The current HDD image could not be prepared for replacement.")
        }
        if (!temporary.renameTo(target)) {
          previous.renameTo(target)
          temporary.delete()
          throw IOException("The new HDD image could not be installed.")
        }
        previous.delete()
        prefs.edit()
          .putString("hddPath", target.absolutePath)
          .remove("hddUri")
          .apply()
      }

      runOnUiThread {
        isChangingSaveHdd = false
        updateEmulatorFilesActionState()
        result.onSuccess {
          refreshSaveStorageLocation()
          refreshHddToolsPreview(findViewById(R.id.btn_initialize_retail_hdd))
          Toast.makeText(this, R.string.settings_change_save_hdd_success, Toast.LENGTH_LONG).show()
        }.onFailure { error ->
          Toast.makeText(
            this,
            getString(R.string.settings_change_save_hdd_failed, error.message ?: error.javaClass.simpleName),
            Toast.LENGTH_LONG,
          ).show()
        }
      }
    }.start()
  }

  private fun showManagedFilesImportWarning() {
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_import_emulator_files_action)
      .setMessage(R.string.settings_import_emulator_files_message)
      .setPositiveButton(R.string.settings_import_emulator_files_continue) { _, _ ->
        showManagedFilesImportSourcePicker()
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun showManagedFilesImportSourcePicker() {
    val labels = arrayOf(
      getString(R.string.settings_import_emulator_files_source_zip),
      getString(R.string.settings_import_emulator_files_source_files),
    )
    val dp = resources.displayMetrics.density
    lateinit var importDialog: androidx.appcompat.app.AlertDialog

    val buttonList = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding((20 * dp).toInt(), (12 * dp).toInt(), (20 * dp).toInt(), 0)
      labels.forEachIndexed { index, label ->
        addView(
          MaterialButton(
            this@SettingsActivity,
            null,
            com.google.android.material.R.attr.materialButtonOutlinedStyle
          ).apply {
            text = label
            layoutParams = LinearLayout.LayoutParams(
              LinearLayout.LayoutParams.MATCH_PARENT,
              LinearLayout.LayoutParams.WRAP_CONTENT,
            ).also { lp ->
              lp.bottomMargin = (8 * dp).toInt()
            }
            setOnClickListener {
              importDialog.dismiss()
              when (index) {
                0 -> importEmulatorFilesZip.launch(arrayOf("application/zip", "application/octet-stream"))
                else -> importEmulatorFilesDocuments.launch(arrayOf("*/*"))
              }
            }
          }
        )
      }
    }

    importDialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_import_emulator_files_source_title)
      .setView(buttonList)
      .setNegativeButton(android.R.string.cancel, null)
      .create()
    importDialog.show()
  }

  private fun defaultManagedFilesArchiveName(): String {
    val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
    return "$MANAGED_FILES_ARCHIVE_PREFIX$stamp.zip"
  }

  private fun exportManagedFiles(uri: Uri) {
    if (isBackupOperationBusy()) {
      return
    }

    val filesToExport = resolveManagedFilesForExport()
    isExportingEmulatorFiles = true
    updateEmulatorFilesActionState()
    Toast.makeText(this, R.string.settings_export_emulator_files_working, Toast.LENGTH_SHORT).show()

    Thread {
      val result = runCatching {
        exportManagedFilesInternal(uri, filesToExport)
      }

      runOnUiThread {
        isExportingEmulatorFiles = false
        updateEmulatorFilesActionState()
        result.onSuccess { exportedNames ->
          Toast.makeText(
            this,
            getString(R.string.settings_export_emulator_files_success, exportedNames.size),
            Toast.LENGTH_LONG,
          ).show()
        }.onFailure { error ->
          Toast.makeText(
            this,
            getString(
              R.string.settings_export_emulator_files_failed,
              error.message ?: getString(R.string.settings_import_emulator_files_unknown_error),
            ),
            Toast.LENGTH_LONG,
          ).show()
        }
      }
    }.start()
  }

  private fun exportManagedFilesInternal(
    uri: Uri,
    filesToExport: List<Pair<String, File>>,
  ): List<String> {
    val snapshotFiles = resolveSnapshotFilesForExport()
    val settingsBytes = buildPreferencesBackupJson().toByteArray(Charsets.UTF_8)
    contentResolver.openOutputStream(uri, "w")?.use { rawOutput ->
      ZipOutputStream(BufferedOutputStream(rawOutput)).use { zip ->
        zip.setLevel(Deflater.NO_COMPRESSION)
        writeBytesToZip(zip, PREFERENCES_ARCHIVE_ENTRY, settingsBytes)
        for ((name, file) in filesToExport) {
          writeFileToZip(zip, name, file)
        }
        for ((name, file) in snapshotFiles) {
          writeFileToZip(zip, name, file)
        }
        zip.finish()
      }
    } ?: throw IOException(getString(R.string.settings_export_emulator_files_open_failed))

    return filesToExport.map { it.first } +
      listOf("settings") +
      List(snapshotFiles.size) { "save state" }
  }

  private fun writeBytesToZip(zip: ZipOutputStream, name: String, bytes: ByteArray) {
    zip.putNextEntry(ZipEntry(name))
    zip.write(bytes)
    zip.closeEntry()
  }

  private fun writeFileToZip(zip: ZipOutputStream, name: String, file: File) {
    val entry = ZipEntry(name)
    if (file.lastModified() > 0L) entry.time = file.lastModified()
    zip.putNextEntry(entry)
    file.inputStream().use { input -> input.copyTo(zip) }
    zip.closeEntry()
  }

  private fun resolveManagedFilesForExport(): List<Pair<String, File>> {
    return MANAGED_EMULATOR_FILE_ORDER.mapNotNull { fileName ->
      resolveManagedExportSource(fileName)
        ?.takeIf { it.isFile }
        ?.let { fileName to it }
    }
  }

  private fun resolveSnapshotFilesForExport(): List<Pair<String, File>> {
    val roots = buildList {
      add(File(filesDir, "x1box/snapshots"))
      add(File(filesDir, "xaniteog/snapshots"))
      getExternalFilesDir(null)?.let { external ->
        add(File(external, "x1box/snapshots"))
        add(File(external, "xaniteog/snapshots"))
      }
    }.distinctBy { it.absolutePath }

    val filesByRelativePath = linkedMapOf<String, File>()
    for (root in roots) {
      if (!root.isDirectory) continue
      root.walkTopDown()
        .filter { it.isFile }
        .forEach { file ->
          val relative = file.relativeTo(root).invariantSeparatorsPath
          filesByRelativePath.putIfAbsent(relative, file)
        }
    }
    return filesByRelativePath.map { (relative, file) ->
      "$SNAPSHOT_ARCHIVE_PREFIX$relative" to file
    }
  }

  private fun importManagedFilesFromZip(uri: Uri) {
    if (isBackupOperationBusy()) {
      return
    }

    isImportingEmulatorFiles = true
    updateEmulatorFilesActionState()
    Toast.makeText(this, R.string.settings_import_emulator_files_working, Toast.LENGTH_SHORT).show()

    Thread {
      val result = runCatching {
        importManagedFilesFromZipInternal(uri)
      }

      runOnUiThread {
        isImportingEmulatorFiles = false
        updateEmulatorFilesActionState()
        result.onSuccess(::finishManagedFilesImport)
          .onFailure { error ->
            Toast.makeText(
              this,
              getString(
                R.string.settings_import_emulator_files_failed,
                error.message ?: getString(R.string.settings_import_emulator_files_unknown_error),
              ),
              Toast.LENGTH_LONG,
            ).show()
          }
      }
    }.start()
  }

  private fun importManagedFilesFromDocuments(uris: List<Uri>) {
    if (isBackupOperationBusy()) {
      return
    }

    isImportingEmulatorFiles = true
    updateEmulatorFilesActionState()
    Toast.makeText(this, R.string.settings_import_emulator_files_working, Toast.LENGTH_SHORT).show()

    Thread {
      val result = runCatching {
        importManagedFilesFromDocumentsInternal(uris)
      }

      runOnUiThread {
        isImportingEmulatorFiles = false
        updateEmulatorFilesActionState()
        result.onSuccess(::finishManagedFilesImport)
          .onFailure { error ->
            Toast.makeText(
              this,
              getString(
                R.string.settings_import_emulator_files_failed,
                error.message ?: getString(R.string.settings_import_emulator_files_unknown_error),
              ),
              Toast.LENGTH_LONG,
            ).show()
          }
      }
    }.start()
  }

  private fun importManagedFilesFromZipInternal(uri: Uri): List<String> {
    ensureManagedFilesRoot()
    val editor = prefs.edit()
    val importedNames = linkedSetOf<String>()
    var settingsPayload: ByteArray? = null
    var importedSnapshotCount = 0
    var entryCount = 0

    contentResolver.openInputStream(uri)?.use { rawInput ->
      ZipInputStream(BufferedInputStream(rawInput)).use { zip ->
        while (true) {
          val entry = zip.nextEntry ?: break
          entryCount++
          if (entryCount > 10_000) throw IOException("The backup contains too many files.")
          if (!entry.isDirectory) {
            val archiveName = entry.name.replace('\\', '/').trimStart('/')
            when {
              archiveName == PREFERENCES_ARCHIVE_ENTRY -> {
                settingsPayload = readZipEntryBytes(zip, MAX_PREFERENCES_BACKUP_BYTES)
              }
              archiveName.startsWith(SNAPSHOT_ARCHIVE_PREFIX) -> {
                val relative = normalizeSnapshotArchivePath(archiveName)
                if (relative != null) {
                  val target = File(primarySnapshotDirectory(), relative)
                  copyZipEntryToFile(zip, target)
                  importedSnapshotCount++
                }
              }
              else -> {
                val normalizedName = normalizeManagedFileName(archiveName)
                if (normalizedName != null && importedNames.add(normalizedName)) {
                  val target = resolveManagedImportTarget(normalizedName)
                  copyZipEntryToFile(zip, target)
                }
              }
            }
          }
          zip.closeEntry()
        }
      }
    } ?: throw IOException(getString(R.string.settings_import_emulator_files_open_failed))

    if (importedNames.isEmpty() && settingsPayload == null && importedSnapshotCount == 0) {
      throw IOException(getString(R.string.settings_import_emulator_files_zip_empty))
    }

    for (fileName in sortManagedFileNames(importedNames)) {
      applyImportedManagedFile(fileName, resolveManagedImportTarget(fileName), editor)
    }
    if (!editor.commit()) throw IOException("The imported file locations could not be saved.")
    settingsPayload?.let { restorePreferencesBackupJson(it.toString(Charsets.UTF_8)) }

    return buildList {
      addAll(sortManagedFileNames(importedNames))
      if (settingsPayload != null) add("settings")
      if (importedSnapshotCount > 0) add("$importedSnapshotCount save states")
    }
  }

  private fun importManagedFilesFromDocumentsInternal(uris: List<Uri>): List<String> {
    ensureManagedFilesRoot()
    val selectedFiles = linkedMapOf<String, Uri>()

    for (uri in uris) {
      val rawName = getFileName(uri) ?: uri.lastPathSegment ?: continue
      val normalizedName = normalizeManagedFileName(rawName) ?: continue
      if (selectedFiles.containsKey(normalizedName)) {
        throw IOException(getString(R.string.settings_import_emulator_files_duplicate, normalizedName))
      }
      selectedFiles[normalizedName] = uri
    }

    if (selectedFiles.isEmpty()) {
      throw IOException(getString(R.string.settings_import_emulator_files_pick_files_error))
    }

    val editor = prefs.edit()
    for ((fileName, uri) in selectedFiles) {
      val target = resolveManagedImportTarget(fileName)
      copyUriToFile(uri, target)
    }
    for (fileName in sortManagedFileNames(selectedFiles.keys)) {
      applyImportedManagedFile(fileName, resolveManagedImportTarget(fileName), editor)
    }
    editor.apply()

    return sortManagedFileNames(selectedFiles.keys)
  }

  private fun finishManagedFilesImport(importedNames: List<String>) {
    val summary = importedNames.joinToString(", ")
    Toast.makeText(
      this,
      getString(R.string.settings_import_emulator_files_success, summary),
      Toast.LENGTH_LONG,
    ).show()
    recreate()
  }

  private fun buildPreferencesBackupJson(): String {
    val stores = JSONObject()
    for (storeName in BACKUP_PREFERENCE_STORES) {
      val source = getSharedPreferences(storeName, Context.MODE_PRIVATE)
      val values = JSONObject()
      for ((key, value) in source.all.toSortedMap()) {
        if (!isPortablePreferenceKey(key)) continue
        val encoded = encodePreferenceValue(value) ?: continue
        values.put(key, encoded)
      }
      stores.put(storeName, values)
    }

    return JSONObject()
      .put("format", PREFERENCES_BACKUP_FORMAT)
      .put("version", PREFERENCES_BACKUP_VERSION)
      .put("createdAt", System.currentTimeMillis())
      .put("preferences", stores)
      .toString()
  }

  private fun encodePreferenceValue(value: Any?): JSONObject? {
    val encoded = JSONObject()
    when (value) {
      is String -> encoded.put("type", "string").put("value", value)
      is Boolean -> encoded.put("type", "boolean").put("value", value)
      is Int -> encoded.put("type", "int").put("value", value)
      is Long -> encoded.put("type", "long").put("value", value)
      is Float -> encoded.put("type", "float").put("value", value.toDouble())
      is Set<*> -> {
        val strings = value.filterIsInstance<String>()
        if (strings.size != value.size) return null
        encoded.put("type", "string_set").put("value", JSONArray(strings.sorted()))
      }
      else -> return null
    }
    return encoded
  }

  private fun restorePreferencesBackupJson(raw: String) {
    val root = try {
      JSONObject(raw)
    } catch (error: Exception) {
      throw IOException("The settings backup is not valid JSON.", error)
    }
    if (root.optString("format") != PREFERENCES_BACKUP_FORMAT ||
      root.optInt("version", -1) != PREFERENCES_BACKUP_VERSION
    ) {
      throw IOException("The settings backup format is not supported.")
    }
    val stores = root.optJSONObject("preferences")
      ?: throw IOException("The settings backup does not contain preferences.")

    var restoredKeyCount = 0
    for (storeName in BACKUP_PREFERENCE_STORES) {
      val values = stores.optJSONObject(storeName) ?: continue
      val destination = getSharedPreferences(storeName, Context.MODE_PRIVATE)
      val editor = destination.edit()
      destination.all.keys
        .filter(::isPortablePreferenceKey)
        .forEach(editor::remove)

      val keys = values.keys()
      while (keys.hasNext()) {
        val key = keys.next()
        if (!isPortablePreferenceKey(key)) continue
        restoredKeyCount++
        if (restoredKeyCount > 10_000) {
          throw IOException("The settings backup contains too many values.")
        }
        val encoded = values.optJSONObject(key) ?: continue
        when (encoded.optString("type")) {
          "string" -> if (!encoded.isNull("value")) editor.putString(key, encoded.getString("value"))
          "boolean" -> editor.putBoolean(key, encoded.getBoolean("value"))
          "int" -> editor.putInt(key, encoded.getInt("value"))
          "long" -> editor.putLong(key, encoded.getLong("value"))
          "float" -> editor.putFloat(key, encoded.getDouble("value").toFloat())
          "string_set" -> {
            val array = encoded.optJSONArray("value") ?: continue
            val restored = linkedSetOf<String>()
            for (index in 0 until array.length()) {
              if (!array.isNull(index)) restored.add(array.getString(index))
            }
            editor.putStringSet(key, restored)
          }
        }
      }
      if (!editor.commit()) throw IOException("The imported settings could not be saved.")
    }
  }

  private fun isPortablePreferenceKey(key: String): Boolean {
    if (key in NON_PORTABLE_PREFERENCE_KEYS || key.startsWith("runtime_override_")) return false
    return !key.endsWith("Path", ignoreCase = true) && !key.endsWith("Uri", ignoreCase = true)
  }

  private fun readZipEntryBytes(zip: ZipInputStream, maxBytes: Int): ByteArray {
    val output = ByteArrayOutputStream()
    val buffer = ByteArray(64 * 1024)
    var total = 0
    while (true) {
      val read = zip.read(buffer)
      if (read < 0) break
      total += read
      if (total > maxBytes) throw IOException("The settings backup is too large.")
      output.write(buffer, 0, read)
    }
    return output.toByteArray()
  }

  private fun normalizeSnapshotArchivePath(archiveName: String): String? {
    val relative = archiveName
      .removePrefix(SNAPSHOT_ARCHIVE_PREFIX)
      .replace('\\', '/')
      .trim('/')
    if (relative.isBlank() || relative.length > 512) return null
    val segments = relative.split('/')
    if (segments.any { it.isBlank() || it == "." || it == ".." }) return null
    return segments.joinToString("/")
  }

  private fun primarySnapshotDirectory(): File {
    return File(filesDir, "x1box/snapshots")
  }

  private fun copyZipEntryToFile(zip: ZipInputStream, target: File) {
    val parent = target.parentFile
    if (parent != null && !parent.exists() && !parent.mkdirs()) {
      throw IOException("Failed to prepare ${parent.absolutePath}.")
    }
    FileOutputStream(target).use { output ->
      zip.copyTo(output)
    }
  }

  private fun resolveManagedFilesRoot(): File {
    val base = getExternalFilesDir(null) ?: filesDir
    return File(base, "XaniteOG")
  }

  private fun managedFilesRoots(): List<File> {
    val storageRoots = buildList {
      getExternalFilesDir(null)?.let(::add)
      add(filesDir)
    }
    return storageRoots.flatMap { root ->
      listOf(File(root, "XaniteOG"), File(root, "xaniteog"), File(root, "x1box"))
    }.distinctBy { it.absolutePath }
  }

  private fun ensureManagedFilesRoot(): File {
    val dir = resolveManagedFilesRoot()
    if (!dir.exists() && !dir.mkdirs()) {
      throw IOException("Failed to prepare the emulator files folder.")
    }
    return dir
  }

  private fun resolveManagedImportTarget(fileName: String): File {
    return File(resolveManagedFilesRoot(), fileName)
  }

  private fun resolveManagedExportSource(fileName: String): File? {
    return when (fileName) {
      "mcpx.bin" -> resolveConfiguredFileOrLocalFallback("mcpxPath", "mcpx.bin")
      "flash.bin" -> resolveConfiguredFileOrLocalFallback("flashPath", "flash.bin")
      "hdd.img" -> resolveConfiguredFileOrLocalFallback("hddPath", "hdd.img")
      "eeprom.bin" -> resolveEepromFile().takeIf { it.isFile }
      "xaniteog.toml" -> managedFilesRoots()
        .asSequence()
        .map { root -> File(root, "xaniteog.toml") }
        .firstOrNull { it.isFile }
      else -> null
    }
  }

  private fun resolveConfiguredFileOrLocalFallback(pathKey: String, fallbackName: String): File? {
    val configuredFile = prefs.getString(pathKey, null)
      ?.let(::File)
      ?.takeIf { it.isFile }
    if (configuredFile != null) {
      return configuredFile
    }

    return managedFilesRoots()
      .asSequence()
      .map { root -> File(root, fallbackName) }
      .firstOrNull { it.isFile }
  }

  private fun normalizeManagedFileName(rawName: String): String? {
    val trimmed = rawName.replace('\\', '/').substringAfterLast('/').trim()
    if (trimmed.isBlank()) {
      return null
    }
    val normalized = trimmed.lowercase(Locale.US)
    return normalized.takeIf { it in MANAGED_EMULATOR_FILE_NAMES }
  }

  private fun sortManagedFileNames(names: Iterable<String>): List<String> {
    return names.sortedBy { MANAGED_EMULATOR_FILE_ORDER.indexOf(it) }
  }

  private fun applyImportedManagedFile(
    fileName: String,
    target: File,
    editor: android.content.SharedPreferences.Editor,
  ) {
    when (fileName) {
      "mcpx.bin" -> editor.putString("mcpxPath", target.absolutePath).remove("mcpxUri")
      "flash.bin" -> editor.putString("flashPath", target.absolutePath).remove("flashUri")
      "hdd.img" -> editor.putString("hddPath", target.absolutePath).remove("hddUri")
      "xaniteog.toml" -> applyImportedConfigToml(target, editor)
    }
  }

  private fun applyImportedConfigToml(
    file: File,
    editor: android.content.SharedPreferences.Editor,
  ) {
    val sections = parseSimpleTomlSections(file)

    resolveImportedConfigFileReference(
      rawPath = parseTomlString(sections, "sys.files", "bootrom_path"),
      managedFileName = "mcpx.bin",
    )?.let { resolved ->
      editor.putString("mcpxPath", resolved.absolutePath).remove("mcpxUri")
    }
    resolveImportedConfigFileReference(
      rawPath = parseTomlString(sections, "sys.files", "flashrom_path"),
      managedFileName = "flash.bin",
    )?.let { resolved ->
      editor.putString("flashPath", resolved.absolutePath).remove("flashUri")
    }
    resolveImportedConfigFileReference(
      rawPath = parseTomlString(sections, "sys.files", "hdd_path"),
      managedFileName = "hdd.img",
    )?.let { resolved ->
      editor.putString("hddPath", resolved.absolutePath).remove("hddUri")
    }

    parseTomlBoolean(sections, "general", "skip_boot_anim")
      ?.let { editor.putBoolean("setting_skip_boot_anim", it) }
    parseTomlString(sections, "display", "renderer")
      ?.lowercase(Locale.US)
      ?.takeIf { it == "opengl" || it == "vulkan" }
      ?.let { editor.putString("setting_renderer", it) }
    parseTomlString(sections, "display", "filtering")
      ?.lowercase(Locale.US)
      ?.takeIf { it == "linear" || it == "nearest" }
      ?.let { editor.putString("setting_filtering", it) }
    parseTomlBoolean(sections, "display.window", "vsync")
      ?.let { editor.putBoolean("setting_vsync", it) }
    parseTomlInt(sections, "display.quality", "surface_scale")
      ?.coerceIn(0, 5)
      ?.let { editor.putInt("setting_surface_scale", it) }
    parseTomlBoolean(sections, "audio", "use_dsp")
      ?.let { editor.putBoolean("setting_use_dsp", it) }
    parseTomlBoolean(sections, "audio", "use_dsp_jit")
      ?.let { editor.putBoolean("setting_use_dsp_jit", it) }
    parseTomlBoolean(sections, "audio", "hrtf")
      ?.let { editor.putBoolean(PREF_HRTF, it) }
    parseTomlBoolean(sections, "perf", "cache_shaders")
      ?.let { editor.putBoolean("setting_cache_shaders", it) }
    (parseTomlBoolean(sections, "perf", "fp_jit")
      ?: parseTomlBoolean(sections, "perf", "hard_fpu"))
      ?.let { editor.putBoolean("setting_hard_fpu", it) }
    parseTomlString(sections, "android", "tcg_thread")
      ?.lowercase(Locale.US)
      ?.takeIf { it == "single" || it == "multi" }
      ?.let { editor.putString("setting_tcg_thread", it) }
    parseTomlString(sections, "android", "audio_driver")
      ?.let(::normalizeImportedAudioDriver)
      ?.let { editor.putString("setting_audio_driver", it) }
    parseTomlBoolean(sections, "net", "enable")
      ?.let { editor.putBoolean("setting_network_enable", it) }
    parseTomlInt(sections, "sys", "mem_limit")
      ?.takeIf { it == 64 || it == 128 }
      ?.let { editor.putInt("setting_system_memory_mib", it) }
  }

  private fun parseSimpleTomlSections(file: File): Map<String, Map<String, String>> {
    val sections = linkedMapOf<String, MutableMap<String, String>>()
    var currentSection = ""

    file.forEachLine { rawLine ->
      val trimmed = stripTomlComment(rawLine).trim()
      if (trimmed.isBlank()) {
        return@forEachLine
      }
      if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
        currentSection = trimmed.substring(1, trimmed.length - 1).trim()
        return@forEachLine
      }

      val separator = trimmed.indexOf('=')
      if (separator <= 0) {
        return@forEachLine
      }

      val key = trimmed.substring(0, separator).trim()
      val value = trimmed.substring(separator + 1).trim()
      if (key.isEmpty() || value.isEmpty()) {
        return@forEachLine
      }

      sections.getOrPut(currentSection) { linkedMapOf() }[key] = value
    }

    return sections
  }

  private fun stripTomlComment(line: String): String {
    var inString = false
    var escaping = false

    for ((index, char) in line.withIndex()) {
      when {
        char == '"' && !escaping -> inString = !inString
        char == '#' && !inString -> return line.substring(0, index)
      }

      escaping = if (char == '\\' && inString) {
        !escaping
      } else {
        false
      }
    }

    return line
  }

  private fun parseTomlBoolean(
    sections: Map<String, Map<String, String>>,
    section: String,
    key: String,
  ): Boolean? {
    return when (sections[section]?.get(key)?.trim()?.lowercase(Locale.US)) {
      "true" -> true
      "false" -> false
      else -> null
    }
  }

  private fun parseTomlInt(
    sections: Map<String, Map<String, String>>,
    section: String,
    key: String,
  ): Int? {
    val rawValue = sections[section]?.get(key)?.trim() ?: return null
    return rawValue.toIntOrNull() ?: decodeTomlString(rawValue)?.toIntOrNull()
  }

  private fun parseTomlString(
    sections: Map<String, Map<String, String>>,
    section: String,
    key: String,
  ): String? {
    val rawValue = sections[section]?.get(key)?.trim() ?: return null
    return decodeTomlString(rawValue) ?: rawValue
  }

  private fun decodeTomlString(rawValue: String): String? {
    if (rawValue.length < 2 || rawValue.first() != '"' || rawValue.last() != '"') {
      return null
    }

    val inner = rawValue.substring(1, rawValue.length - 1)
    return inner
      .replace("\\\\", "\\")
      .replace("\\\"", "\"")
  }

  private fun normalizeImportedAudioDriver(rawValue: String): String? {
    return when (rawValue.trim().lowercase(Locale.US)) {
      "android",
      "audiotrack",
      "opensl",
      "opensles",
      "opensl_es",
      "opensl-es",
      "openslesaudio",
      "openslesbackend" -> "openslES"
      "aaudio" -> "aaudio"
      "dummy", "disabled" -> "dummy"
      else -> null
    }
  }

  private fun resolveImportedConfigFileReference(
    rawPath: String?,
    managedFileName: String,
  ): File? {
    val trimmed = rawPath?.trim().orEmpty()
    if (trimmed.isEmpty()) {
      return null
    }

    val directFile = File(trimmed)
    if (directFile.isFile) {
      return directFile
    }

    return if (directFile.name.lowercase(Locale.US) == managedFileName.lowercase(Locale.US)) {
      resolveManagedImportTarget(managedFileName).takeIf { it.isFile }
    } else {
      null
    }
  }

  private fun setAdvancedExperimentalExpanded(expanded: Boolean) {
    layoutAdvancedExperimentalContent.visibility = if (expanded) View.VISIBLE else View.GONE
    btnToggleAdvancedExperimental.text = getString(
      if (expanded) {
        R.string.settings_advanced_experimental_hide
      } else {
        R.string.settings_advanced_experimental_show
      }
    )
    prefs.edit().putBoolean(PREF_ADVANCED_EXPERIMENTAL_EXPANDED, expanded).apply()
  }

  private fun exportDebugLog(uri: Uri) {
    try {
      contentResolver.openOutputStream(uri, "w")?.use { stream ->
        DebugLog.exportCombined(this, stream)
      } ?: throw IOException("Could not open the selected export location.")
      Toast.makeText(this, R.string.settings_export_debug_log_success, Toast.LENGTH_LONG).show()
    } catch (error: Exception) {
      Toast.makeText(
        this,
        getString(R.string.settings_export_debug_log_failed, error.message ?: "unknown error"),
        Toast.LENGTH_LONG
      ).show()
    }
  }

  /**
   * Updates the "Last Crash" row to reflect whatever's currently on disk.
   * Called once in onCreate and again in onResume, since a crash in a
   * separate process (e.g. the game, running in :xaniteog) could have happened
   * while Settings wasn't the active screen.
   */
  private fun refreshCrashSection(
    statusText: TextView,
    btnCopy: MaterialButton,
    btnDismiss: MaterialButton,
  ) {
    val timestamp = CrashReportManager.latestCrashTimestampLabel(this)
    if (timestamp == null) {
      statusText.text = getString(R.string.settings_last_crash_none)
      btnCopy.isEnabled = false
      btnDismiss.isEnabled = false
    } else {
      statusText.text = getString(R.string.settings_last_crash_detected, timestamp)
      btnCopy.isEnabled = true
      btnDismiss.isEnabled = true
    }
  }

  /**
   * Updates the "Last Session" row — unlike the crash row, a status here
   * isn't inherently bad news: latestSessionStatusLabel() reports either
   * "No issues: <time>" for a normal healthy session, or "Issue detected:
   * <time>" when xaniteog_android.cpp flagged a boot stall, instability, or
   * repeated rendering errors during that session.
   */
  private fun refreshSessionSection(
    statusText: TextView,
    btnCopy: MaterialButton,
    btnDismiss: MaterialButton,
  ) {
    if (!CrashReportManager.isActiveDevLogsEnabled(this)) {
      statusText.text = getString(R.string.settings_last_session_disabled)
      btnCopy.isEnabled = false
      btnDismiss.isEnabled = false
      return
    }
    val status = CrashReportManager.latestSessionStatusLabel(this)
    if (status == null) {
      statusText.text = getString(R.string.settings_last_session_none)
      btnCopy.isEnabled = false
      btnDismiss.isEnabled = false
    } else {
      statusText.text = status
      btnCopy.isEnabled = true
      btnDismiss.isEnabled = true
    }
  }

  override fun onResume() {
    super.onResume()
    refreshCrashSection(
      findViewById(R.id.text_last_crash_status),
      findViewById(R.id.btn_copy_crash_log),
      findViewById(R.id.btn_dismiss_crash_log),
    )
    refreshSessionSection(
      findViewById(R.id.text_last_session_status),
      findViewById(R.id.btn_copy_session_log),
      findViewById(R.id.btn_dismiss_session_log),
    )
  }

  // openExternalLink removed (Insignia URLs moved to XboxLiveActivity)

  // Insignia/Online functionality moved to XboxLiveActivity

  private fun setupEepromEditor() {
    val languageLabels = eepromLanguageOptions.map { getString(it.labelRes) }
    val videoLabels = eepromVideoOptions.map { getString(it.labelRes) }
    val aspectRatioLabels = eepromAspectRatioOptions.map { getString(it.labelRes) }
    val refreshRateLabels = eepromRefreshRateOptions.map { getString(it.labelRes) }

    dropdownEepromLanguage.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, languageLabels)
    )
    dropdownEepromVideoStandard.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, videoLabels)
    )
    dropdownEepromAspectRatio.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, aspectRatioLabels)
    )
    dropdownEepromRefreshRate.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, refreshRateLabels)
    )

    dropdownEepromLanguage.setOnItemClickListener { _, _, position, _ ->
      selectedEepromLanguage = eepromLanguageOptions[position].value
    }
    dropdownEepromVideoStandard.setOnItemClickListener { _, _, position, _ ->
      selectedEepromVideoStandard = eepromVideoOptions[position].value
    }
    dropdownEepromAspectRatio.setOnItemClickListener { _, _, position, _ ->
      selectedEepromAspectRatio = eepromAspectRatioOptions[position].value
    }
    dropdownEepromRefreshRate.setOnItemClickListener { _, _, position, _ ->
      selectedEepromRefreshRate = eepromRefreshRateOptions[position].value
    }

    val eepromFile = resolveEepromFile()
    if (!eepromFile.isFile) {
      eepromEditable = false
      eepromMissing = true
      eepromError = false
      setEepromEditorEnabled(false)
      setEepromLanguageSelection(selectedEepromLanguage)
      setEepromVideoSelection(selectedEepromVideoStandard)
      setEepromVideoSettingsSelection(
        XboxEepromEditor.VideoSettings(
          allow480p = false,
          allow720p = false,
          allow1080i = false,
          aspectRatio = selectedEepromAspectRatio,
          refreshRate = selectedEepromRefreshRate,
        )
      )
      tvEepromStatus.text = getString(
        R.string.settings_eeprom_status_missing,
        eepromFile.absolutePath,
      )
      return
    }

    try {
      val snapshot = XboxEepromEditor.load(eepromFile)
      eepromEditable = true
      eepromMissing = false
      eepromError = false
      setEepromEditorEnabled(true)
      setEepromLanguageSelection(snapshot.language)
      setEepromVideoSelection(snapshot.videoStandard)
      setEepromVideoSettingsSelection(snapshot.videoSettings)

      val hasUnknownValues =
        snapshot.rawLanguage != snapshot.language.id ||
        snapshot.rawVideoStandard != snapshot.videoStandard.id ||
        snapshot.hasManagedVideoSettingsMismatch
      tvEepromStatus.text = if (hasUnknownValues) {
        getString(R.string.settings_eeprom_status_unknown, eepromFile.absolutePath)
      } else {
        getString(R.string.settings_eeprom_status_ready, eepromFile.absolutePath)
      }
    } catch (_: IllegalArgumentException) {
      eepromEditable = false
      eepromMissing = false
      eepromError = true
      setEepromEditorEnabled(false)
      setEepromLanguageSelection(selectedEepromLanguage)
      setEepromVideoSelection(selectedEepromVideoStandard)
      setEepromVideoSettingsSelection(
        XboxEepromEditor.VideoSettings(
          allow480p = false,
          allow720p = false,
          allow1080i = false,
          aspectRatio = selectedEepromAspectRatio,
          refreshRate = selectedEepromRefreshRate,
        )
      )
      tvEepromStatus.text = getString(
        R.string.settings_eeprom_status_invalid,
        eepromFile.absolutePath,
      )
    } catch (_: Exception) {
      eepromEditable = false
      eepromMissing = false
      eepromError = true
      setEepromEditorEnabled(false)
      setEepromLanguageSelection(selectedEepromLanguage)
      setEepromVideoSelection(selectedEepromVideoStandard)
      setEepromVideoSettingsSelection(
        XboxEepromEditor.VideoSettings(
          allow480p = false,
          allow720p = false,
          allow1080i = false,
          aspectRatio = selectedEepromAspectRatio,
          refreshRate = selectedEepromRefreshRate,
        )
      )
      tvEepromStatus.text = getString(
        R.string.settings_eeprom_status_error,
        eepromFile.absolutePath,
      )
    }
  }

  private fun setupOrientationControls() {
    val uiOrientationLabels = uiOrientationOptions.map { getString(it.labelRes) }
    val gameOrientationLabels = gameOrientationOptions.map { getString(it.labelRes) }

    dropdownUiOrientation.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, uiOrientationLabels)
    )
    dropdownGameOrientation.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, gameOrientationLabels)
    )

    dropdownUiOrientation.setOnItemClickListener { _, _, position, _ ->
      selectedUiOrientation = uiOrientationOptions[position].value
      OrientationPreferences.setUiOrientation(this, selectedUiOrientation)
      requestedOrientation = selectedUiOrientation.requestedOrientation
    }
    dropdownGameOrientation.setOnItemClickListener { _, _, position, _ ->
      selectedGameOrientation = gameOrientationOptions[position].value
      OrientationPreferences.setGameOrientation(this, selectedGameOrientation)
    }
  }

  private fun setUiOrientationSelection(orientation: OrientationPreferences.UiOrientation) {
    selectedUiOrientation = orientation
    val option = uiOrientationOptions.firstOrNull { it.value == orientation }
      ?: uiOrientationOptions.first()
    dropdownUiOrientation.setText(getString(option.labelRes), false)
  }

  private fun setGameOrientationSelection(orientation: OrientationPreferences.GameOrientation) {
    selectedGameOrientation = orientation
    val option = gameOrientationOptions.firstOrNull { it.value == orientation }
      ?: gameOrientationOptions.first()
    dropdownGameOrientation.setText(getString(option.labelRes), false)
  }

  private fun setupLanguageControls() {
    val languageLabels = LocaleHelper.supportedLocales.map { it.displayName }

    dropdownAppLanguage.setAdapter(
      ArrayAdapter(this, android.R.layout.simple_list_item_1, languageLabels)
    )

    dropdownAppLanguage.setOnItemClickListener { _, _, position, _ ->
      val selectedCode = LocaleHelper.supportedLocales[position].code
      val currentCode = LocaleHelper.getCurrentLocaleCode(this)
      if (selectedCode != currentCode) {
        LocaleHelper.setLocale(this, selectedCode)
        recreate()
      }
    }
  }

  private fun setLanguageSelection(code: String) {
    val option = LocaleHelper.supportedLocales.firstOrNull { it.code == code }
      ?: LocaleHelper.supportedLocales.first()
    dropdownAppLanguage.setText(option.displayName, false)
  }

  private fun applyEepromEdits(): Pair<Int, Int> {
    if (eepromMissing) {
      return Pair(R.string.settings_saved_eeprom_missing, Toast.LENGTH_LONG)
    }
    if (eepromError || !eepromEditable) {
      return Pair(R.string.settings_saved_eeprom_failed, Toast.LENGTH_LONG)
    }

    return try {
      val changed = XboxEepromEditor.apply(
        resolveEepromFile(),
        selectedEepromLanguage,
        selectedEepromVideoStandard,
        XboxEepromEditor.VideoSettings(
          allow480p = switchEeprom480p.isChecked,
          allow720p = switchEeprom720p.isChecked,
          allow1080i = switchEeprom1080i.isChecked,
          aspectRatio = selectedEepromAspectRatio,
          refreshRate = selectedEepromRefreshRate,
        ),
      )
      if (changed) {
        Pair(R.string.settings_saved_with_eeprom, Toast.LENGTH_SHORT)
      } else {
        Pair(R.string.settings_saved, Toast.LENGTH_SHORT)
      }
    } catch (_: Exception) {
      Pair(R.string.settings_saved_eeprom_failed, Toast.LENGTH_LONG)
    }
  }

  private fun setEepromEditorEnabled(enabled: Boolean) {
    inputEepromLanguage.isEnabled = enabled
    inputEepromVideoStandard.isEnabled = enabled
    inputEepromAspectRatio.isEnabled = enabled
    inputEepromRefreshRate.isEnabled = enabled
    dropdownEepromLanguage.isEnabled = enabled
    dropdownEepromVideoStandard.isEnabled = enabled
    dropdownEepromAspectRatio.isEnabled = enabled
    dropdownEepromRefreshRate.isEnabled = enabled
    switchEeprom480p.isEnabled = enabled
    switchEeprom720p.isEnabled = enabled
    switchEeprom1080i.isEnabled = enabled
  }

  private fun setEepromLanguageSelection(language: XboxEepromEditor.Language) {
    selectedEepromLanguage = language
    val option = eepromLanguageOptions.firstOrNull { it.value == language }
      ?: eepromLanguageOptions.first()
    dropdownEepromLanguage.setText(getString(option.labelRes), false)
  }

  private fun setEepromVideoSelection(video: XboxEepromEditor.VideoStandard) {
    selectedEepromVideoStandard = video
    val option = eepromVideoOptions.firstOrNull { it.value == video }
      ?: eepromVideoOptions.first()
    dropdownEepromVideoStandard.setText(getString(option.labelRes), false)
  }

  private fun setEepromVideoSettingsSelection(videoSettings: XboxEepromEditor.VideoSettings) {
    switchEeprom480p.isChecked = videoSettings.allow480p
    switchEeprom720p.isChecked = videoSettings.allow720p
    switchEeprom1080i.isChecked = videoSettings.allow1080i
    setEepromAspectRatioSelection(videoSettings.aspectRatio)
    setEepromRefreshRateSelection(videoSettings.refreshRate)
  }

  private fun setEepromAspectRatioSelection(aspectRatio: XboxEepromEditor.AspectRatio) {
    selectedEepromAspectRatio = aspectRatio
    val option = eepromAspectRatioOptions.firstOrNull { it.value == aspectRatio }
      ?: eepromAspectRatioOptions.first()
    dropdownEepromAspectRatio.setText(getString(option.labelRes), false)
  }

  private fun setEepromRefreshRateSelection(refreshRate: XboxEepromEditor.RefreshRate) {
    selectedEepromRefreshRate = refreshRate
    val option = eepromRefreshRateOptions.firstOrNull { it.value == refreshRate }
      ?: eepromRefreshRateOptions.first()
    dropdownEepromRefreshRate.setText(getString(option.labelRes), false)
  }

  private fun showClearCacheConfirmation() {
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_clear_cache_title)
      .setMessage(R.string.settings_clear_cache_message)
      .setPositiveButton(R.string.settings_clear_cache_action) { _, _ ->
        val result = clearSystemCache()
        val messageRes = when {
          result.hadFailures -> R.string.settings_clear_cache_partial
          result.deletedEntries > 0 -> R.string.settings_clear_cache_success
          else -> R.string.settings_clear_cache_empty
        }
        Toast.makeText(this, getString(messageRes), Toast.LENGTH_SHORT).show()
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun showInitializeHddLayoutPicker(button: MaterialButton) {
    val hddFile = resolveHddFile()
    if (hddFile == null) {
      refreshHddToolsPreview(button)
      return
    }

    val inspection = runCatching { XboxHddFormatter.inspect(hddFile) }.getOrElse { error ->
      tvHddToolsStatus.text = getString(
        R.string.settings_hdd_status_error,
        error.message ?: hddFile.absolutePath,
      )
      button.isEnabled = false
      return
    }

    if (!inspection.supportsRetailFormat) {
      refreshHddToolsState(button)
      return
    }

    val supportedLayouts = XboxHddFormatter.supportedLayouts(inspection).toSet()
    if (supportedLayouts.isEmpty()) {
      refreshHddToolsState(button)
      return
    }

    val allLayouts = XboxHddFormatter.Layout.entries
    val labels = allLayouts
      .map { layout ->
        val label = getString(hddLayoutLabelRes(layout))
        val availability = XboxHddFormatter.availabilityFor(inspection, layout)
        if (availability == XboxHddFormatter.LayoutAvailability.AVAILABLE) {
          label
        } else {
          getString(
            R.string.settings_hdd_layout_unavailable_format,
            label,
            getString(hddLayoutUnavailableReasonRes(availability)),
          )
        }
      }
      .toTypedArray()
    val dp = resources.displayMetrics.density
    lateinit var hddDialog: androidx.appcompat.app.AlertDialog

    val buttonList = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding((20 * dp).toInt(), (12 * dp).toInt(), (20 * dp).toInt(), 0)
      labels.forEachIndexed { i, label ->
        addView(MaterialButton(this@SettingsActivity, null,
          com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
          text = label
          isAllCaps = false
          layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
          ).also { lp -> lp.bottomMargin = (8 * dp).toInt() }
          setOnClickListener {
            hddDialog.dismiss()
            val layout = allLayouts[i]
            val availability = XboxHddFormatter.availabilityFor(inspection, layout)
            if (availability == XboxHddFormatter.LayoutAvailability.AVAILABLE) {
              showInitializeHddConfirmation(hddFile, layout, button)
            } else {
              MaterialAlertDialogBuilder(this@SettingsActivity, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
                .setTitle(R.string.settings_hdd_layout_unavailable_title)
                .setMessage(getString(hddLayoutUnavailableReasonRes(availability)))
                .setPositiveButton(android.R.string.ok, null)
                .show()
            }
          }
        })
      }
    }

    hddDialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_hdd_layout_pick_title)
      .setView(buttonList)
      .setNegativeButton(android.R.string.cancel, null)
      .create()
    hddDialog.show()
  }

  private fun showInitializeHddConfirmation(
    hddFile: File,
    layout: XboxHddFormatter.Layout,
    button: MaterialButton,
  ) {
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_hdd_init_title)
      .setMessage(
        getString(
          R.string.settings_hdd_init_message,
          getString(hddLayoutLabelRes(layout)),
          getString(hddLayoutSummaryRes(layout)),
          hddFile.absolutePath,
        )
      )
      .setPositiveButton(R.string.settings_hdd_init_action) { _, _ ->
        initializeHddLayout(hddFile, layout, button)
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun initializeHddLayout(
    hddFile: File,
    layout: XboxHddFormatter.Layout,
    button: MaterialButton,
  ) {
    if (isInitializingHdd) {
      return
    }

    isInitializingHdd = true
    button.isEnabled = false
    Toast.makeText(this, R.string.settings_hdd_init_working, Toast.LENGTH_SHORT).show()

    Thread {
      val result = runCatching {
        XboxHddFormatter.initialize(hddFile, layout)
      }

      runOnUiThread {
        isInitializingHdd = false
        refreshHddToolsState(button)
        result.onSuccess {
          Toast.makeText(this, R.string.settings_hdd_init_success, Toast.LENGTH_SHORT).show()
        }.onFailure { error ->
          Toast.makeText(
            this,
            getString(
              R.string.settings_hdd_init_failed,
              error.message ?: hddFile.absolutePath,
            ),
            Toast.LENGTH_LONG,
          ).show()
        }
      }
    }.start()
  }

  private fun refreshHddToolsState(button: MaterialButton) {
    val hddFile = resolveHddFile()
    if (hddFile == null) {
      tvHddToolsStatus.text = getString(R.string.settings_hdd_status_missing)
      button.isEnabled = false
      return
    }

    val inspection = runCatching { XboxHddFormatter.inspect(hddFile) }.getOrElse { error ->
      tvHddToolsStatus.text = getString(
        R.string.settings_hdd_status_error,
        error.message ?: hddFile.absolutePath,
      )
      button.isEnabled = false
      return
    }

    val sizeLabel = Formatter.formatFileSize(this, inspection.totalBytes)
    val formatLabel = getString(hddFormatLabelRes(inspection.format))
    tvHddToolsStatus.text = when {
      inspection.totalBytes < XboxHddFormatter.MINIMUM_RETAIL_DISK_BYTES -> getString(
        R.string.settings_hdd_status_too_small,
        formatLabel,
        sizeLabel,
        hddFile.absolutePath,
      )
      else -> getString(
        R.string.settings_hdd_status_ready,
        formatLabel,
        sizeLabel,
        hddFile.absolutePath,
      )
    }
    button.isEnabled = !isInitializingHdd && XboxHddFormatter.supportedLayouts(inspection).isNotEmpty()
  }

  private fun refreshHddToolsPreview(button: MaterialButton) {
    val hddFile = resolveHddFile()
    if (hddFile == null || !hddFile.isFile) {
      tvHddToolsStatus.text = getString(R.string.settings_hdd_status_missing)
      button.isEnabled = false
      return
    }

    tvHddToolsStatus.text = getString(
      R.string.settings_hdd_status_configured,
      hddFile.absolutePath,
    )
    button.isEnabled = !isInitializingHdd
  }

  private fun showDashboardImportSourcePicker() {
    val hddFile = resolveHddFile()
    if (hddFile == null) {
      Toast.makeText(this, R.string.settings_hdd_status_missing, Toast.LENGTH_LONG).show()
      return
    }
    if (isImportingDashboard) {
      return
    }

    val labels = arrayOf(
      getString(R.string.settings_dashboard_import_source_zip),
      getString(R.string.settings_dashboard_import_source_folder),
    )
    val dp = resources.displayMetrics.density
    lateinit var importDialog: androidx.appcompat.app.AlertDialog

    val buttonList = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding((20 * dp).toInt(), (12 * dp).toInt(), (20 * dp).toInt(), 0)
      labels.forEachIndexed { i, label ->
        addView(MaterialButton(this@SettingsActivity, null,
          com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
          text = label
          layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
          ).also { lp -> lp.bottomMargin = (8 * dp).toInt() }
          setOnClickListener {
            importDialog.dismiss()
            when (i) {
              0 -> pickDashboardZip.launch(arrayOf("application/zip", "application/octet-stream"))
              else -> pickDashboardFolder.launch(null)
            }
          }
        })
      }
    }

    importDialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_dashboard_import_source_title)
      .setView(buttonList)
      .setNegativeButton(android.R.string.cancel, null)
      .create()
    importDialog.show()
  }

  private fun prepareDashboardImportFromZip(uri: Uri) {
    val hddFile = resolveHddFile()
    if (hddFile == null) {
      Toast.makeText(this, R.string.settings_hdd_status_missing, Toast.LENGTH_LONG).show()
      return
    }

    startDashboardImportPreparation(hddFile) { workingDir ->
      extractDashboardZipToDirectory(uri, workingDir)
    }
  }

  private fun prepareDashboardImportFromFolder(uri: Uri) {
    val hddFile = resolveHddFile()
    if (hddFile == null) {
      Toast.makeText(this, R.string.settings_hdd_status_missing, Toast.LENGTH_LONG).show()
      return
    }

    startDashboardImportPreparation(hddFile) { workingDir ->
      copyDashboardTreeToDirectory(uri, workingDir)
    }
  }

  private fun startDashboardImportPreparation(
    hddFile: File,
    prepareSource: (File) -> File,
  ) {
    if (isImportingDashboard) {
      return
    }

    isImportingDashboard = true
    btnImportDashboard.isEnabled = false
    Toast.makeText(this, R.string.settings_dashboard_import_preparing, Toast.LENGTH_SHORT).show()

    Thread {
      var workingDir: File? = null
      val result = runCatching {
        workingDir = createDashboardWorkingDirectory()
        val preparedRoot = prepareSource(workingDir!!)
        val sourceRoot = normalizeDashboardSourceRoot(preparedRoot)
        if (!dashboardSourceHasFiles(sourceRoot)) {
          throw IOException(getString(R.string.settings_dashboard_import_empty))
        }
        val importLayoutRoot = buildDashboardImportLayout(sourceRoot, workingDir!!)
        val bootPreparation = prepareDashboardBootFiles(importLayoutRoot)

        DashboardImportPlan(
          hddFile = hddFile,
          workingDir = workingDir!!,
          sourceDir = importLayoutRoot,
          backupDir = createDashboardBackupDirectory(),
          summary = describeDashboardSource(importLayoutRoot),
          bootNote = bootPreparation.note,
          bootAliasCreated = bootPreparation.aliasCreated,
          retailBootReady = bootPreparation.retailBootReady,
        )
      }

      runOnUiThread {
        result.onSuccess { plan ->
          showDashboardImportConfirmation(plan)
        }.onFailure { error ->
          workingDir?.deleteRecursively()
          isImportingDashboard = false
          btnImportDashboard.isEnabled = true
          Toast.makeText(
            this,
            getString(
              R.string.settings_dashboard_import_failed,
              error.message ?: getString(R.string.settings_dashboard_import_empty),
            ),
            Toast.LENGTH_LONG,
          ).show()
        }
      }
    }.start()
  }

  private fun showDashboardImportConfirmation(plan: DashboardImportPlan) {
    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.settings_dashboard_import_title)
      .setMessage(
        buildString {
          append(
            getString(
              R.string.settings_dashboard_import_message,
              plan.summary,
              plan.backupDir.absolutePath,
            )
          )
          if (!plan.bootNote.isNullOrBlank()) {
            append("\n\n")
            append(plan.bootNote)
          }
        }
      )
      .setPositiveButton(R.string.settings_dashboard_import_action) { _, _ ->
        importDashboard(plan)
      }
      .setNegativeButton(android.R.string.cancel) { _, _ ->
        plan.workingDir.deleteRecursively()
        isImportingDashboard = false
        btnImportDashboard.isEnabled = true
      }
      .setOnCancelListener {
        plan.workingDir.deleteRecursively()
        isImportingDashboard = false
        btnImportDashboard.isEnabled = true
      }
      .show()
  }

  private fun importDashboard(plan: DashboardImportPlan) {
    Toast.makeText(this, R.string.settings_dashboard_import_working, Toast.LENGTH_SHORT).show()

    Thread {
      val result = runCatching {
        XboxDashboardImporter.importDashboard(
          hddFile = plan.hddFile,
          sourceRoot = plan.sourceDir,
          backupRoot = plan.backupDir,
        )
      }

      runOnUiThread {
        plan.workingDir.deleteRecursively()
        isImportingDashboard = false
        btnImportDashboard.isEnabled = true
        result.onSuccess {
          val messageRes = when {
            plan.bootAliasCreated -> R.string.settings_dashboard_import_success_with_alias
            !plan.retailBootReady -> R.string.settings_dashboard_import_success_without_retail_boot
            else -> R.string.settings_dashboard_import_success
          }
          Toast.makeText(this, getString(messageRes, plan.backupDir.absolutePath), Toast.LENGTH_LONG).show()
        }.onFailure { error ->
          Toast.makeText(
            this,
            getString(
              R.string.settings_dashboard_import_failed,
              error.message ?: plan.hddFile.absolutePath,
            ),
            Toast.LENGTH_LONG,
          ).show()
        }
      }
    }.start()
  }

  private fun createDashboardWorkingDirectory(): File {
    val dir = File(cacheDir, "dashboard-import-${System.currentTimeMillis()}")
    if (!dir.mkdirs()) {
      throw IOException("Failed to prepare a temporary dashboard import folder.")
    }
    return dir
  }

  private fun createDashboardBackupDirectory(): File {
    val base = getExternalFilesDir(null) ?: filesDir
    val root = File(File(base, "xaniteog"), "dashboard-backups")
    val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
    val dir = File(root, "dashboard-$stamp")
    if (!dir.mkdirs()) {
      throw IOException("Failed to prepare the dashboard backup folder.")
    }
    return dir
  }

  private fun extractDashboardZipToDirectory(uri: Uri, targetDir: File): File {
    val canonicalRoot = targetDir.canonicalFile
    contentResolver.openInputStream(uri)?.use { rawInput ->
      ZipInputStream(BufferedInputStream(rawInput)).use { zip ->
        while (true) {
          val entry = zip.nextEntry ?: break
          if (entry.name.isBlank()) {
            continue
          }
          val outFile = File(targetDir, entry.name).canonicalFile
          val rootPath = canonicalRoot.path + File.separator
          if (outFile.path != canonicalRoot.path && !outFile.path.startsWith(rootPath)) {
            throw IOException("The selected ZIP contains an invalid path.")
          }
          if (entry.isDirectory) {
            if (!outFile.exists() && !outFile.mkdirs()) {
              throw IOException("Failed to create ${outFile.name} from the ZIP.")
            }
            continue
          }

          outFile.parentFile?.let { parent ->
            if (!parent.exists() && !parent.mkdirs()) {
              throw IOException("Failed to create ${parent.name} from the ZIP.")
            }
          }
          FileOutputStream(outFile).use { output ->
            zip.copyTo(output)
          }
          zip.closeEntry()
        }
      }
    } ?: throw IOException("Failed to open the selected dashboard ZIP.")

    return targetDir
  }

  private fun copyDashboardTreeToDirectory(uri: Uri, targetDir: File): File {
    val root = DocumentFile.fromTreeUri(this, uri)
      ?: throw IOException("Failed to open the selected dashboard folder.")
    copyDocumentFileRecursively(root, targetDir)
    return targetDir
  }

  private fun copyDocumentFileRecursively(source: DocumentFile, target: File) {
    if (source.isDirectory) {
      val children = source.listFiles()
      for (child in children) {
        val name = child.name ?: continue
        val childTarget = File(target, name)
        if (child.isDirectory) {
          if (!childTarget.exists() && !childTarget.mkdirs()) {
            throw IOException("Failed to create ${childTarget.name}.")
          }
          copyDocumentFileRecursively(child, childTarget)
        } else if (child.isFile) {
          childTarget.parentFile?.mkdirs()
          contentResolver.openInputStream(child.uri)?.use { input ->
            FileOutputStream(childTarget).use { output ->
              input.copyTo(output)
            }
          } ?: throw IOException("Failed to copy ${child.name}.")
        }
      }
      return
    }

    if (source.isFile) {
      contentResolver.openInputStream(source.uri)?.use { input ->
        FileOutputStream(target).use { output ->
          input.copyTo(output)
        }
      } ?: throw IOException("Failed to copy ${source.name}.")
    }
  }

  private fun normalizeDashboardSourceRoot(root: File): File {
    var current = root

    while (true) {
      val children = dashboardSourceEntries(current)
      if (children.size != 1 || !children.first().isDirectory) {
        break
      }
      current = children.first()
    }

    if (looksLikeDashboardSourceRoot(current)) {
      return current
    }

    return findNestedDashboardSourceRoot(current) ?: current
  }

  private fun buildDashboardImportLayout(sourceRoot: File, workingDir: File): File {
    val entries = sourceRoot.listFiles()
      ?.filterNot { shouldSkipDashboardSourceEntry(it.name) }
      .orEmpty()
    val layoutRoot = File(workingDir, "dashboard-layout")
    if (layoutRoot.exists()) {
      layoutRoot.deleteRecursively()
    }
    if (!layoutRoot.mkdirs()) {
      throw IOException("Failed to prepare the dashboard import layout.")
    }

    val sourceC = entries.firstOrNull { it.isDirectory && it.name.equals("C", ignoreCase = true) }
      ?.let(::normalizeDashboardPartitionRoot)
    val sourceE = entries.firstOrNull { it.isDirectory && it.name.equals("E", ignoreCase = true) }
      ?.let(::normalizeDashboardPartitionRoot)
    val rootEntriesForC = if (sourceC == null) entries.filterNot { entry ->
      entry.isDirectory && (entry.name.equals("C", ignoreCase = true) || entry.name.equals("E", ignoreCase = true))
    } else {
      emptyList()
    }

    sourceC?.let { copyLocalDirectoryContents(it, File(layoutRoot, "C")) }
    if (rootEntriesForC.isNotEmpty()) {
      val targetC = File(layoutRoot, "C")
      for (entry in rootEntriesForC) {
        copyLocalEntry(entry, File(targetC, entry.name))
      }
    }

    sourceE?.let { copyLocalDirectoryContents(it, File(layoutRoot, "E")) }

    return layoutRoot
  }

  private fun normalizeDashboardPartitionRoot(partitionDir: File): File {
    if (!partitionDir.isDirectory) {
      return partitionDir
    }

    var current = partitionDir
    while (true) {
      if (looksLikeDashboardSourceRoot(current)) {
        return current
      }

      val children = dashboardSourceEntries(current).filter { it.isDirectory }
      if (children.size != 1) {
        break
      }
      current = children.first()
    }

    return findNestedDashboardSourceRoot(current) ?: current
  }

  private fun copyLocalDirectoryContents(sourceDir: File, targetDir: File) {
    val children = sourceDir.listFiles().orEmpty()
    if (!targetDir.exists() && !targetDir.mkdirs()) {
      throw IOException("Failed to create ${targetDir.name}.")
    }
    for (child in children) {
      if (shouldSkipDashboardSourceEntry(child.name)) {
        continue
      }
      copyLocalEntry(child, File(targetDir, child.name))
    }
  }

  private fun copyLocalEntry(source: File, target: File) {
    if (source.isDirectory) {
      if (!target.exists() && !target.mkdirs()) {
        throw IOException("Failed to create ${target.name}.")
      }
      for (child in source.listFiles().orEmpty()) {
        if (shouldSkipDashboardSourceEntry(child.name)) {
          continue
        }
        copyLocalEntry(child, File(target, child.name))
      }
      return
    }

    target.parentFile?.let { parent ->
      if (!parent.exists() && !parent.mkdirs()) {
        throw IOException("Failed to create ${parent.name}.")
      }
    }
    source.copyTo(target, overwrite = true)
  }

  private fun prepareDashboardBootFiles(layoutRoot: File): DashboardBootPreparation {
    val cDir = File(layoutRoot, "C")
    if (!cDir.isDirectory || !cDir.exists()) {
      return DashboardBootPreparation(
        note = getString(R.string.settings_dashboard_import_boot_missing_note),
        aliasCreated = false,
        retailBootReady = false,
      )
    }

    val topLevelFiles = cDir.listFiles()
      ?.filter { it.isFile }
      .orEmpty()
    val xboxdash = topLevelFiles.firstOrNull { it.name.equals("xboxdash.xbe", ignoreCase = true) }
    if (xboxdash != null) {
      return DashboardBootPreparation(
        note = null,
        aliasCreated = false,
        retailBootReady = true,
      )
    }

    val candidate = findDashboardBootCandidate(cDir)

    if (candidate != null) {
      val aliasFile = File(cDir, "xboxdash.xbe")
      candidate.copyTo(aliasFile, overwrite = true)
      val relativePath = candidate.relativeTo(cDir).invariantSeparatorsPath
      return DashboardBootPreparation(
        note = getString(R.string.settings_dashboard_import_boot_alias_note, relativePath),
        aliasCreated = true,
        retailBootReady = true,
      )
    }

    return DashboardBootPreparation(
      note = getString(R.string.settings_dashboard_import_boot_missing_note),
      aliasCreated = false,
      retailBootReady = false,
    )
  }

  private fun findDashboardBootCandidate(cDir: File): File? {
    var bestFile: File? = null
    var bestScore = Int.MIN_VALUE

    cDir.walkTopDown().forEach { file ->
      if (!file.isFile || !file.extension.equals("xbe", ignoreCase = true)) {
        return@forEach
      }

      val score = scoreDashboardBootCandidate(cDir, file)
      if (score > bestScore) {
        bestScore = score
        bestFile = file
      }
    }

    return bestFile
  }

  private fun scoreDashboardBootCandidate(cDir: File, candidate: File): Int {
    val relativePath = candidate.relativeTo(cDir).invariantSeparatorsPath.lowercase(Locale.US)
    val fileName = candidate.name.lowercase(Locale.US)
    val baseName = candidate.nameWithoutExtension.lowercase(Locale.US)
    val depth = relativePath.count { it == '/' }
    var score = 0

    score += when (fileName) {
      "xboxdash.xbe" -> 12_000
      "default.xbe" -> 10_000
      "evoxdash.xbe" -> 9_500
      "avalaunch.xbe" -> 9_400
      "unleashx.xbe" -> 9_300
      "xbmc.xbe" -> 9_200
      "nexgen.xbe" -> 9_100
      else -> 0
    }

    if (baseName.contains("dash")) {
      score += 800
    }
    if (relativePath.contains("/dashboard/") || relativePath.contains("/dash/")) {
      score += 500
    }
    if (relativePath.startsWith("dashboard/") || relativePath.startsWith("dash/")) {
      score += 400
    }
    if (relativePath.contains("/apps/") || relativePath.contains("/games/")) {
      score -= 1_000
    }
    if (baseName.contains("installer") || baseName.contains("uninstall") || baseName.contains("config")) {
      score -= 2_000
    }

    score += 300 - (depth * 40)
    return score
  }

  private fun dashboardSourceHasFiles(root: File): Boolean {
    return looksLikeDashboardSourceRoot(root) || findNestedDashboardSourceRoot(root) != null
  }

  private fun dashboardSourceEntries(root: File): List<File> {
    return root.listFiles()
      ?.filterNot { shouldSkipDashboardSourceEntry(it.name) }
      .orEmpty()
  }

  private fun looksLikeDashboardSourceRoot(root: File): Boolean {
    val entries = dashboardSourceEntries(root)
    if (entries.isEmpty()) {
      return false
    }

    val hasPartitionDir = entries.any { entry ->
      entry.isDirectory &&
        (entry.name.equals("C", ignoreCase = true) || entry.name.equals("E", ignoreCase = true)) &&
        dashboardSourceEntries(entry).isNotEmpty()
    }
    if (hasPartitionDir) {
      return true
    }

    return scoreDashboardSourceRoot(root, root) > 0
  }

  private fun findNestedDashboardSourceRoot(root: File): File? {
    var bestDir: File? = null
    var bestScore = Int.MIN_VALUE

    root.walkTopDown()
      .maxDepth(8)
      .forEach { candidate ->
        if (!candidate.isDirectory || candidate == root) {
          return@forEach
        }

        val score = scoreDashboardSourceRoot(root, candidate)
        if (score > bestScore) {
          bestScore = score
          bestDir = candidate
        }
      }

    return bestDir?.takeIf { bestScore > 0 }
  }

  private fun scoreDashboardSourceRoot(searchRoot: File, candidate: File): Int {
    val entries = dashboardSourceEntries(candidate)
    if (entries.isEmpty()) {
      return Int.MIN_VALUE
    }

    val partitionDirs = entries.filter { entry ->
      entry.isDirectory &&
        (entry.name.equals("C", ignoreCase = true) || entry.name.equals("E", ignoreCase = true)) &&
        dashboardSourceEntries(entry).isNotEmpty()
    }
    val directFiles = entries.filter { it.isFile }
    val directDirs = entries.filter { it.isDirectory }

    var score = 0
    if (partitionDirs.isNotEmpty()) {
      score += 10_000
    }
    if (directFiles.any { it.name.equals("xboxdash.xbe", ignoreCase = true) }) {
      score += 9_000
    }
    if (directFiles.any { it.name.equals("msdash.xbe", ignoreCase = true) }) {
      score += 7_000
    }
    if (directFiles.any { it.name.equals("xbox.xtf", ignoreCase = true) }) {
      score += 3_000
    }
    if (directDirs.any { it.name.equals("xodash", ignoreCase = true) }) {
      score += 3_000
    }
    if (directDirs.any { it.name.equals("audio", ignoreCase = true) }) {
      score += 1_500
    }
    if (directDirs.any { it.name.equals("fonts", ignoreCase = true) }) {
      score += 1_500
    }
    if (directFiles.any { it.extension.equals("xbe", ignoreCase = true) }) {
      score += 1_000
    }

    if (score <= 0) {
      return score
    }

    val depth = candidate.relativeTo(searchRoot)
      .invariantSeparatorsPath
      .count { it == '/' } + 1
    return score - (depth * 120)
  }

  private fun describeDashboardSource(root: File): String {
    val sourceC = File(root, "C")
    val sourceE = File(root, "E")
    val hasC = sourceC.isDirectory && sourceC.walkTopDown().any { it.isFile }
    val hasE = sourceE.isDirectory && sourceE.walkTopDown().any { it.isFile }

    return when {
      hasC && hasE -> getString(R.string.settings_dashboard_import_summary_c_e)
      hasE -> getString(R.string.settings_dashboard_import_summary_e)
      else -> getString(R.string.settings_dashboard_import_summary_c)
    }
  }

  private fun shouldSkipDashboardSourceEntry(name: String): Boolean {
    return name == ".DS_Store" || name == "__MACOSX"
  }

  private fun isZipSelection(uri: Uri): Boolean {
    val name = getFileName(uri) ?: uri.lastPathSegment ?: return false
    return name.lowercase(Locale.US).endsWith(".zip")
  }

  private fun copyUriToFile(
    uri: Uri,
    target: File,
    openError: String = "Failed to open the selected file.",
  ) {
    val parent = target.parentFile
    if (parent != null && !parent.exists() && !parent.mkdirs()) {
      throw IOException("Failed to prepare ${parent.absolutePath}.")
    }
    contentResolver.openInputStream(uri)?.use { input ->
      FileOutputStream(target).use { output ->
        input.copyTo(output)
      }
    } ?: throw IOException(openError)
  }

  private fun persistUriPermission(uri: Uri) {
    val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
    try {
      contentResolver.takePersistableUriPermission(uri, flags)
    } catch (_: SecurityException) {
    }
  }

  private fun hasPersistedReadPermission(uri: Uri): Boolean {
    return contentResolver.persistedUriPermissions.any { permission ->
      permission.isReadPermission && permission.uri == uri
    }
  }

  private fun hddFormatLabelRes(format: XboxHddFormatter.ImageFormat): Int {
    return when (format) {
      XboxHddFormatter.ImageFormat.RAW -> R.string.settings_hdd_format_raw
      XboxHddFormatter.ImageFormat.QCOW2 -> R.string.settings_hdd_format_qcow2
    }
  }

  private fun hddLayoutLabelRes(layout: XboxHddFormatter.Layout): Int {
    return when (layout) {
      XboxHddFormatter.Layout.RETAIL -> R.string.settings_hdd_layout_retail
      XboxHddFormatter.Layout.RETAIL_PLUS_F -> R.string.settings_hdd_layout_retail_f
      XboxHddFormatter.Layout.RETAIL_PLUS_F_G -> R.string.settings_hdd_layout_retail_f_g
    }
  }

  private fun hddLayoutSummaryRes(layout: XboxHddFormatter.Layout): Int {
    return when (layout) {
      XboxHddFormatter.Layout.RETAIL -> R.string.settings_hdd_layout_summary_retail
      XboxHddFormatter.Layout.RETAIL_PLUS_F -> R.string.settings_hdd_layout_summary_retail_f
      XboxHddFormatter.Layout.RETAIL_PLUS_F_G -> R.string.settings_hdd_layout_summary_retail_f_g
    }
  }

  private fun hddLayoutUnavailableReasonRes(
    availability: XboxHddFormatter.LayoutAvailability,
  ): Int {
    return when (availability) {
      XboxHddFormatter.LayoutAvailability.AVAILABLE ->
        R.string.settings_hdd_layout_unavailable_not_enough_space
      XboxHddFormatter.LayoutAvailability.NO_EXTENDED_SPACE ->
        R.string.settings_hdd_layout_unavailable_no_extended_space
      XboxHddFormatter.LayoutAvailability.NEEDS_STANDARD_G_BOUNDARY ->
        R.string.settings_hdd_layout_unavailable_needs_standard_g_boundary
      XboxHddFormatter.LayoutAvailability.NOT_ENOUGH_SPACE ->
        R.string.settings_hdd_layout_unavailable_not_enough_space
    }
  }

  private fun clearSystemCache(): CacheClearResult {
    var result = CacheClearResult(0, false)

    val cacheRoots = buildList {
      add(cacheDir)
      add(codeCacheDir)
      externalCacheDir?.let { add(it) }
    }
    for (root in cacheRoots.distinctBy { it.absolutePath }) {
      result = mergeCacheClearResults(result, clearDirectoryChildren(root))
    }

    val persistentRoots = buildList {
      add(filesDir)
      getExternalFilesDir(null)?.let { add(it) }
    }
    for (root in persistentRoots.distinctBy { it.absolutePath }) {
      result = mergeCacheClearResults(result, clearPersistentCacheEntries(root))
    }

    return result
  }

  private fun clearDirectoryChildren(dir: File?): CacheClearResult {
    if (dir == null || !dir.exists()) {
      return CacheClearResult(0, false)
    }

    val children = dir.listFiles() ?: return CacheClearResult(0, false)
    var deletedEntries = 0
    var hadFailures = false
    for (child in children) {
      val deleted = runCatching { child.deleteRecursively() }.getOrDefault(false)
      if (deleted) {
        deletedEntries++
      } else {
        hadFailures = true
      }
    }
    return CacheClearResult(deletedEntries, hadFailures)
  }

  private fun clearPersistentCacheEntries(root: File): CacheClearResult {
    if (!root.exists() || !root.isDirectory) {
      return CacheClearResult(0, false)
    }

    val children = root.listFiles() ?: return CacheClearResult(0, false)
    var deletedEntries = 0
    var hadFailures = false

    for (child in children) {
      if (isPersistentCacheEntry(child.name)) {
        val deleted = runCatching { child.deleteRecursively() }.getOrDefault(false)
        if (deleted) {
          deletedEntries++
        } else {
          hadFailures = true
        }
        continue
      }

      if (child.isDirectory) {
        val nested = clearPersistentCacheEntries(child)
        deletedEntries += nested.deletedEntries
        hadFailures = hadFailures || nested.hadFailures
      }
    }

    return CacheClearResult(deletedEntries, hadFailures)
  }

  private fun isPersistentCacheEntry(name: String): Boolean {
    return name == "shaders" ||
      name == "tb_cache.bin" ||
      name == "shader_cache_list" ||
      name.startsWith("scache-") ||
      name.startsWith("vk_pipeline_cache_")
  }

  private fun mergeCacheClearResults(
    first: CacheClearResult,
    second: CacheClearResult,
  ): CacheClearResult {
    return CacheClearResult(
      deletedEntries = first.deletedEntries + second.deletedEntries,
      hadFailures = first.hadFailures || second.hadFailures,
    )
  }

  private fun resolveEepromFile(): File {
    return managedFilesRoots()
      .asSequence()
      .map { root -> File(root, "eeprom.bin") }
      .firstOrNull { it.isFile }
      ?: File(resolveManagedFilesRoot(), "eeprom.bin")
  }

  private fun resolveHddFile(): File? {
    val path = prefs.getString("hddPath", null) ?: return null
    val file = File(path)
    return file.takeIf { it.isFile }
  }

  private fun getFileName(uri: Uri): String? {
    return contentResolver.query(uri, null, null, null, null)?.use { cursor ->
      val col = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
      if (col >= 0 && cursor.moveToFirst()) cursor.getString(col) else null
    }
  }

  override fun finish() {
    super.finish()
    overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
  }

  private fun finishWithTransition() {
    finish()
    overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
  }

}

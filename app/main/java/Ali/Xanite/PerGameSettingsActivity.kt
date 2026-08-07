package Ali.Xanite

import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.AutoCompleteTextView
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputLayout
import java.io.File

class PerGameSettingsActivity : BaseActivity() {
  companion object {
    const val EXTRA_GAME_TITLE = "Ali.Xanite.extra.GAME_TITLE"
    const val EXTRA_GAME_RELATIVE_PATH = "Ali.Xanite.extra.GAME_RELATIVE_PATH"
  }

  private data class SettingOption(
    val value: String?,
    val labelRes: Int = 0,
    val label: String? = null,
  )

  private data class SettingField(
    val key: String,
    val inputLayoutId: Int,
    val dropdownId: Int,
    val options: List<SettingOption>,
  )

  private fun optionLabel(option: SettingOption): String =
    option.label ?: getString(option.labelRes)

  private val fieldSelections = linkedMapOf<String, String?>()

  private val fields by lazy {
    listOf(
      SettingField(
        key = "setting_gpu_driver",
        inputLayoutId = R.id.input_per_game_gpu_driver,
        dropdownId = R.id.dropdown_per_game_gpu_driver,
        options = buildList {
          add(SettingOption(null, R.string.per_game_settings_use_global))
          add(SettingOption("system", R.string.settings_gpu_driver_option_system))
          for (driver in GpuDriverHelper.getAvailableDrivers()) {
            val zipName = File(driver.path ?: continue).name
            add(SettingOption(zipName, label = driver.name ?: zipName))
          }
        },
      ),
      SettingField(
        key = "setting_renderer",
        inputLayoutId = R.id.input_per_game_renderer,
        dropdownId = R.id.dropdown_per_game_renderer,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("vulkan", R.string.settings_graphics_api_vulkan),
          SettingOption("opengl", R.string.settings_graphics_api_opengl),
        ),
      ),
      SettingField(
        key = "setting_filtering",
        inputLayoutId = R.id.input_per_game_filtering,
        dropdownId = R.id.dropdown_per_game_filtering,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("linear", R.string.settings_filtering_linear),
          SettingOption("nearest", R.string.settings_filtering_nearest),
        ),
      ),
      SettingField(
        key = "setting_vsync",
        inputLayoutId = R.id.input_per_game_vsync,
        dropdownId = R.id.dropdown_per_game_vsync,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_surface_scale",
        inputLayoutId = R.id.input_per_game_surface_scale,
        dropdownId = R.id.dropdown_per_game_surface_scale,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("0", R.string.settings_resolution_scale_0_5x),
          SettingOption("1", R.string.settings_resolution_scale_1x),
          SettingOption("2", R.string.settings_resolution_scale_2x),
          SettingOption("3", R.string.settings_resolution_scale_3x),
          SettingOption("4", R.string.settings_resolution_scale_4x),
          SettingOption("5", R.string.settings_resolution_scale_5x),
        ),
      ),
      SettingField(
        key = "setting_display_mode",
        inputLayoutId = R.id.input_per_game_display_mode,
        dropdownId = R.id.dropdown_per_game_display_mode,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("0", R.string.settings_display_mode_stretch),
          SettingOption("1", R.string.settings_display_mode_4_3),
          SettingOption("2", R.string.settings_display_mode_16_9),
        ),
      ),
      SettingField(
        key = OrientationPreferences.PREF_GAME_ORIENTATION,
        inputLayoutId = R.id.input_per_game_orientation,
        dropdownId = R.id.dropdown_per_game_orientation,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption(
            OrientationPreferences.GameOrientation.FOLLOW_DEVICE.prefValue,
            R.string.settings_orientation_follow_device,
          ),
          SettingOption(
            OrientationPreferences.GameOrientation.LANDSCAPE.prefValue,
            R.string.settings_orientation_landscape,
          ),
          SettingOption(
            OrientationPreferences.GameOrientation.REVERSE_LANDSCAPE.prefValue,
            R.string.settings_orientation_reverse_landscape,
          ),
        ),
      ),
      SettingField(
        key = "setting_system_memory_mib",
        inputLayoutId = R.id.input_per_game_system_memory,
        dropdownId = R.id.dropdown_per_game_system_memory,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("64", R.string.settings_system_memory_64),
          SettingOption("128", R.string.settings_system_memory_128),
        ),
      ),
      SettingField(
        key = "setting_tcg_thread",
        inputLayoutId = R.id.input_per_game_tcg_thread,
        dropdownId = R.id.dropdown_per_game_tcg_thread,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("multi", R.string.settings_tcg_thread_multi),
          SettingOption("single", R.string.settings_tcg_thread_single),
        ),
      ),
      SettingField(
        key = "setting_hard_fpu",
        inputLayoutId = R.id.input_per_game_hard_fpu,
        dropdownId = R.id.dropdown_per_game_hard_fpu,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_cache_shaders",
        inputLayoutId = R.id.input_per_game_cache_shaders,
        dropdownId = R.id.dropdown_per_game_cache_shaders,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_skip_boot_anim",
        inputLayoutId = R.id.input_per_game_skip_boot_anim,
        dropdownId = R.id.dropdown_per_game_skip_boot_anim,
        options = booleanOptions(),
      ),
      SettingField(
        key = "draw_reorder",
        inputLayoutId = R.id.input_per_game_draw_reorder,
        dropdownId = R.id.dropdown_per_game_draw_reorder,
        options = booleanOptions(),
      ),
      SettingField(
        key = "draw_merge",
        inputLayoutId = R.id.input_per_game_draw_merge,
        dropdownId = R.id.dropdown_per_game_draw_merge,
        options = booleanOptions(),
      ),
      SettingField(
        key = "async_compile",
        inputLayoutId = R.id.input_per_game_async_compile,
        dropdownId = R.id.dropdown_per_game_async_compile,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_use_dsp",
        inputLayoutId = R.id.input_per_game_use_dsp,
        dropdownId = R.id.dropdown_per_game_use_dsp,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_use_dsp_jit",
        inputLayoutId = R.id.input_per_game_use_dsp_jit,
        dropdownId = R.id.dropdown_per_game_use_dsp_jit,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_hrtf",
        inputLayoutId = R.id.input_per_game_hrtf,
        dropdownId = R.id.dropdown_per_game_hrtf,
        options = booleanOptions(),
      ),
      SettingField(
        key = "setting_audio_driver",
        inputLayoutId = R.id.input_per_game_audio_driver,
        dropdownId = R.id.dropdown_per_game_audio_driver,
        options = listOf(
          SettingOption(null, R.string.per_game_settings_use_global),
          SettingOption("openslES", R.string.settings_audio_driver_opensles),
          SettingOption("aaudio", R.string.settings_audio_driver_aaudio),
          SettingOption("dummy", R.string.settings_audio_driver_disabled),
        ),
      ),
      SettingField(
        key = "setting_network_enable",
        inputLayoutId = R.id.input_per_game_network_enable,
        dropdownId = R.id.dropdown_per_game_network_enable,
        options = booleanOptions(),
      ),
    )
  }

  private lateinit var gameTitle: String
  private lateinit var relativePath: String

  /** Built-in compatibility defaults for this title; empty for most games. */
  private var builtInProfile: Map<String, String> = emptyMap()

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_per_game_settings)
    EdgeToEdgeHelper.enable(this)
    EdgeToEdgeHelper.applySystemBarPadding(findViewById(R.id.per_game_settings_scroll))
    enableFullScreen()

    gameTitle = intent.getStringExtra(EXTRA_GAME_TITLE)?.trim().orEmpty()
    relativePath = intent.getStringExtra(EXTRA_GAME_RELATIVE_PATH)?.trim().orEmpty()

    if (relativePath.isEmpty()) {
      Toast.makeText(this, R.string.per_game_settings_missing_game, Toast.LENGTH_SHORT).show()
      finish()
      return
    }

    findViewById<TextView>(R.id.tv_per_game_settings_game_title).text =
      gameTitle.ifEmpty { relativePath.substringAfterLast('/') }
    findViewById<TextView>(R.id.tv_per_game_settings_game_path).text = relativePath

    GpuDriverHelper.init(this)
    builtInProfile = PerGameSettingsManager.builtInProfileFor(relativePath)
    val savedOverrides = PerGameSettingsManager.loadOverrides(this, relativePath)
    bindFields(savedOverrides)

    findViewById<MaterialButton>(R.id.btn_per_game_settings_clear).setOnClickListener {
      PerGameSettingsManager.clearOverrides(this, relativePath)
      Toast.makeText(this, R.string.per_game_settings_cleared, Toast.LENGTH_SHORT).show()
      finish()
    }

    findViewById<MaterialButton>(R.id.btn_per_game_settings_save).setOnClickListener {
      PerGameSettingsManager.saveOverrides(this, relativePath, fieldSelections)
      Toast.makeText(this, R.string.per_game_settings_saved, Toast.LENGTH_SHORT).show()
      finish()
    }
  }

  private fun bindFields(savedOverrides: Map<String, String>) {
    for (field in fields) {
      val inputLayout = findViewById<TextInputLayout>(field.inputLayoutId)
      val dropdown = findViewById<AutoCompleteTextView>(field.dropdownId)
      val labels = field.options.map { option -> optionLabel(option) }

      dropdown.setAdapter(ArrayAdapter(this, android.R.layout.simple_list_item_1, labels))
      dropdown.setOnItemClickListener { _, _, position, _ ->
        fieldSelections[field.key] = field.options[position].value
      }

      val selectedValue = savedOverrides[field.key]
      fieldSelections[field.key] = selectedValue
      setFieldSelection(dropdown, field, selectedValue)

      // A built-in profile only applies while the user has not chosen their own
      // value (applyRuntimeOverridesToEditor merges user overrides last), so
      // only surface it when it is actually what the game will run with.
      val builtInValue = builtInProfile[field.key]
      inputLayout.helperText = if (builtInValue != null && selectedValue == null) {
        getString(
          R.string.per_game_settings_builtin_value,
          describeValue(field, builtInValue),
        )
      } else {
        getString(
          R.string.per_game_settings_global_value,
          describeGlobalValue(field),
        )
      }
    }
  }

  private fun setFieldSelection(
    dropdown: AutoCompleteTextView,
    field: SettingField,
    value: String?,
  ) {
    val option = field.options.firstOrNull { it.value == value } ?: field.options.first()
    dropdown.setText(optionLabel(option), false)
  }

  private fun describeGlobalValue(field: SettingField): String {
    val globalValue = readGlobalValue(field.key)
    val matchingOption = field.options.firstOrNull { option -> option.value == globalValue }
      ?: field.options.first()
    return optionLabel(matchingOption)
  }

  /**
   * Human label for a built-in profile value. Unlike the global case there is no
   * sensible option to fall back to, so an unrecognised value is shown verbatim
   * rather than mislabelled as the first dropdown entry.
   */
  private fun describeValue(field: SettingField, value: String?): String {
    val matchingOption = field.options.firstOrNull { option -> option.value == value }
    return if (matchingOption != null) optionLabel(matchingOption) else value.orEmpty()
  }

  private fun readGlobalValue(key: String): String {
    val prefs = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
    return when (key) {
      "setting_renderer" -> prefs.getString(key, "vulkan") ?: "vulkan"
      "setting_filtering" -> prefs.getString(key, "linear") ?: "linear"
      "setting_vsync" -> prefs.getBoolean(key, true).toString()
      "setting_surface_scale" -> prefs.getInt(key, 1).toString()
      "setting_display_mode" -> prefs.getInt(key, 0).toString()
      OrientationPreferences.PREF_GAME_ORIENTATION ->
        prefs.getString(
          key,
          OrientationPreferences.GameOrientation.FOLLOW_DEVICE.prefValue,
        ) ?: OrientationPreferences.GameOrientation.FOLLOW_DEVICE.prefValue
      "setting_system_memory_mib" -> prefs.getInt(key, 64).toString()
      "setting_tcg_thread" -> prefs.getString(key, "multi") ?: "multi"
      "setting_use_dsp" -> prefs.getBoolean(key, false).toString()
      "setting_use_dsp_jit" -> prefs.getBoolean(key, true).toString()
      "setting_hrtf" -> prefs.getBoolean(key, false).toString()
      "setting_cache_shaders" -> prefs.getBoolean(key, true).toString()
      "setting_hard_fpu" -> prefs.getBoolean(key, true).toString()
      "setting_skip_boot_anim" -> prefs.getBoolean(key, true).toString()
      "draw_reorder" -> prefs.getBoolean(key, true).toString()
      "draw_merge" -> prefs.getBoolean(key, true).toString()
      "async_compile" -> prefs.getBoolean(key, true).toString()
      "setting_audio_driver" -> prefs.getString(key, "openslES") ?: "openslES"
      "setting_network_enable" -> prefs.getBoolean(key, false).toString()
      "setting_gpu_driver" -> {
        if (!GpuDriverHelper.supportsCustomDriverLoading()) {
          "system"
        } else {
          val installedName = GpuDriverHelper.getInstalledDriverName()
          if (installedName == null) {
            "system"
          } else {
            GpuDriverHelper.getAvailableDrivers()
              .firstOrNull { it.name == installedName }
              ?.path?.let { File(it).name }
              ?: "system"
          }
        }
      }
      else -> ""
    }
  }

  private fun booleanOptions(): List<SettingOption> {
    return listOf(
      SettingOption(null, R.string.per_game_settings_use_global),
      SettingOption("true", R.string.per_game_settings_enabled),
      SettingOption("false", R.string.per_game_settings_disabled),
    )
  }

  // No finish() override: it used to call super.finish() and then
  // ActivityCompat.finishAfterTransition(this), but finishAfterTransition()
  // calls finish() internally, so the override re-entered itself. Saving left
  // the activity in a half-finished state instead of returning to the library.
}

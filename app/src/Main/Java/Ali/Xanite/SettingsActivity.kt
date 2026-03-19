package Ali.Xanite

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.AutoCompleteTextView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import androidx.viewpager2.adapter.FragmentStateAdapter
import androidx.viewpager2.widget.ViewPager2
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.tabs.TabLayout
import com.google.android.material.tabs.TabLayoutMediator
import com.google.android.material.textfield.TextInputLayout
import java.io.File
import java.io.IOException

class SettingsActivity : AppCompatActivity() {

    private val prefs by lazy { getSharedPreferences("Xanite_prefs", Context.MODE_PRIVATE) }

    private lateinit var viewPager: ViewPager2
    private lateinit var tabLayout: TabLayout
    private lateinit var toolbar: MaterialToolbar

    var pendingVulkanUri: String? = null
    var pendingVulkanName: String? = null
    var clearVulkan = false

    private var eepromEditable = false
    private var eepromMissing = false
    private var eepromError = false

    private var selectedEepromLanguage = 1 
    private var selectedEepromVideoStandard = 0 
    private var selectedEepromRefreshRate = 0 
    private var selectedEepromAspectRatio = 0 
    private var selectedEepromResolution = 0 

    private val eepromLanguageOptions = listOf(
        EepromLanguageOption(1, R.string.settings_eeprom_language_english),
        EepromLanguageOption(2, R.string.settings_eeprom_language_japanese),
        EepromLanguageOption(3, R.string.settings_eeprom_language_german),
        EepromLanguageOption(4, R.string.settings_eeprom_language_french),
        EepromLanguageOption(5, R.string.settings_eeprom_language_spanish),
        EepromLanguageOption(6, R.string.settings_eeprom_language_italian),
        EepromLanguageOption(7, R.string.settings_eeprom_language_korean),
        EepromLanguageOption(8, R.string.settings_eeprom_language_chinese),
        EepromLanguageOption(9, R.string.settings_eeprom_language_portuguese),
    )

    private val eepromVideoOptions = listOf(
        EepromVideoOption(0, R.string.settings_eeprom_video_standard_ntsc_m),
        EepromVideoOption(1, R.string.settings_eeprom_video_standard_ntsc_j),
        EepromVideoOption(2, R.string.settings_eeprom_video_standard_pal_i),
        EepromVideoOption(3, R.string.settings_eeprom_video_standard_pal_m),
    )

    private val refreshRateOptions = listOf(
        Triple(0, R.string.eeprom_refresh_auto, 0),
        Triple(1, R.string.eeprom_refresh_50hz, 50),
        Triple(2, R.string.eeprom_refresh_60hz, 60)
    )

    private val aspectRatioOptions = listOf(
        Triple(0, R.string.eeprom_aspect_auto, 0),
        Triple(1, R.string.eeprom_aspect_4_3, 43),
        Triple(2, R.string.eeprom_aspect_16_9, 169)
    )

    private val resolutionOptions = listOf(
        Triple(0, R.string.eeprom_resolution_auto, 0),
        Triple(1, R.string.eeprom_resolution_480i, 480),
        Triple(2, R.string.eeprom_resolution_480p, 480),
        Triple(3, R.string.eeprom_resolution_720p, 720),
        Triple(4, R.string.eeprom_resolution_1080i, 1080)
    )

    val pickDriver = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@registerForActivityResult
        pendingVulkanUri = uri.toString()
        pendingVulkanName = getFileName(uri) ?: uri.lastPathSegment ?: "custom_driver.so"
        clearVulkan = false

        val systemFragment = supportFragmentManager.findFragmentByTag("f3") as? SystemSettingsFragment
        systemFragment?.updateVulkanDriverName(pendingVulkanName ?: "custom_driver.so")

        saveAllSettings()

        showToast("Driver selected: ${pendingVulkanName}", Toast.LENGTH_SHORT)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings_tabs)

        initializeViews()
        setupToolbar()
        setupViewPager()
        loadSavedPreferences()
    }

    override fun onBackPressed() {
        saveAllSettings()
        super.onBackPressed()
    }

    override fun onPause() {
        super.onPause()

        saveAllSettings()
    }

    override fun onDestroy() {
        super.onDestroy()

        saveAllSettings()
    }

    private fun initializeViews() {
        viewPager = findViewById(R.id.view_pager)
        tabLayout = findViewById(R.id.tab_layout)
        toolbar = findViewById(R.id.toolbar)
    }

    private fun setupToolbar() {
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.setDisplayShowHomeEnabled(true)
        toolbar.setNavigationOnClickListener {
            onBackPressed() 
        }
    }

    private fun setupViewPager() {
        val adapter = SettingsPagerAdapter(this)
        viewPager.adapter = adapter

        TabLayoutMediator(tabLayout, viewPager) { tab, position ->
            when (position) {
                0 -> tab.text = "Display"
                1 -> tab.text = "Audio"
                2 -> tab.text = "Performance"
                3 -> tab.text = "System"
            }
        }.attach()
    }

    private fun loadSavedPreferences() {

        selectedEepromLanguage = prefs.getInt("eeprom_language", 1)
        selectedEepromVideoStandard = prefs.getInt("eeprom_video_standard", 0)
        selectedEepromRefreshRate = prefs.getInt("eeprom_refresh_rate", 0)
        selectedEepromAspectRatio = prefs.getInt("eeprom_aspect_ratio", 0)
        selectedEepromResolution = prefs.getInt("eeprom_resolution", 0)
    }


    fun saveAllSettings() {
        val edit = prefs.edit()


        val displayFragment = supportFragmentManager.findFragmentByTag("f0") as? DisplaySettingsFragment
        val audioFragment = supportFragmentManager.findFragmentByTag("f1") as? AudioSettingsFragment
        val performanceFragment = supportFragmentManager.findFragmentByTag("f2") as? PerformanceSettingsFragment
        val systemFragment = supportFragmentManager.findFragmentByTag("f3") as? SystemSettingsFragment

        displayFragment?.let { fragment ->
            edit.putInt("setting_display_mode", fragment.getSelectedDisplayMode())
            edit.putInt("setting_surface_scale", fragment.getSelectedScale())
            edit.putInt("setting_frame_rate_limit", fragment.getSelectedFrameRate())
            edit.putString("setting_filtering", fragment.getSelectedFiltering())
            edit.putString("setting_renderer", fragment.getSelectedRenderer())
            edit.putBoolean("setting_vsync", fragment.isVsyncEnabled())
        }

        audioFragment?.let { fragment ->
            edit.putBoolean("setting_use_dsp", fragment.isDspEnabled())
            edit.putBoolean("setting_hrtf", fragment.isHrtfEnabled())
            edit.putString("setting_audio_driver", fragment.getSelectedAudioDriver())
        }

        performanceFragment?.let { fragment ->
            edit.putInt("setting_system_memory_mib", fragment.getSelectedMemory())
            edit.putString("setting_tcg_thread", fragment.getSelectedThread())
            edit.putBoolean("setting_cache_shaders", fragment.isCacheShadersEnabled())
            edit.putBoolean("setting_hard_fpu", fragment.isHardFpuEnabled())
            edit.putBoolean("setting_skip_boot_anim", fragment.isSkipBootAnimEnabled())
        }

        systemFragment?.let { fragment ->
            edit.putInt("eeprom_language", fragment.getSelectedLanguage())
            edit.putInt("eeprom_video_standard", fragment.getSelectedVideoStandard())
            edit.putInt("eeprom_refresh_rate", fragment.getSelectedRefreshRate())
            edit.putInt("eeprom_aspect_ratio", fragment.getSelectedAspectRatio())
            edit.putInt("eeprom_resolution", fragment.getSelectedResolution())
        }

        when {
            clearVulkan -> edit
                .remove("setting_vulkan_driver_uri")
                .remove("setting_vulkan_driver_name")
            pendingVulkanUri != null -> edit
                .putString("setting_vulkan_driver_uri", pendingVulkanUri)
                .putString("setting_vulkan_driver_name", pendingVulkanName)
        }

        edit.apply()
    }

    private fun getFileName(uri: Uri): String? {
        return contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val col = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (col >= 0 && cursor.moveToFirst()) cursor.getString(col) else null
        }
    }

    private fun showToast(message: String, duration: Int) {
        Toast.makeText(this, message, duration).show()
    }

    private fun showToast(messageResId: Int, duration: Int) {
        Toast.makeText(this, messageResId, duration).show()
    }

    inner class SettingsPagerAdapter(activity: AppCompatActivity) : FragmentStateAdapter(activity) {
        override fun getItemCount(): Int = 4
        override fun createFragment(position: Int): Fragment {
            return when (position) {
                0 -> DisplaySettingsFragment()
                1 -> AudioSettingsFragment()
                2 -> PerformanceSettingsFragment()
                3 -> SystemSettingsFragment()
                else -> DisplaySettingsFragment()
            }
        }
    }

    class DisplaySettingsFragment : Fragment() {

        private lateinit var toggleGraphicsApi: MaterialButtonToggleGroup
        private lateinit var toggleFiltering: MaterialButtonToggleGroup
        private lateinit var toggleScale: MaterialButtonToggleGroup
        private lateinit var toggleDisplayMode: MaterialButtonToggleGroup
        private lateinit var toggleFrameRate: MaterialButtonToggleGroup
        private lateinit var switchVsync: MaterialSwitch
        private lateinit var prefs: SharedPreferences

        override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
            return inflater.inflate(R.layout.fragment_settings_display, container, false)
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            prefs = requireActivity().getSharedPreferences("Xanite_prefs", Context.MODE_PRIVATE)

            initializeViews(view)
            loadSettings()

            setupAutoSaveListeners()
        }

        private fun initializeViews(view: View) {
            toggleGraphicsApi = view.findViewById(R.id.toggle_graphics_api)
            toggleFiltering = view.findViewById(R.id.toggle_filtering)
            toggleScale = view.findViewById(R.id.toggle_resolution_scale)
            toggleDisplayMode = view.findViewById(R.id.toggle_display_mode)
            toggleFrameRate = view.findViewById(R.id.toggle_frame_rate)
            switchVsync = view.findViewById(R.id.switch_vsync)
        }

        private fun setupAutoSaveListeners() {
            val autoSave = {
                (requireActivity() as SettingsActivity).saveAllSettings()
            }

            toggleGraphicsApi.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            toggleFiltering.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            toggleScale.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            toggleDisplayMode.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            toggleFrameRate.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            switchVsync.setOnCheckedChangeListener { _, _ -> autoSave() }
        }

        private fun loadSettings() {
            val renderer = prefs.getString("setting_renderer", "opengl")
            if (renderer == "opengl") {
                toggleGraphicsApi.check(R.id.btn_renderer_opengl)
            } else {
                toggleGraphicsApi.check(R.id.btn_renderer_vulkan)
            }

            val filtering = prefs.getString("setting_filtering", "linear")
            if (filtering == "nearest") {
                toggleFiltering.check(R.id.btn_filtering_nearest)
            } else {
                toggleFiltering.check(R.id.btn_filtering_linear)
            }

            val scale = prefs.getInt("setting_surface_scale", 1)
            when (scale) {
                2 -> toggleScale.check(R.id.btn_scale_2x)
                3 -> toggleScale.check(R.id.btn_scale_3x)
                else -> toggleScale.check(R.id.btn_scale_1x)
            }

            val displayMode = prefs.getInt("setting_display_mode", 0)
            when (displayMode) {
                1 -> toggleDisplayMode.check(R.id.btn_display_4_3)
                2 -> toggleDisplayMode.check(R.id.btn_display_16_9)
                else -> toggleDisplayMode.check(R.id.btn_display_stretch)
            }

            val frameRate = prefs.getInt("setting_frame_rate_limit", 60)
            if (frameRate == 30) {
                toggleFrameRate.check(R.id.btn_fps_30)
            } else {
                toggleFrameRate.check(R.id.btn_fps_60)
            }

            switchVsync.isChecked = prefs.getBoolean("setting_vsync", true)
        }

        fun getSelectedDisplayMode(): Int = when (toggleDisplayMode.checkedButtonId) {
            R.id.btn_display_4_3 -> 1
            R.id.btn_display_16_9 -> 2
            else -> 0
        }

        fun getSelectedScale(): Int = when (toggleScale.checkedButtonId) {
            R.id.btn_scale_2x -> 2
            R.id.btn_scale_3x -> 3
            else -> 1
        }

        fun getSelectedFrameRate(): Int = when (toggleFrameRate.checkedButtonId) {
            R.id.btn_fps_30 -> 30
            else -> 60
        }

        fun getSelectedFiltering(): String = when (toggleFiltering.checkedButtonId) {
            R.id.btn_filtering_nearest -> "nearest"
            else -> "linear"
        }

        fun getSelectedRenderer(): String = when (toggleGraphicsApi.checkedButtonId) {
            R.id.btn_renderer_opengl -> "opengl"
            else -> "vulkan"
        }

        fun isVsyncEnabled(): Boolean = switchVsync.isChecked
    }

    class AudioSettingsFragment : Fragment() {

        private lateinit var toggleAudioDriver: MaterialButtonToggleGroup
        private lateinit var switchDsp: MaterialSwitch
        private lateinit var switchHrtf: MaterialSwitch
        private lateinit var prefs: SharedPreferences

        override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
            return inflater.inflate(R.layout.fragment_settings_audio, container, false)
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            prefs = requireActivity().getSharedPreferences("Xanite_prefs", Context.MODE_PRIVATE)

            initializeViews(view)
            loadSettings()

            setupAutoSaveListeners()
        }

        private fun initializeViews(view: View) {
            toggleAudioDriver = view.findViewById(R.id.toggle_audio_driver)
            switchDsp = view.findViewById(R.id.switch_use_dsp)
            switchHrtf = view.findViewById(R.id.switch_hrtf)
        }

        private fun setupAutoSaveListeners() {
            val autoSave = {
                (requireActivity() as SettingsActivity).saveAllSettings()
            }

            toggleAudioDriver.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            switchDsp.setOnCheckedChangeListener { _, _ -> autoSave() }
            switchHrtf.setOnCheckedChangeListener { _, _ -> autoSave() }
        }

        private fun loadSettings() {
            val audioDriver = prefs.getString("setting_audio_driver", "openslES")
            when (audioDriver) {
                "aaudio" -> toggleAudioDriver.check(R.id.btn_audio_aaudio)
                "dummy" -> toggleAudioDriver.check(R.id.btn_audio_disabled)
                else -> toggleAudioDriver.check(R.id.btn_audio_opensles)
            }

            switchDsp.isChecked = prefs.getBoolean("setting_use_dsp", false)
            switchHrtf.isChecked = prefs.getBoolean("setting_hrtf", true)
        }

        fun getSelectedAudioDriver(): String = when (toggleAudioDriver.checkedButtonId) {
            R.id.btn_audio_aaudio -> "aaudio"
            R.id.btn_audio_disabled -> "dummy"
            else -> "openslES"
        }

        fun isDspEnabled(): Boolean = switchDsp.isChecked
        fun isHrtfEnabled(): Boolean = switchHrtf.isChecked
    }

    class PerformanceSettingsFragment : Fragment() {

        private lateinit var toggleThread: MaterialButtonToggleGroup
        private lateinit var toggleSystemMemory: MaterialButtonToggleGroup
        private lateinit var switchShaders: MaterialSwitch
        private lateinit var switchFpu: MaterialSwitch
        private lateinit var switchSkipBootAnim: MaterialSwitch
        private lateinit var prefs: SharedPreferences

        override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
            return inflater.inflate(R.layout.fragment_settings_performance, container, false)
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            prefs = requireActivity().getSharedPreferences("Xanite_prefs", Context.MODE_PRIVATE)

            initializeViews(view)
            loadSettings()

            setupAutoSaveListeners()
        }

        private fun initializeViews(view: View) {
            toggleThread = view.findViewById(R.id.toggle_tcg_thread)
            toggleSystemMemory = view.findViewById(R.id.toggle_system_memory)
            switchShaders = view.findViewById(R.id.switch_cache_shaders)
            switchFpu = view.findViewById(R.id.switch_hard_fpu)
            switchSkipBootAnim = view.findViewById(R.id.switch_skip_boot_anim)
        }

        private fun setupAutoSaveListeners() {
            val autoSave = {
                (requireActivity() as SettingsActivity).saveAllSettings()
            }

            toggleThread.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            toggleSystemMemory.addOnButtonCheckedListener { _, _, _ -> autoSave() }
            switchShaders.setOnCheckedChangeListener { _, _ -> autoSave() }
            switchFpu.setOnCheckedChangeListener { _, _ -> autoSave() }
            switchSkipBootAnim.setOnCheckedChangeListener { _, _ -> autoSave() }
        }

        private fun loadSettings() {
            val tcgThread = prefs.getString("setting_tcg_thread", "multi")
            if (tcgThread == "single") {
                toggleThread.check(R.id.btn_thread_single)
            } else {
                toggleThread.check(R.id.btn_thread_multi)
            }

            val memory = prefs.getInt("setting_system_memory_mib", 64)
            if (memory == 128) {
                toggleSystemMemory.check(R.id.btn_memory_128)
            } else {
                toggleSystemMemory.check(R.id.btn_memory_64)
            }

            switchShaders.isChecked = prefs.getBoolean("setting_cache_shaders", true)
            switchFpu.isChecked = prefs.getBoolean("setting_hard_fpu", true)
            switchSkipBootAnim.isChecked = prefs.getBoolean("setting_skip_boot_anim", false)
        }

        fun getSelectedThread(): String = when (toggleThread.checkedButtonId) {
            R.id.btn_thread_single -> "single"
            else -> "multi"
        }

        fun getSelectedMemory(): Int = when (toggleSystemMemory.checkedButtonId) {
            R.id.btn_memory_128 -> 128
            else -> 64
        }

        fun isCacheShadersEnabled(): Boolean = switchShaders.isChecked
        fun isHardFpuEnabled(): Boolean = switchFpu.isChecked
        fun isSkipBootAnimEnabled(): Boolean = switchSkipBootAnim.isChecked
    }

    class SystemSettingsFragment : Fragment() {

        private lateinit var tvEepromStatus: TextView
        private lateinit var tvVulkanDriverName: TextView
        private lateinit var inputEepromLanguage: TextInputLayout
        private lateinit var inputEepromVideoStandard: TextInputLayout
        private lateinit var inputEepromRefreshRate: TextInputLayout
        private lateinit var inputEepromAspectRatio: TextInputLayout
        private lateinit var inputEepromResolution: TextInputLayout

        private lateinit var dropdownEepromLanguage: AutoCompleteTextView
        private lateinit var dropdownEepromVideoStandard: AutoCompleteTextView
        private lateinit var dropdownEepromRefreshRate: AutoCompleteTextView
        private lateinit var dropdownEepromAspectRatio: AutoCompleteTextView
        private lateinit var dropdownEepromResolution: AutoCompleteTextView

        private lateinit var btnVulkanBrowse: MaterialButton
        private lateinit var btnVulkanClear: MaterialButton
        private lateinit var btnRedoSetup: MaterialButton
        private lateinit var btnSave: MaterialButton

        private var prefs: SharedPreferences? = null

        private var selectedLanguage = 1 
        private var selectedVideoStandard = 0 
        private var selectedRefreshRate = 0 
        private var selectedAspectRatio = 0 
        private var selectedResolution = 0 

        override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
            return inflater.inflate(R.layout.fragment_settings_system, container, false)
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            prefs = requireActivity().getSharedPreferences("Xanite_prefs", Context.MODE_PRIVATE)

            initializeViews(view)
            setupDropdownAdapters()
            setupDropdownListeners()
            loadSettings()
            setupClickListeners()
        }

        private fun initializeViews(view: View) {
            tvEepromStatus = view.findViewById(R.id.tv_eeprom_status)
            tvVulkanDriverName = view.findViewById(R.id.tv_vulkan_driver_name)

            inputEepromLanguage = view.findViewById(R.id.input_eeprom_language)
            inputEepromVideoStandard = view.findViewById(R.id.input_eeprom_video_standard)
            inputEepromRefreshRate = view.findViewById(R.id.input_eeprom_refresh_rate)
            inputEepromAspectRatio = view.findViewById(R.id.input_eeprom_aspect_ratio)
            inputEepromResolution = view.findViewById(R.id.input_eeprom_resolution)

            dropdownEepromLanguage = view.findViewById(R.id.dropdown_eeprom_language)
            dropdownEepromVideoStandard = view.findViewById(R.id.dropdown_eeprom_video_standard)
            dropdownEepromRefreshRate = view.findViewById(R.id.dropdown_eeprom_refresh_rate)
            dropdownEepromAspectRatio = view.findViewById(R.id.dropdown_eeprom_aspect_ratio)
            dropdownEepromResolution = view.findViewById(R.id.dropdown_eeprom_resolution)

            btnVulkanBrowse = view.findViewById(R.id.btn_vulkan_browse)
            btnVulkanClear = view.findViewById(R.id.btn_vulkan_clear)
            btnRedoSetup = view.findViewById(R.id.btn_redo_setup_wizard)
            btnSave = view.findViewById(R.id.btn_settings_save)
        }

        private fun setupDropdownAdapters() {

            val languageLabels = (requireActivity() as SettingsActivity).eepromLanguageOptions.map { getString(it.labelRes) }
            val languageAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_dropdown_item_1line, languageLabels)
            dropdownEepromLanguage.setAdapter(languageAdapter)


            val videoLabels = (requireActivity() as SettingsActivity).eepromVideoOptions.map { getString(it.labelRes) }
            val videoAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_dropdown_item_1line, videoLabels)
            dropdownEepromVideoStandard.setAdapter(videoAdapter)


            val refreshLabels = (requireActivity() as SettingsActivity).refreshRateOptions.map { getString(it.second) }
            val refreshAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_dropdown_item_1line, refreshLabels)
            dropdownEepromRefreshRate.setAdapter(refreshAdapter)


            val aspectLabels = (requireActivity() as SettingsActivity).aspectRatioOptions.map { getString(it.second) }
            val aspectAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_dropdown_item_1line, aspectLabels)
            dropdownEepromAspectRatio.setAdapter(aspectAdapter)


            val resolutionLabels = (requireActivity() as SettingsActivity).resolutionOptions.map { getString(it.second) }
            val resolutionAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_dropdown_item_1line, resolutionLabels)
            dropdownEepromResolution.setAdapter(resolutionAdapter)
        }

        private fun setupDropdownListeners() {
            dropdownEepromLanguage.setOnItemClickListener { _, _, position, _ ->
                selectedLanguage = (requireActivity() as SettingsActivity).eepromLanguageOptions[position].value
                (requireActivity() as SettingsActivity).saveAllSettings() 
            }

            dropdownEepromVideoStandard.setOnItemClickListener { _, _, position, _ ->
                selectedVideoStandard = (requireActivity() as SettingsActivity).eepromVideoOptions[position].value
                (requireActivity() as SettingsActivity).saveAllSettings() 
            }

            dropdownEepromRefreshRate.setOnItemClickListener { _, _, position, _ ->
                selectedRefreshRate = (requireActivity() as SettingsActivity).refreshRateOptions[position].first
                (requireActivity() as SettingsActivity).saveAllSettings() 
            }

            dropdownEepromAspectRatio.setOnItemClickListener { _, _, position, _ ->
                selectedAspectRatio = (requireActivity() as SettingsActivity).aspectRatioOptions[position].first
                (requireActivity() as SettingsActivity).saveAllSettings() 
            }

            dropdownEepromResolution.setOnItemClickListener { _, _, position, _ ->
                selectedResolution = (requireActivity() as SettingsActivity).resolutionOptions[position].first
                (requireActivity() as SettingsActivity).saveAllSettings() 
            }
        }

        private fun loadSettings() {

            tvVulkanDriverName.text = prefs?.getString("setting_vulkan_driver_name", null) ?: getString(R.string.settings_vulkan_driver_none)

            selectedLanguage = prefs?.getInt("eeprom_language", 1) ?: 1
            selectedVideoStandard = prefs?.getInt("eeprom_video_standard", 0) ?: 0
            selectedRefreshRate = prefs?.getInt("eeprom_refresh_rate", 0) ?: 0
            selectedAspectRatio = prefs?.getInt("eeprom_aspect_ratio", 0) ?: 0
            selectedResolution = prefs?.getInt("eeprom_resolution", 0) ?: 0

            setLanguageSelection(selectedLanguage)
            setVideoSelection(selectedVideoStandard)
            setRefreshRateSelection(selectedRefreshRate)
            setAspectRatioSelection(selectedAspectRatio)
            setResolutionSelection(selectedResolution)

            loadEepromStatus()
        }

        private fun setLanguageSelection(languageValue: Int) {
            val options = (requireActivity() as SettingsActivity).eepromLanguageOptions
            val option = options.firstOrNull { it.value == languageValue } ?: options.first()
            dropdownEepromLanguage.setText(getString(option.labelRes), false)
        }

        private fun setVideoSelection(videoValue: Int) {
            val options = (requireActivity() as SettingsActivity).eepromVideoOptions
            val option = options.firstOrNull { it.value == videoValue } ?: options.first()
            dropdownEepromVideoStandard.setText(getString(option.labelRes), false)
        }

        private fun setRefreshRateSelection(rateValue: Int) {
            val options = (requireActivity() as SettingsActivity).refreshRateOptions
            val option = options.firstOrNull { it.first == rateValue } ?: options.first()
            dropdownEepromRefreshRate.setText(getString(option.second), false)
        }

        private fun setAspectRatioSelection(ratioValue: Int) {
            val options = (requireActivity() as SettingsActivity).aspectRatioOptions
            val option = options.firstOrNull { it.first == ratioValue } ?: options.first()
            dropdownEepromAspectRatio.setText(getString(option.second), false)
        }

        private fun setResolutionSelection(resValue: Int) {
            val options = (requireActivity() as SettingsActivity).resolutionOptions
            val option = options.firstOrNull { it.first == resValue } ?: options.first()
            dropdownEepromResolution.setText(getString(option.second), false)
        }

        fun updateVulkanDriverName(name: String) {
            tvVulkanDriverName.text = name
        }

        private fun loadEepromStatus() {
            val eepromFile = resolveEepromFile()

            tvEepromStatus.text = if (!eepromFile.exists()) {
                "EEPROM file not found: ${eepromFile.absolutePath}"
            } else {
                "EEPROM ready: ${eepromFile.absolutePath}"
            }
        }

        private fun resolveEepromFile(): File {
            val base = requireActivity().getExternalFilesDir(null) ?: requireActivity().filesDir
            return File(File(base, "Xanite"), "eeprom.bin")
        }

        private fun setupClickListeners() {
            btnVulkanBrowse.setOnClickListener {
                (requireActivity() as SettingsActivity).pickDriver.launch(arrayOf("*/*"))
            }

            btnVulkanClear.setOnClickListener {
                (requireActivity() as SettingsActivity).apply {
                    pendingVulkanUri = null
                    pendingVulkanName = null
                    clearVulkan = true
                    saveAllSettings() 
                }
                tvVulkanDriverName.text = getString(R.string.settings_vulkan_driver_none)
                Toast.makeText(requireContext(), "Vulkan driver cleared", Toast.LENGTH_SHORT).show()
            }

            btnRedoSetup.setOnClickListener {
                requireActivity().getSharedPreferences("Xanite_prefs", Context.MODE_PRIVATE)
                    .edit().putBoolean("setup_complete", false).apply()
                startActivity(Intent(requireContext(), SetupWizardActivity::class.java))
                requireActivity().finish()
            }

            btnSave.setOnClickListener {
                (requireActivity() as SettingsActivity).saveAllSettings()
                Toast.makeText(requireContext(), R.string.settings_saved, Toast.LENGTH_SHORT).show()
                requireActivity().finish()
            }
        }

        fun getSelectedLanguage(): Int = selectedLanguage
        fun getSelectedVideoStandard(): Int = selectedVideoStandard
        fun getSelectedRefreshRate(): Int = selectedRefreshRate
        fun getSelectedAspectRatio(): Int = selectedAspectRatio
        fun getSelectedResolution(): Int = selectedResolution
    }

    private data class EepromLanguageOption(
        val value: Int,
        val labelRes: Int,
    )

    private data class EepromVideoOption(
        val value: Int,
        val labelRes: Int,
    )
}

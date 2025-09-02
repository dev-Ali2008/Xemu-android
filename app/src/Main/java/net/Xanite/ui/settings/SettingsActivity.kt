package com.xanite.settings

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import com.xanite.R
import android.content.Intent

class SettingsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.settings_activity)

        supportFragmentManager
            .beginTransaction()
            .replace(R.id.settings_container, SettingsFragment())
            .commit()

        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "Advanced Settings"
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }

    class SettingsFragment : PreferenceFragmentCompat() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.root_preferences, rootKey)


            findPreference<Preference>("advanced_settings")?.let {
                it.icon = context?.getDrawable(R.drawable.ic_settings_advanced)
                it.title = "Advanced Settings"
            }

           val editOverlayPref = findPreference<Preference>("edit_overlay")

editOverlayPref?.apply {
    icon = context?.getDrawable(R.drawable.ic_edit)
    title = "Edit Overlay"

    setOnPreferenceClickListener {
        startActivity(Intent(context, EditOverlayActivity::class.java))
        true
    }
    }

            findPreference<Preference>("control")?.let {
                it.icon = context?.getDrawable(R.drawable.ic_control)
                it.title = "Control"
            }

           findPreference<Preference>("gpu_driver")?.let {
    it.icon = ContextCompat.getDrawable(requireContext(), R.drawable.ic_gpu)
    it.title = "Custom GPU Driver"
    it.setOnPreferenceClickListener {
        startActivity(Intent(context, GPUSettingsActivity::class.java))
        true
    }
}


              val deviceInfoPref = findPreference<Preference>("device_info")

deviceInfoPref?.apply {
    icon = context?.getDrawable(R.drawable.ic_info)
    title = "Device Info"

    setOnPreferenceClickListener {
        startActivity(Intent(context, DeviceInfoActivity::class.java))
        true
    }
}


            val rendererSettingsPref = findPreference<Preference>("renderer_settings")

            rendererSettingsPref?.apply {
                icon = ContextCompat.getDrawable(requireContext(), R.drawable.ic_gpu)
                title = "Renderer Settings"

                setOnPreferenceClickListener {
                    startActivity(Intent(context, RendererSettingsActivity::class.java))
                    true
                }
            }
}
}
}

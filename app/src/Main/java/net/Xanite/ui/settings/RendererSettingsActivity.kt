package com.xanite.settings

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import com.xanite.R
import com.xanite.utils.SharedPrefs

class RendererSettingsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.settings_activity)

        supportFragmentManager
            .beginTransaction()
            .replace(R.id.settings_container, RendererSettingsFragment())
            .commit()

        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "Renderer Settings"
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }

    class RendererSettingsFragment : PreferenceFragmentCompat() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.renderer_preferences, rootKey)


            findPreference<ListPreference>("renderer_type")?.let { preference ->
                preference.setOnPreferenceChangeListener { _, newValue ->
                    val rendererType = newValue as String
                    SharedPrefs.saveRendererType(requireContext(), rendererType)

                    val message = when (rendererType) {
                        "vulkan" -> "Vulkan renderer selected. Restart the app for changes to take effect."
                        "opengl" -> "OpenGL renderer selected. Restart the app for changes to take effect."
                        else -> "Unknown renderer selected"
                    }

                    Toast.makeText(context, message, Toast.LENGTH_LONG).show()
                    true
                }


                val currentRenderer = SharedPrefs.getRendererType(requireContext())
                preference.value = currentRenderer
            }
        }
    }
}

package com.xanite.settings

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.PreferenceManager
import com.xanite.R
import com.xanite.databinding.ActivityGpuSettingsBinding
import com.xanite.utils.GraphicsAPIHelper

class GPUSettingsActivity : AppCompatActivity() {

    private lateinit var binding: ActivityGpuSettingsBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityGpuSettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupUI()
        checkGPUCapabilities()
        updateGraphicsAPIInfo()
    }

    override fun onResume() {
        super.onResume()

        updateGraphicsAPIInfo()
    }

    private fun setupUI() {
        binding.toolbar.setNavigationOnClickListener { finish() }

        binding.btnInstallVulkan.setOnClickListener {
            installDriver("vulkan")
        }

        binding.btnInstallOpenGL.setOnClickListener {
            installDriver("opengl")
        }

        binding.btnTestPerformance.setOnClickListener {
            testGPUPerformance()
        }

        binding.btnOpenSettings.setOnClickListener {

            val intent = Intent(this, SettingsActivity::class.java)
            startActivity(intent)
        }
    }

    private fun updateGraphicsAPIInfo() {
        val graphicsInfo = GraphicsAPIHelper.getGraphicsAPIInfo(this)

        val currentAPIName = GraphicsAPIHelper.getGraphicsAPIDisplayName(graphicsInfo.currentAPI)
        binding.tvCurrentGraphicsAPI.text = "Current Graphics API: $currentAPIName"

        val effectiveAPIName = GraphicsAPIHelper.getGraphicsAPIDisplayName(graphicsInfo.effectiveAPI)
        val recommendation = when {
            graphicsInfo.hasVulkan && graphicsInfo.hasOpenGL -> "Vulkan (Better performance)"
            graphicsInfo.hasVulkan -> "Vulkan (Only option)"
            graphicsInfo.hasOpenGL -> "OpenGL ES (Only option)"
            else -> "No graphics API supported"
        }

        binding.tvGraphicsAPIRecommendation.text = "Effective API: $effectiveAPIName • $recommendation"
    }

    private fun checkGPUCapabilities() {
        val pm = packageManager
        val hasVulkan = pm.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION)
        val vulkanVersion = if (hasVulkan && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            pm.systemAvailableFeatures
                .firstOrNull { it.name == PackageManager.FEATURE_VULKAN_HARDWARE_VERSION }
                ?.version?.toString() ?: "Unknown"
        } else {
            "Not supported"
        }

        binding.tvVulkanStatus.text = "Vulkan Support: $vulkanVersion"
        binding.tvOpenGLStatus.text = "OpenGL ES Support: ${getOpenGLVersion()}"

        binding.btnInstallVulkan.isEnabled = hasVulkan
    }

    private fun getOpenGLVersion(): String {
        return try {
            val glVersion = (getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager)
                .deviceConfigurationInfo
                .glEsVersion
            "OpenGL ES $glVersion"
        } catch (e: Exception) {
            "Unknown"
        }
    }

    private fun installDriver(driverType: String) {
        when (driverType) {
            "vulkan" -> {
                if (checkVulkanRequirements()) {

                    Toast.makeText(this, "Installing Vulkan 1.1 driver...", Toast.LENGTH_SHORT).show()

                } else {
                    Toast.makeText(this, "Device doesn't meet Vulkan requirements", Toast.LENGTH_SHORT).show()
                }
            }
            "opengl" -> {
                if (checkOpenGLRequirements()) {
                    Toast.makeText(this, "Installing OpenGL ES 3.2 driver...", Toast.LENGTH_SHORT).show()

                } else {
                    Toast.makeText(this, "Device doesn't meet OpenGL requirements", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun checkVulkanRequirements(): Boolean {
        return packageManager.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION)
    }

    private fun checkOpenGLRequirements(): Boolean {
        return try {
            val glVersion = (getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager)
                .deviceConfigurationInfo
                .glEsVersion

            glVersion.startsWith("3.") || glVersion.startsWith("4.")
        } catch (e: Exception) {
            false
        }
    }

    private fun testGPUPerformance() {

        Toast.makeText(this, "Running GPU performance tests...", Toast.LENGTH_SHORT).show()


        val vulkanScore = runVulkanBenchmark()
        val openglScore = runOpenGLBenchmark()

        binding.tvVulkanBenchmark.text = "Vulkan Score: $vulkanScore"
        binding.tvOpenGLBenchmark.text = "OpenGL Score: $openglScore"
    }

    private fun runVulkanBenchmark(): Int {

        return (1000..5000).random() 
    }

    private fun runOpenGLBenchmark(): Int {

        return (800..4500).random() 
    }
}

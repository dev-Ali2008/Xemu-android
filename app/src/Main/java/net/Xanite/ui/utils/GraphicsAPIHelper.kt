package com.xanite.utils

import android.content.Context
import android.content.pm.PackageManager
import androidx.preference.PreferenceManager

object GraphicsAPIHelper {
    
    const val GRAPHICS_API_VULKAN = "vulkan"
    const val GRAPHICS_API_OPENGL = "opengl"
    const val GRAPHICS_API_AUTO = "auto"
    
    /**
     * Get the current graphics API setting
     */
    fun getCurrentGraphicsAPI(context: Context): String {
        val sharedPref = PreferenceManager.getDefaultSharedPreferences(context)
        return sharedPref.getString("graphics_api", GRAPHICS_API_AUTO) ?: GRAPHICS_API_AUTO
    }
    
    /**
     * Get the effective graphics API based on device capabilities and user preference
     */
    fun getEffectiveGraphicsAPI(context: Context): String {
        val userChoice = getCurrentGraphicsAPI(context)
        
        return when (userChoice) {
            GRAPHICS_API_VULKAN -> {
                if (isVulkanSupported(context)) GRAPHICS_API_VULKAN else GRAPHICS_API_OPENGL
            }
            GRAPHICS_API_OPENGL -> {
                if (isOpenGLSupported(context)) GRAPHICS_API_OPENGL else GRAPHICS_API_VULKAN
            }
            GRAPHICS_API_AUTO -> {
                getRecommendedGraphicsAPI(context)
            }
            else -> getRecommendedGraphicsAPI(context)
        }
    }
    
    /**
     * Check if Vulkan is supported on this device
     */
    fun isVulkanSupported(context: Context): Boolean {
        return context.packageManager.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION)
    }
    
    /**
     * Check if OpenGL ES is supported on this device
     */
    fun isOpenGLSupported(context: Context): Boolean {
        return try {
            val glVersion = (context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager)
                .deviceConfigurationInfo
                .glEsVersion
            // Parse the version string to check if it's 3.2 or higher
            glVersion.startsWith("3.") || glVersion.startsWith("4.")
        } catch (e: Exception) {
            false
        }
    }
    
    /**
     * Get the recommended graphics API for this device
     */
    fun getRecommendedGraphicsAPI(context: Context): String {
        return when {
            isVulkanSupported(context) -> GRAPHICS_API_VULKAN
            isOpenGLSupported(context) -> GRAPHICS_API_OPENGL
            else -> GRAPHICS_API_OPENGL // Fallback to OpenGL
        }
    }
    
    /**
     * Get display name for graphics API
     */
    fun getGraphicsAPIDisplayName(api: String): String {
        return when (api) {
            GRAPHICS_API_VULKAN -> "Vulkan"
            GRAPHICS_API_OPENGL -> "OpenGL ES"
            GRAPHICS_API_AUTO -> "Auto (Recommended)"
            else -> "Unknown"
        }
    }
    
    /**
     * Get detailed information about graphics API support
     */
    fun getGraphicsAPIInfo(context: Context): GraphicsAPIInfo {
        val hasVulkan = isVulkanSupported(context)
        val hasOpenGL = isOpenGLSupported(context)
        val currentAPI = getCurrentGraphicsAPI(context)
        val effectiveAPI = getEffectiveGraphicsAPI(context)
        
        return GraphicsAPIInfo(
            hasVulkan = hasVulkan,
            hasOpenGL = hasOpenGL,
            currentAPI = currentAPI,
            effectiveAPI = effectiveAPI,
            recommendation = getRecommendedGraphicsAPI(context)
        )
    }
    
    /**
     * Data class to hold graphics API information
     */
    data class GraphicsAPIInfo(
        val hasVulkan: Boolean,
        val hasOpenGL: Boolean,
        val currentAPI: String,
        val effectiveAPI: String,
        val recommendation: String
    )
}

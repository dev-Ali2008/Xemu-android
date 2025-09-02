package com.xanite.utils

import android.content.Context

object RendererManager {
    
    enum class RendererType {
        NV2A,
        VULKAN,
        OPENGL
    }
    
    fun getCurrentRenderer(context: Context): RendererType {
        val rendererString = SharedPrefs.getRendererType(context)
        return when (rendererString) {
            "nv2a" -> RendererType.NV2A
            "vulkan" -> RendererType.VULKAN
            "opengl" -> RendererType.OPENGL
            else -> RendererType.NV2A // Default to NV2A for Xbox Original compatibility
        }
    }
    
    fun setRenderer(context: Context, rendererType: RendererType) {
        val rendererString = when (rendererType) {
            RendererType.NV2A -> "nv2a"
            RendererType.VULKAN -> "vulkan"
            RendererType.OPENGL -> "opengl"
        }
        SharedPrefs.saveRendererType(context, rendererString)
    }
    
    fun isNV2AEnabled(context: Context): Boolean {
        return getCurrentRenderer(context) == RendererType.NV2A
    }
    
    fun isVulkanEnabled(context: Context): Boolean {
        return getCurrentRenderer(context) == RendererType.VULKAN
    }
    
    fun isOpenGLEnabled(context: Context): Boolean {
        return getCurrentRenderer(context) == RendererType.OPENGL
    }
    
    fun getRendererName(context: Context): String {
        return when (getCurrentRenderer(context)) {
            RendererType.NV2A -> "NV2A"
            RendererType.VULKAN -> "Vulkan"
            RendererType.OPENGL -> "OpenGL"
        }
    }
    
    fun getRendererDescription(context: Context): String {
        return when (getCurrentRenderer(context)) {
            RendererType.NV2A -> "Original Xbox GPU emulation with best compatibility"
            RendererType.VULKAN -> "Modern graphics API with better performance"
            RendererType.OPENGL -> "Traditional graphics API with better compatibility"
        }
    }
}

package com.xanite.utils

import android.content.Context
import android.content.SharedPreferences

object SharedPrefs {
    private const val PREFS_NAME = "XanitePrefs"
    private const val KEY_BIOS_PATH = "Complex_4627v1.03.bin"
    private const val KEY_BIOS_VERSION = "mcpx_1.0.bin"
    private const val KEY_BIOS_VALID = "xbox_hdd.qcow2"
    private const val KEY_RENDERER_TYPE = "renderer_type"

    fun saveBiosInfo(
        context: Context,
        path: String,
        version: String,
        isValid: Boolean
    ) {
        getPrefs(context).edit().apply {
            putString(KEY_BIOS_PATH, path)
            putString(KEY_BIOS_VERSION, version)
            putBoolean(KEY_BIOS_VALID, isValid)
            apply()
        }
    }

    fun getBiosInfo(context: Context): BiosInfo? {
        val prefs = getPrefs(context)
        val path = prefs.getString(KEY_BIOS_PATH, null)
        val version = prefs.getString(KEY_BIOS_VERSION, null)
        
        return if (path != null && version != null) {
            BiosInfo(
                path = path,
                version = version,
                isValid = prefs.getBoolean(KEY_BIOS_VALID, false)
            )
        } else {
            null
        }
    }

    private fun getPrefs(context: Context): SharedPreferences {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    }

    // Renderer type management
    fun saveRendererType(context: Context, rendererType: String) {
        getPrefs(context).edit().apply {
            putString(KEY_RENDERER_TYPE, rendererType)
            apply()
        }
    }

    fun getRendererType(context: Context): String {
        return getPrefs(context).getString(KEY_RENDERER_TYPE, "opengl") ?: "opengl"
    }

    data class BiosInfo(
        val path: String,
        val version: String,
        val isValid: Boolean
    )
}
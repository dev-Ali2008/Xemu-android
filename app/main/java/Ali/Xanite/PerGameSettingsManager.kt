package Ali.Xanite

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import java.security.MessageDigest
import java.util.Locale

object PerGameSettingsManager {
  private const val APP_PREFS_NAME = "xaniteog_prefs"
  private const val STORE_PREFS_NAME = "xaniteog_per_game_settings"
  private const val STORE_KEY_PREFIX = "game_override:"
  private const val RUNTIME_KEY_PREFIX = "runtime_override_"

  val overridablePreferenceKeys = listOf(
    "setting_gpu_driver",
    "setting_renderer",
    "setting_filtering",
    "setting_vsync",
    "setting_surface_scale",
    "setting_display_mode",
    OrientationPreferences.PREF_GAME_ORIENTATION,
    "setting_system_memory_mib",
    "setting_tcg_thread",
    "setting_use_dsp",
    "setting_use_dsp_jit",
    "setting_hrtf",
    "setting_cache_shaders",
    "setting_hard_fpu",
    "setting_skip_boot_anim",
    "setting_audio_driver",
    "setting_network_enable",
    "draw_reorder",
    "draw_merge",
    "async_compile",
    "tier1_budget",
  )

  fun hasOverrides(context: Context, relativePath: String): Boolean {
    val prefs = storePreferences(context)
    val gameId = gameId(relativePath)
    return overridablePreferenceKeys.any { key ->
      prefs.contains(storageKey(gameId, key))
    }
  }

  fun loadOverrides(context: Context, relativePath: String): Map<String, String> {
    val prefs = storePreferences(context)
    val gameId = gameId(relativePath)
    return buildMap {
      for (key in overridablePreferenceKeys) {
        prefs.getString(storageKey(gameId, key), null)
          ?.takeIf { value -> value.isNotEmpty() }
          ?.let { value -> put(key, value) }
      }
    }
  }

  fun saveOverrides(
    context: Context,
    relativePath: String,
    overrides: Map<String, String?>,
  ) {
    val prefs = storePreferences(context)
    val gameId = gameId(relativePath)
    val editor = prefs.edit()
    for (key in overridablePreferenceKeys) {
      val value = overrides[key]
      if (value.isNullOrEmpty()) {
        editor.remove(storageKey(gameId, key))
      } else {
        editor.putString(storageKey(gameId, key), value)
      }
    }
    editor.apply()
  }

  fun clearOverrides(context: Context, relativePath: String) {
    val prefs = storePreferences(context)
    val gameId = gameId(relativePath)
    val editor = prefs.edit()
    for (key in overridablePreferenceKeys) {
      editor.remove(storageKey(gameId, key))
    }
    editor.apply()
  }

  fun applyRuntimeOverridesToEditor(
    context: Context,
    editor: SharedPreferences.Editor,
    relativePath: String?,
  ) {
    val path = relativePath?.takeIf { value -> value.isNotBlank() }
    val builtInOverrides = path?.let(::builtInCompatibilityProfile).orEmpty()
    val userOverrides = path?.let { value -> loadOverrides(context, value) }.orEmpty()
    val overrides = builtInOverrides + userOverrides

    if (builtInOverrides.isNotEmpty()) {
      Log.i(
        "Xanite-compat",
        "Applied built-in profile for ${path?.substringAfterLast('/')}; " +
          "user overrides take priority",
      )
    }

    for (key in overridablePreferenceKeys) {
      val runtimeKey = runtimeKey(key)
      val value = overrides[key]
      if (value.isNullOrEmpty()) {
        editor.remove(runtimeKey)
      } else {
        editor.putString(runtimeKey, value)
      }
    }
  }

  fun getRuntimeOverride(context: Context, key: String): String? {
    return appPreferences(context).getString(runtimeKey(key), null)
      ?.takeIf { value -> value.isNotEmpty() }
  }

  fun runtimeKey(key: String): String = RUNTIME_KEY_PREFIX + key

  /**
   * The built-in compatibility defaults for a title, so the UI can say which
   * settings a game silently starts from. Without this the per-game screen
   * reports the *global* value for keys a profile has changed - which is how
   * Forza came to show "Vulkan" while actually running OpenGL.
   */
  fun builtInProfileFor(relativePath: String): Map<String, String> =
    relativePath.takeIf { it.isNotBlank() }?.let(::builtInCompatibilityProfile).orEmpty()

  private fun gameId(relativePath: String): String {
    val normalized = relativePath.trim().lowercase(Locale.ROOT)
    val digest = MessageDigest.getInstance("SHA-256")
    val bytes = digest.digest(normalized.toByteArray(Charsets.UTF_8))
    return bytes.joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xFF) }
  }

  private fun storageKey(gameId: String, key: String): String =
    STORE_KEY_PREFIX + gameId + ":" + key

  /**
   * Conservative profiles for issues reproduced on this Android renderer.
   * They are deliberately keyed by the selected disc name, never global, and
   * every value can be replaced from the existing per-game settings screen.
   */
  private fun builtInCompatibilityProfile(relativePath: String): Map<String, String> {
    val name = relativePath.substringAfterLast('/').lowercase(Locale.ROOT)
    return when {
      "smashing drive" in name -> mapOf(
        // The supplied Adreno 750 trace showed both experimental draw
        // transformations enabled while reproducing black geometry.
        "draw_reorder" to "false",
        "draw_merge" to "false",
        "async_compile" to "false",
      )
      "forza" in name -> mapOf(
        // The renderer is deliberately NOT pinned here any more: this title now
        // runs on Vulkan, and it looks noticeably better than the OpenGL path,
        // so it follows the global setting like every other game.
        //
        // History, because this was hard-won and may come back: Vulkan used to
        // SIGSEGV inside the Adreno driver (null+0x28) in
        // pgraph_vk_update_descriptor_sets, roughly two thousand frames in. The
        // Khronos validation layer showed a descriptor set being updated while
        // still bound - render_thread.c calls pgraph_vk_finish ->
        // flush_all_frames, touching the descriptor ring, while the PFIFO
        // thread is inside pgraph_vk_update_descriptor_sets. It survived a
        // descriptor-pool lifetime fix, six guarded ring resets, wiring up
        // grow_descriptor_ring() and enforcing PFIFO ownership of the descriptor
        // indices, and the profile pinned the game to OpenGL as a workaround.
        //
        // If that crash reappears - a hard crash several minutes into a race,
        // not at startup - re-adding "setting_renderer" to "opengl" here is the
        // first thing to try, and the underlying descriptor race is the real
        // bug to fix.
        //
        // Measured on device: one vCPU thread pinned near 100% while the JIT
        // reported promoted=28800 against dropped=179843782, i.e. hot blocks
        // repeatedly crossing the promotion threshold and being refused for
        // want of budget, so the game ran largely untranslated. Frame pacing
        // held 60fps in menus and fell to 14-34fps in a race.
        //
        // Raising the per-window promotion budget lets the working set
        // translate in the first seconds instead of trickling out over a
        // session. Promotion happens once per block, so the extra compilation
        // is a one-off cost that pays back for the rest of the race.
        "tier1_budget" to "512",
        // Shader cache persists compiled shaders between runs, which is what
        // removes the repeated first-lap compilation stalls.
        "setting_cache_shaders" to "true",
      )
      "jet set radio future" in name || "jsrf" in name -> mapOf(
        // Keep JSRF on the conservative Vulkan path. The Android OpenGL
        // experiment emits GL_INVALID_OPERATION on this device and does not
        // provide a reliable replacement for the shared texture-cache fix.
        "setting_surface_scale" to "1",
        "setting_vsync" to "false",
        "async_compile" to "false",
      )
      else -> emptyMap()
    }
  }

  private fun appPreferences(context: Context): SharedPreferences {
    return context.applicationContext.getSharedPreferences(APP_PREFS_NAME, Context.MODE_PRIVATE)
  }

  private fun storePreferences(context: Context): SharedPreferences {
    return context.applicationContext.getSharedPreferences(STORE_PREFS_NAME, Context.MODE_PRIVATE)
  }
}

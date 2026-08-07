package Ali.Xanite

import android.content.Context
import android.content.SharedPreferences

class ControllerSettings(context: Context) {
  private val prefs: SharedPreferences = context.getSharedPreferences(
    "xaniteog_prefs",
    Context.MODE_PRIVATE
  )

  companion object {
    const val KEY_SHOW_CONTROLLER = "setting_show_controller"
    private const val KEY_VISIBLE_DEFAULT_MIGRATED =
      "touch_controls_visible_default_migrated_v1"
    private const val KEY_OPACITY = "setting_controller_opacity"
    private const val KEY_SCALE = "setting_controller_scale"

    /**
     * Older builds could persist the controller as hidden even though the
     * intended default is visible. Apply this once on upgrade, then continue
     * respecting whatever the user chooses afterward.
     */
    fun applyVisibleByDefaultMigration(context: Context) {
      val prefs = context.getSharedPreferences("xaniteog_prefs", Context.MODE_PRIVATE)
      if (prefs.getBoolean(KEY_VISIBLE_DEFAULT_MIGRATED, false)) return

      prefs.edit()
        .putBoolean(KEY_SHOW_CONTROLLER, true)
        .putBoolean(KEY_VISIBLE_DEFAULT_MIGRATED, true)
        .apply()
    }
  }

  var showOnScreenController: Boolean
    get() = prefs.getBoolean(KEY_SHOW_CONTROLLER, true)
    set(value) = prefs.edit().putBoolean(KEY_SHOW_CONTROLLER, value).apply()

  var controllerOpacity: Float
    get() = prefs.getFloat(KEY_OPACITY, 0.85f)
    set(value) = prefs.edit().putFloat(KEY_OPACITY, value.coerceIn(0f, 1f)).apply()

  var controllerScale: Float
    get() = prefs.getFloat(KEY_SCALE, 1.0f)
    set(value) = prefs.edit().putFloat(KEY_SCALE, value.coerceIn(0.5f, 2.0f)).apply()
}

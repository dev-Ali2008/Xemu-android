package Ali.Xanite

import android.content.Context
import android.content.SharedPreferences
import android.content.pm.ActivityInfo

object OrientationPreferences {
  private const val PREFS_NAME = "xaniteog_prefs"

  const val PREF_UI_ORIENTATION   = "setting_ui_orientation"
  const val PREF_GAME_ORIENTATION = "setting_game_orientation"

  enum class UiOrientation(
    val prefValue: String,
    val requestedOrientation: Int,
  ) {
    FOLLOW_DEVICE("follow_device", ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR),
    PORTRAIT("portrait", ActivityInfo.SCREEN_ORIENTATION_PORTRAIT),
    REVERSE_PORTRAIT("reverse_portrait", ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT),
    LANDSCAPE("landscape", ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE),
    REVERSE_LANDSCAPE("reverse_landscape", ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE),
    ;

    companion object {
      // Follow the phone by default. Users can still lock portrait or landscape.
      fun fromPrefValue(value: String?): UiOrientation =
        values().firstOrNull { it.prefValue == value } ?: FOLLOW_DEVICE
    }
  }

  enum class GameOrientation(
    val prefValue: String,
    val requestedOrientation: Int,
  ) {
    FOLLOW_DEVICE(
      "follow_device",
      ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
    ),
    LANDSCAPE(
      "landscape",
      ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE,
    ),
    REVERSE_LANDSCAPE(
      "reverse_landscape",
      ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE,
    ),
    ;

    companion object {
      // Game always defaults to sensor-landscape (like PUBG)
      fun fromPrefValue(value: String?): GameOrientation =
        values().firstOrNull { it.prefValue == value } ?: FOLLOW_DEVICE
    }
  }

  fun getUiOrientation(context: Context): UiOrientation =
    UiOrientation.fromPrefValue(sharedPreferences(context).getString(PREF_UI_ORIENTATION, null))

  fun getGameOrientation(context: Context): GameOrientation {
    val prefs = sharedPreferences(context)
    val runtimeOverride = PerGameSettingsManager.getRuntimeOverride(context, PREF_GAME_ORIENTATION)
    return GameOrientation.fromPrefValue(
      runtimeOverride ?: prefs.getString(PREF_GAME_ORIENTATION, null)
    )
  }

  fun getUiRequestedOrientation(context: Context): Int =
    getUiOrientation(context).requestedOrientation

  fun getGameRequestedOrientation(context: Context): Int =
    getGameOrientation(context).requestedOrientation

  fun setUiOrientation(context: Context, orientation: UiOrientation) {
    sharedPreferences(context).edit()
      .putString(PREF_UI_ORIENTATION, orientation.prefValue)
      .apply()
  }

  fun setGameOrientation(context: Context, orientation: GameOrientation) {
    sharedPreferences(context).edit()
      .putString(PREF_GAME_ORIENTATION, orientation.prefValue)
      .apply()
  }

  private fun sharedPreferences(context: Context): SharedPreferences =
    context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}

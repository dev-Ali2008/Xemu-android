package Ali.Xanite

import android.content.Context
import android.content.Intent
import android.net.Uri

/**
 * Single place that knows how to start a game.
 *
 * The library and home-screen shortcuts both go through here so the two can't
 * drift apart - in particular the per-game runtime overrides and the
 * synchronous commit(), which matters because MainActivity runs in the
 * :xaniteog process and reads SharedPreferences during startup.
 */
object GameLauncher {

  /**
   * Boots with no disc inserted, which lands on the dashboard installed on C:.
   * Needed for Insignia: signing up happens inside the retail dashboard using
   * a subscription code, not on the website.
   */
  fun launchDashboard(context: Context) {
    context.getSharedPreferences("xaniteog_prefs", Context.MODE_PRIVATE)
      .edit()
      .remove("dvdUri")
      .remove("dvdPath")
      .putBoolean("skip_game_picker", true)
      .commit()

    context.startActivity(
      Intent(context, MainActivity::class.java)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
    )
  }

  fun launch(context: Context, gameUri: Uri, relativePath: String) {
    val prefs = context.getSharedPreferences("xaniteog_prefs", Context.MODE_PRIVATE)
    val editor = prefs.edit()

    PerGameSettingsManager.applyRuntimeOverridesToEditor(
      context = context,
      editor = editor,
      relativePath = relativePath,
    )

    editor
      .putString("dvdUri", gameUri.toString())
      .remove("dvdPath")
      .putBoolean("skip_game_picker", false)
      .commit()

    context.startActivity(
      Intent(context, MainActivity::class.java)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
    )
  }
}

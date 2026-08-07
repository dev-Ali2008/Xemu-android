package Ali.Xanite

import android.app.Activity
import android.net.Uri
import android.os.Bundle
import android.widget.Toast

/**
 * Entry point for home-screen game shortcuts.
 *
 * Exported on purpose - the launcher has to be able to start it - but it takes
 * no action beyond launching a game the user already added to their own home
 * screen, and it verifies we still hold a persisted read permission for the
 * URI before doing so.
 */
class ShortcutLaunchActivity : Activity() {

  companion object {
    const val EXTRA_GAME_URI = "Ali.Xanite.extra.SHORTCUT_GAME_URI"
    const val EXTRA_GAME_RELATIVE_PATH = "Ali.Xanite.extra.SHORTCUT_GAME_RELATIVE_PATH"
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    val uriString = intent?.getStringExtra(EXTRA_GAME_URI)
    val relativePath = intent?.getStringExtra(EXTRA_GAME_RELATIVE_PATH).orEmpty()

    if (uriString.isNullOrEmpty()) {
      Toast.makeText(this, R.string.shortcut_launch_failed, Toast.LENGTH_SHORT).show()
      finish()
      return
    }

    val uri = Uri.parse(uriString)

    // The storage grant can be revoked after the shortcut was created (folder
    // re-picked, permission cleared). Fail with a message rather than dropping
    // the user into a black screen.
    val stillGranted = contentResolver.persistedUriPermissions.any {
      it.isReadPermission && (it.uri == uri || uriString.startsWith(it.uri.toString()))
    }
    if (!stillGranted) {
      Toast.makeText(this, R.string.shortcut_permission_lost, Toast.LENGTH_LONG).show()
      finish()
      return
    }

    GameLauncher.launch(this, uri, relativePath)
    finish()
  }
}

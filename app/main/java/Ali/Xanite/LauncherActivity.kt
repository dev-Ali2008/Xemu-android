package Ali.Xanite

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Toast
import java.io.File

class LauncherActivity : Activity() {
  override fun attachBaseContext(newBase: Context) {
    super.attachBaseContext(LocaleHelper.applyLocale(newBase))
  }

  companion object {
    private const val TAG = "LauncherActivity"
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    DebugLog.initialize(this)

    // Gold hard-block. The native core is the authority: it re-derives the
    // HWID and verifies the RSA signature on license.bin every boot, so
    // patching this Kotlin branch out still leaves an unlicensed build with a
    // core that refuses to run. Green skips it entirely.
    if (LicenseGate.isEnforced(this) && !LicenseGate.isActivated(this)) {
      startActivity(ActivationGatewayActivity.intent(this))
      finish()
      return
    }

    continueNavigation()
  }

  private fun continueNavigation() {
    try {
      val target = resolveNavigationAction()
      if (target != null) {
        startActivity(target)
      }
    } catch (t: Throwable) {
      // Last-resort safety net: never let LauncherActivity die with no UI.
      // Falls back to the setup wizard, which is the safest known-good screen.
      android.util.Log.e(TAG, "onCreate navigation failed, falling back to setup", t)
      try {
        startActivity(Intent(this, SetupWizardActivity::class.java))
      } catch (t2: Throwable) {
        android.util.Log.e(TAG, "Fallback to SetupWizardActivity also failed", t2)
      }
    } finally {
      finish()
    }
  }

  /** Reads preferences and intent, returns the target [Intent] or null to exit. */
  private fun resolveNavigationAction(): Intent? {
    try {

      val prefs = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
      var setupComplete = prefs.getBoolean("setup_complete", false)
      val mcpxUriStr = prefs.getString("mcpxUri", null)
      val flashUriStr = prefs.getString("flashUri", null)
      val hddUriStr = prefs.getString("hddUri", null)
      val dvdUriStr = prefs.getString("dvdUri", null)
      val gamesFolderUriStr = prefs.getString("gamesFolderUri", null)
      val mcpxPath = prefs.getString("mcpxPath", null)
      val flashPath = prefs.getString("flashPath", null)
      val hddPath = prefs.getString("hddPath", null)
      val dvdPath = prefs.getString("dvdPath", null)

      val mcpxUri = mcpxUriStr?.let(Uri::parse)
      val flashUri = flashUriStr?.let(Uri::parse)
      val hddUri = hddUriStr?.let(Uri::parse)
      val dvdUri = dvdUriStr?.let(Uri::parse)
      val gamesFolderUri = gamesFolderUriStr?.let(Uri::parse)
      val frontendLaunch = FrontendLaunchHelper.resolve(this, intent, gamesFolderUri)

      val hasMcpx = hasLocalFile(mcpxPath) || (mcpxUri != null && hasPersistedReadPermission(mcpxUri))
      val hasFlash = hasLocalFile(flashPath) || (flashUri != null && hasPersistedReadPermission(flashUri))
      val hasHdd = hasLocalFile(hddPath) || (hddUri != null && hasPersistedReadPermission(hddUri))
      val hasDvd = hasLocalFile(dvdPath) || (dvdUri != null && hasPersistedReadPermission(dvdUri))
      val hasGamesFolder = gamesFolderUri != null && hasPersistedReadPermission(gamesFolderUri)

      val editor = prefs.edit()
      var clearedCore = false
      var clearedOptional = false
      if (!hasMcpx && mcpxUriStr != null) {
        editor.remove("mcpxUri")
        clearedCore = true
      }
      if (!hasFlash && flashUriStr != null) {
        editor.remove("flashUri")
        clearedCore = true
      }
      if (!hasHdd && hddUriStr != null) {
        editor.remove("hddUri")
        clearedCore = true
      }
      if (!hasDvd && dvdUriStr != null) {
        editor.remove("dvdUri")
        clearedOptional = true
      }
      if (!hasMcpx && mcpxPath != null) {
        editor.remove("mcpxPath")
        clearedCore = true
      }
      if (!hasFlash && flashPath != null) {
        editor.remove("flashPath")
        clearedCore = true
      }
      if (!hasHdd && hddPath != null) {
        editor.remove("hddPath")
        clearedCore = true
      }
      if (!hasDvd && dvdPath != null) {
        editor.remove("dvdPath")
        clearedOptional = true
      }
      if (!hasGamesFolder && gamesFolderUriStr != null) {
        editor.remove("gamesFolderUri")
        clearedCore = true
      }
      if (clearedCore) {
        setupComplete = false
        editor.putBoolean("setup_complete", false)
        editor.putBoolean("skip_game_picker", false)
        editor.apply()
      } else if (clearedOptional) {
        editor.apply()
      }

      if (frontendLaunch != null) {
        if (frontendLaunch.dvdUri != null) {
          FrontendLaunchHelper.persistReadPermission(this, intent, frontendLaunch.dvdUri)
        }
        val launchEditor = prefs.edit()
        launchEditor.putBoolean("skip_game_picker", false)
        PerGameSettingsManager.applyRuntimeOverridesToEditor(
          context = this,
          editor = launchEditor,
          relativePath = frontendLaunch.relativePath,
        )
        when {
          frontendLaunch.dvdUri != null -> {
            launchEditor.putString("dvdUri", frontendLaunch.dvdUri.toString())
            launchEditor.remove("dvdPath")
          }
          frontendLaunch.dvdPath != null -> {
            launchEditor.putString("dvdPath", frontendLaunch.dvdPath)
            launchEditor.remove("dvdUri")
          }
        }
        launchEditor.commit()

        if (hasMcpx && hasFlash && hasHdd) {
          return Intent(this, MainActivity::class.java)
        }

        Toast.makeText(this, R.string.frontend_launch_setup_required, Toast.LENGTH_SHORT).show()
      } else if (hasExternalLaunchPayload(intent)) {
        Toast.makeText(this, R.string.frontend_launch_unresolved, Toast.LENGTH_LONG).show()
      }

      val needsSetup = !setupComplete || !hasMcpx || !hasFlash || !hasHdd || !hasGamesFolder
      return Intent(this, if (needsSetup) SetupWizardActivity::class.java else XboxDashboardActivity::class.java)
    } catch (e: Exception) {
      android.util.Log.e("LauncherActivity", "resolveNavigationAction failed", e)
      return Intent(this, SetupWizardActivity::class.java)
    }
  }

  private fun hasPersistedReadPermission(uri: Uri): Boolean {
    return contentResolver.persistedUriPermissions.any { perm ->
      perm.uri == uri && perm.isReadPermission
    }
  }

  private fun hasLocalFile(path: String?): Boolean {
    return path != null && File(path).isFile
  }

  private fun hasExternalLaunchPayload(intent: Intent?): Boolean {
    if (intent == null) {
      return false
    }
    if (intent.data != null || intent.clipData != null) {
      return true
    }
    return sequenceOf(
      Intent.EXTRA_STREAM,
      "rom",
      "ROM",
      "path",
      "PATH",
      "file",
      "FILE",
      "filename",
      "FILENAME",
      "romPath",
      "ROM_PATH",
      "uri",
      "URI",
    ).any { key -> intent.hasExtra(key) }
  }
}

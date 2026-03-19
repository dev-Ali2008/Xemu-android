package Ali.Xanite

import android.app.Activity
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import java.io.File

class LauncherActivity : Activity() {

    companion object {
        private const val TAG = "LauncherActivity"
        private const val PREFS_NAME = "Xanite_prefs"
    }

    private lateinit var prefs: SharedPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)

        try {
            initializeLauncher()
        } catch (e: Exception) {
            Log.e(TAG, "Fatal error in launcher", e)
            Toast.makeText(this, "Launch error: ${e.message}", Toast.LENGTH_LONG).show()
            startActivity(Intent(this, SetupWizardActivity::class.java))
            finish()
        }
    }

    private fun initializeLauncher() {
        Log.d(TAG, "=== LauncherActivity Started ===")

        val mcpxPath = prefs.getString("mcpxPath", null)
        val flashPath = prefs.getString("flashPath", null)
        val hddPath = prefs.getString("hddPath", null)
        val gamesFolderUriStr = prefs.getString("gamesFolderUri", null)

        val hasMcpxFile = mcpxPath?.let { File(it).exists() } ?: false
        val hasFlashFile = flashPath?.let { File(it).exists() } ?: false
        val hasHddFile = hddPath?.let { File(it).exists() } ?: false

        val mcpxUriStr = prefs.getString("mcpxUri", null)
        val flashUriStr = prefs.getString("flashUri", null)
        val hddUriStr = prefs.getString("hddUri", null)

        val gamesFolderUri = gamesFolderUriStr?.let { Uri.parse(it) }
        val mcpxUri = mcpxUriStr?.let { Uri.parse(it) }
        val flashUri = flashUriStr?.let { Uri.parse(it) }
        val hddUri = hddUriStr?.let { Uri.parse(it) }


        val hasMcpxUri = mcpxUri != null && hasPersistedReadPermission(mcpxUri)
        val hasFlashUri = flashUri != null && hasPersistedReadPermission(flashUri)
        val hasHddUri = hddUri != null && hasPersistedReadPermission(hddUri)
        val hasGamesFolder = gamesFolderUri != null && hasPersistedReadPermission(gamesFolderUri)

        val hasEssentialFiles = hasMcpxFile && hasFlashFile && hasHddFile
        val setupComplete = prefs.getBoolean("setup_complete", false)

        Log.d(TAG, "=== System Files Check ===")
        Log.d(TAG, "MCPX: file=$hasMcpxFile, path=$mcpxPath")
        Log.d(TAG, "FLASH: file=$hasFlashFile, path=$flashPath")
        Log.d(TAG, "HDD: file=$hasHddFile, path=$hddPath")
        Log.d(TAG, "Games Folder: uri=$hasGamesFolder")
        Log.d(TAG, "hasEssentialFiles=$hasEssentialFiles, setupComplete=$setupComplete")
        Log.d(TAG, "==========================")

        val editor = prefs.edit()

        if (!hasMcpxUri && mcpxUriStr != null) {
            Log.w(TAG, "Removing mcpxUri (permission lost)")
            editor.remove("mcpxUri")
        }
        if (!hasFlashUri && flashUriStr != null) {
            Log.w(TAG, "Removing flashUri (permission lost)")
            editor.remove("flashUri")
        }
        if (!hasHddUri && hddUriStr != null) {
            Log.w(TAG, "Removing hddUri (permission lost)")
            editor.remove("hddUri")
        }
        if (!hasGamesFolder && gamesFolderUriStr != null) {
            Log.w(TAG, "Removing gamesFolderUri (permission lost)")
            editor.remove("gamesFolderUri")

            editor.putBoolean("setup_complete", false)
            editor.apply()

            startActivity(Intent(this, SetupWizardActivity::class.java))
            finish()
            return
        }

        editor.apply()

        if (intent.data != null || hasGameLaunchIntent(intent)) {
            handleGameLaunchIntent(intent, hasEssentialFiles)
            return
        }

        val needsSetup = !setupComplete || !hasEssentialFiles || !hasGamesFolder

        val nextActivity = if (needsSetup) {
            Log.i(TAG, "Setup required - starting SetupWizardActivity")
            SetupWizardActivity::class.java
        } else {
            Log.i(TAG, "Setup complete - starting GameLibraryActivity")
            GameLibraryActivity::class.java
        }

        startActivity(Intent(this, nextActivity))
        finish()
    }

    private fun handleGameLaunchIntent(intent: Intent, hasEssentialFiles: Boolean) {
        try {
            val gameUri = intent.data
            if (gameUri != null) {

                prefs.edit()
                    .putString("dvdUri", gameUri.toString())
                    .putBoolean("skip_game_picker", false)
                    .apply()

                try {
                    contentResolver.takePersistableUriPermission(
                        gameUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION
                    )
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to persist URI permission", e)
                }
            }

            if (hasEssentialFiles) {
                Log.i(TAG, "Launching game directly")
                startActivity(Intent(this, MainActivity::class.java))
                finish()
            } else {
                Log.i(TAG, "Game launch queued but setup required")
                Toast.makeText(this, "Setup required before playing", Toast.LENGTH_SHORT).show()
                startActivity(Intent(this, SetupWizardActivity::class.java))
                finish()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error handling game launch", e)
            startActivity(Intent(this, GameLibraryActivity::class.java))
            finish()
        }
    }

    private fun hasGameLaunchIntent(intent: Intent): Boolean {
        return intent.hasExtra("rom") ||
                intent.hasExtra("ROM") ||
                intent.hasExtra("game") ||
                intent.hasExtra("GAME") ||
                intent.action == Intent.ACTION_VIEW
    }

    private fun hasPersistedReadPermission(uri: Uri): Boolean {
        return try {
            contentResolver.persistedUriPermissions.any { perm ->
                perm.uri == uri && perm.isReadPermission
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error checking permission", e)
            false
        }
    }
}

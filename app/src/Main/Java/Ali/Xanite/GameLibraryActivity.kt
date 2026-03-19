package Ali.Xanite

import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.OpenableColumns
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.method.LinkMovementMethod
import android.text.style.ClickableSpan
import android.view.LayoutInflater
import android.view.MenuItem
import android.view.View
import android.util.Log
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.GravityCompat
import androidx.drawerlayout.widget.DrawerLayout
import androidx.documentfile.provider.DocumentFile
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.navigation.NavigationView
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.ArrayDeque
import java.util.Date
import java.util.Locale

class GameLibraryActivity : AppCompatActivity(), NavigationView.OnNavigationItemSelectedListener {

    companion object {
        const val EXTRA_RESTART_LAST_GAME = "Ali.Xanite.extra.RESTART_LAST_GAME"
        private const val TAG = "GameLibraryActivity"
    }

    private data class GameEntry(
        val title: String,
        val uri: Uri,
        val relativePath: String,
        val sizeBytes: Long
    )

      private val prefs by lazy { getSharedPreferences("xanite_prefs", MODE_PRIVATE) }

    private val gameExts = setOf("iso", "xiso", "cso", "cci")

    private var scanGeneration = 0
    private var currentGames: List<GameEntry> = emptyList()
    private var gamesFolderUri: Uri? = null

    private lateinit var appBaseDir: File
    private lateinit var shaderDir: File
    private lateinit var saveDir: File
    private lateinit var memoryDir: File

    private lateinit var drawerLayout: DrawerLayout
    private lateinit var navView: NavigationView
    private lateinit var loadingSpinner: ProgressBar
    private lateinit var loadingText: TextView
    private lateinit var emptyText: TextView
    private lateinit var gamesListContainer: LinearLayout
    private lateinit var btnMenu: ImageButton
    private lateinit var scrollView: androidx.core.widget.NestedScrollView

    private val pickGamesFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            persistUriPermission(uri)
            gamesFolderUri = uri
            prefs.edit().putString("gamesFolderUri", uri.toString()).apply()
            loadGames()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_game_library)

        setupDirectories()

        if (tryRestartLastGameFromIntent()) {
            return
        }

        initializeViews()
        loadSavedData()
        setupClickListeners()
        setupDrawer()

        if (!isFolderReady(gamesFolderUri)) {

            if (gamesFolderUri != null) {
                showToast("Games folder is not accessible. Please select again.")
                prefs.edit().remove("gamesFolderUri").apply()
                gamesFolderUri = null
            }

            val setupComplete = prefs.getBoolean("setup_complete", false)
            if (!setupComplete) {
                showToast(getString(R.string.setup_pick_disc))
                startActivity(Intent(this, SetupWizardActivity::class.java))
                finish()
                return
            } else {
                showToast(getString(R.string.setup_pick_disc))
                pickGamesFolder.launch(null)
                return
            }
        }

        loadGames()
    }

    private fun setupDirectories() {

        appBaseDir = getExternalFilesDir(null) ?: filesDir

        shaderDir = File(appBaseDir, "shader")
        saveDir = File(appBaseDir, "save")
        memoryDir = File(appBaseDir, "memory")

        if (!shaderDir.exists()) shaderDir.mkdirs()
        if (!saveDir.exists()) saveDir.mkdirs()
        if (!memoryDir.exists()) memoryDir.mkdirs()

        Log.d(TAG, "Directories created:")
        Log.d(TAG, "Shader: ${shaderDir.absolutePath}")
        Log.d(TAG, "Save: ${saveDir.absolutePath}")
        Log.d(TAG, "Memory: ${memoryDir.absolutePath}")
    }

    override fun onResume() {
        super.onResume()

        if (isFolderReady(gamesFolderUri)) {
            loadGames()
        }
    }

    override fun onBackPressed() {
        if (drawerLayout.isDrawerOpen(GravityCompat.START)) {
            drawerLayout.closeDrawer(GravityCompat.START)
        } else {
            super.onBackPressed()
        }
    }

    override fun onNavigationItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            R.id.nav_settings -> {
                startActivity(Intent(this, SettingsActivity::class.java))
            }
            R.id.nav_games_folder -> {
                pickGamesFolder.launch(gamesFolderUri)
            }
            R.id.nav_info -> {
                showInfoDialog()
            }
        }
        drawerLayout.closeDrawer(GravityCompat.START)
        return true
    }

    private fun initializeViews() {
        drawerLayout = findViewById(R.id.drawer_layout)
        navView = findViewById(R.id.nav_view)
        loadingSpinner = findViewById(R.id.library_loading)
        loadingText = findViewById(R.id.library_loading_text)
        emptyText = findViewById(R.id.library_empty_text)
        gamesListContainer = findViewById(R.id.library_games_container)
        btnMenu = findViewById(R.id.btn_menu)
        scrollView = findViewById(R.id.library_scroll)
    }

    private fun loadSavedData() {
        val uriString = prefs.getString("gamesFolderUri", null)
        gamesFolderUri = uriString?.let { Uri.parse(it) }
        Log.d(TAG, "Loaded games folder: $gamesFolderUri")
    }

    private fun setupClickListeners() {
        btnMenu.setOnClickListener {
            drawerLayout.openDrawer(GravityCompat.START)
        }
    }

    private fun setupDrawer() {
        navView.setNavigationItemSelectedListener(this)
    }

    private fun loadGames() {
        val folderUri = gamesFolderUri
        if (!isFolderReady(folderUri)) {
            setLoading(false)
            currentGames = emptyList()
            renderGames()
            return
        }

        setLoading(true, getString(R.string.library_loading_games))

        val generation = ++scanGeneration
        Thread {
            val games = scanFolderForGames(folderUri!!)
            runOnUiThread {
                if (generation != scanGeneration) {
                    return@runOnUiThread
                }
                setLoading(false)
                currentGames = games
                renderGames()
            }
        }.start()
    }

    private fun scanFolderForGames(folderUri: Uri): List<GameEntry> {
        val root = DocumentFile.fromTreeUri(this, folderUri) ?: return emptyList()
        val stack = ArrayDeque<Pair<DocumentFile, String>>()
        stack.add(Pair(root, ""))

        val games = ArrayList<GameEntry>()

        while (stack.isNotEmpty()) {
            val (node, prefix) = stack.removeLast()

            val files = try {
                node.listFiles()
            } catch (e: Exception) {
                emptyArray()
            }

            for (child in files) {
                val name = child.name ?: continue

                if (child.isDirectory) {
                    stack.add(Pair(child, prefix + name + "/"))
                    continue
                }

                if (!child.isFile || !isSupportedGame(name)) {
                    continue
                }

                games.add(
                    GameEntry(
                        title = toGameTitle(name),
                        uri = child.uri,
                        relativePath = prefix + name,
                        sizeBytes = child.length()
                    )
                )
            }
        }

        games.sortBy { it.title.lowercase(Locale.ROOT) }
        return games
    }

    private fun isSupportedGame(name: String): Boolean {
        val lower = name.lowercase(Locale.ROOT)
        if (lower.endsWith(".xiso.iso")) {
            return true
        }
        val ext = lower.substringAfterLast('.', "")
        return ext.isNotEmpty() && gameExts.contains(ext)
    }

    private fun toGameTitle(fileName: String): String {
        val lower = fileName.lowercase(Locale.ROOT)
        return when {
            lower.endsWith(".xiso.iso") -> fileName.dropLast(".xiso.iso".length)
            fileName.contains('.') -> fileName.substringBeforeLast('.')
            else -> fileName
        }
    }


    private fun renderGames() {
        gamesListContainer.removeAllViews()

        if (currentGames.isEmpty()) {
            emptyText.visibility = View.VISIBLE
            return
        } else {
            emptyText.visibility = View.GONE
        }

        renderList(currentGames)
    }

    private fun renderList(games: List<GameEntry>) {
        val inflater = LayoutInflater.from(this)

        for (game in games) {
            val item = inflater.inflate(R.layout.item_game_entry, gamesListContainer, false)

            val nameText = item.findViewById<TextView>(R.id.game_name_text)
            val sizeText = item.findViewById<TextView>(R.id.game_size_text)
            val pathText = item.findViewById<TextView>(R.id.game_path_text)

            nameText.text = game.title
            sizeText.text = getString(R.string.library_game_size, formatSize(game.sizeBytes))
            pathText.text = getString(R.string.library_game_path, game.relativePath)

            item.setOnClickListener { launchGame(game) }

            item.setOnLongClickListener { 
                showGameContextMenu(game, item)
                true 
            }

            gamesListContainer.addView(item)
        }
    }

    private fun launchGame(game: GameEntry) {
        persistUriPermission(game.uri)

        val gameId = generateGameId(game.title)

        prefs.edit()
            .putString("dvdUri", game.uri.toString())
            .putString("current_game_id", gameId)
            .putString("current_game_title", game.title)
            .remove("dvdPath")
            .putBoolean("skip_game_picker", false)
            .apply()

        Log.d(TAG, "Launching game: ${game.title} (ID: $gameId)")

        val intent = Intent(this, MainActivity::class.java).apply {
            putExtra("game_id", gameId)
            putExtra("game_title", game.title)
            putExtra("game_uri", game.uri.toString())
        }

        startActivity(intent)
        finish()
    }

    private fun generateGameId(title: String): String {
        return title
            .lowercase()
            .replace(Regex("[^a-z0-9]"), "_")  
            .replace(Regex("_+"), "_")          
            .trim('_')                           
            .take(30)                             
    }

    private fun tryRestartLastGameFromIntent(): Boolean {
        if (!intent.getBooleanExtra(EXTRA_RESTART_LAST_GAME, false)) {
            return false
        }

        val gameId = prefs.getString("current_game_id", null)
        val gameTitle = prefs.getString("current_game_title", null)
        val dvdUri = prefs.getString("dvdUri", null)?.let { Uri.parse(it) }

        if (gameId != null && gameTitle != null && dvdUri != null && hasPersistedReadPermission(dvdUri)) {
            Log.d(TAG, "Restarting last game: $gameTitle")

            val intent = Intent(this, MainActivity::class.java).apply {
                putExtra("game_id", gameId)
                putExtra("game_title", gameTitle)
                putExtra("game_uri", dvdUri.toString())
            }

            startActivity(intent)
            finish()
            return true
        }

        showToast(getString(R.string.library_restart_failed))
        return false
    }

    private fun showGameContextMenu(game: GameEntry, anchor: View) {
        val popup = PopupMenu(this, anchor)
        popup.menuInflater.inflate(R.menu.menu_game_context, popup.menu)

        popup.setOnMenuItemClickListener { menuItem ->
            when (menuItem.itemId) {
                R.id.action_delete_game -> {
                    showDeleteGameDialog(game)
                    true
                }
                R.id.action_info -> {
                    showGameInfoDialog(game)
                    true
                }
                else -> false
            }
        }

        popup.show()
    }

    private fun showDeleteShaderDialog(game: GameEntry) {
        MaterialAlertDialogBuilder(this)
            .setTitle("Delete Shader")
            .setMessage("Are you sure you want to delete shader cache for ${game.title}?")
            .setPositiveButton("Delete") { _, _ ->
                val gameId = generateGameId(game.title)
                val gameShaderDir = File(shaderDir, gameId)

                if (gameShaderDir.exists() && gameShaderDir.isDirectory) {
                    val deleted = gameShaderDir.deleteRecursively()
                    if (deleted) {
                        showToast("✅ Shader cache deleted for ${game.title}")
                        Log.d(TAG, "Deleted shader: ${gameShaderDir.absolutePath}")
                    } else {
                        showToast("❌ Failed to delete shader cache")
                    }
                } else {
                    showToast("ℹ️ No shader cache found for this game")
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showDeleteDataMemoryDialog(game: GameEntry) {
        MaterialAlertDialogBuilder(this)
            .setTitle("Delete Data & Memory")
            .setMessage("This will delete all saved data and memory cards for ${game.title}. Are you sure?")
            .setPositiveButton("Delete") { _, _ ->
                var deletedAnything = false
                val gameId = generateGameId(game.title)

                val gameSaveDir = File(saveDir, gameId)
                if (gameSaveDir.exists() && gameSaveDir.isDirectory) {
                    if (gameSaveDir.deleteRecursively()) {
                        deletedAnything = true
                        Log.d(TAG, "Deleted save: ${gameSaveDir.absolutePath}")
                    }
                }

                val gameMemoryDir = File(memoryDir, gameId)
                if (gameMemoryDir.exists() && gameMemoryDir.isDirectory) {
                    if (gameMemoryDir.deleteRecursively()) {
                        deletedAnything = true
                        Log.d(TAG, "Deleted memory: ${gameMemoryDir.absolutePath}")
                    }
                }

                if (deletedAnything) {
                    showToast("✅ Save data and memory deleted for ${game.title}")
                } else {
                    showToast("ℹ️ No save data or memory found for this game")
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showDeleteGameDialog(game: GameEntry) {
        MaterialAlertDialogBuilder(this)
            .setTitle("Delete Game")
            .setMessage("Are you sure you want to delete ${game.title}? This will permanently remove the game file.")
            .setPositiveButton("Delete") { _, _ ->
                deleteGameFile(game)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun deleteGameFile(game: GameEntry) {
        try {
            val docFile = DocumentFile.fromSingleUri(this, game.uri)
            if (docFile != null && docFile.exists()) {
                val deleted = docFile.delete()
                if (deleted) {
                    showToast("✅ ${game.title} deleted successfully")
                    deleteAssociatedGameData(game)
                    loadGames()
                    return
                }
            }

            val path = getPathFromUri(game.uri)
            if (path != null) {
                val file = File(path)
                if (file.exists() && file.isFile) {
                    val deleted = file.delete()
                    if (deleted) {
                        showToast("✅ ${game.title} deleted successfully")
                        deleteAssociatedGameData(game)
                        loadGames()
                        return
                    }
                }
            }

            showToast("❌ Failed to delete ${game.title}. File may be protected or in use.")

        } catch (e: Exception) {
            showToast("Error deleting file: ${e.message}")
        }
    }

    private fun deleteAssociatedGameData(game: GameEntry) {
        val gameId = generateGameId(game.title)

        val gameShaderDir = File(shaderDir, gameId)
        val gameSaveDir = File(saveDir, gameId)
        val gameMemoryDir = File(memoryDir, gameId)

        if (gameShaderDir.exists()) gameShaderDir.deleteRecursively()
        if (gameSaveDir.exists()) gameSaveDir.deleteRecursively()
        if (gameMemoryDir.exists()) gameMemoryDir.deleteRecursively()

        val snapshotsDir = File(appBaseDir, "Xanite/snapshots/$gameId")
        if (snapshotsDir.exists()) snapshotsDir.deleteRecursively()

        Log.d(TAG, "Associated data deleted for game ID: $gameId")
    }

    private fun showDedicatedSettingsDialog(game: GameEntry) {
        val dialogView = layoutInflater.inflate(R.layout.dialog_game_settings, null)

        val switchWidescreen = dialogView.findViewById<MaterialSwitch>(R.id.switch_widescreen)
        val gameId = generateGameId(game.title)
        val prefs = getSharedPreferences("game_settings_$gameId", MODE_PRIVATE)

        switchWidescreen.isChecked = prefs.getBoolean("widescreen", false)

        MaterialAlertDialogBuilder(this)
            .setTitle("Settings for ${game.title}")
            .setView(dialogView)
            .setPositiveButton("Save") { _, _ ->
                prefs.edit()
                    .putBoolean("widescreen", switchWidescreen.isChecked)
                    .apply()

                if (switchWidescreen.isChecked) {
                    showToast("✅ Widescreen enabled for ${game.title}")
                } else {
                    showToast("✅ Standard 4:3 enabled for ${game.title}")
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showGameInfoDialog(game: GameEntry) {
        val gameId = generateGameId(game.title)

        val gameShaderDir = File(shaderDir, gameId)
        val gameSaveDir = File(saveDir, gameId)
        val gameMemoryDir = File(memoryDir, gameId)
        val snapshotsDir = File(appBaseDir, "Xanite/snapshots/$gameId")

        val shaderSize = if (gameShaderDir.exists()) formatSize(getFolderSize(gameShaderDir)) else "None"
        val saveSize = if (gameSaveDir.exists()) formatSize(getFolderSize(gameSaveDir)) else "None"
        val memorySize = if (gameMemoryDir.exists()) formatSize(getFolderSize(gameMemoryDir)) else "None"


        val snapshotCount = if (snapshotsDir.exists()) {
            snapshotsDir.listFiles { file -> file.extension == "state" }?.size ?: 0
        } else 0

        val gameStatus = when {
            gameShaderDir.exists() || gameSaveDir.exists() || gameMemoryDir.exists() || snapshotCount > 0 -> "Played"
            else -> "Not Played"
        }

        val lastModified = try {
            val file = File(getPathFromUri(game.uri) ?: "")
            if (file.exists()) {
                val date = Date(file.lastModified())
                SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US).format(date)
            } else {
                "Unknown"
            }
        } catch (e: Exception) {
            "Unknown"
        }

        val gameVersion = generateGameVersion(gameId)

        val info = """
            ID: $gameId
            Version: $gameVersion
            Game Status: $gameStatus
            Last Modified: $lastModified
            Snapshots: $snapshotCount

            Path: ${game.relativePath}
            Size: ${formatSize(game.sizeBytes)}
            Format: ${getGameFormat(game)}

            Cache:
            • Shader: $shaderSize
            • Save: $saveSize
            • Memory: $memorySize
        """.trimIndent()

        MaterialAlertDialogBuilder(this)
            .setTitle(game.title)
            .setMessage(info)
            .setPositiveButton("OK", null)
            .show()
    }

    private fun generateGameVersion(gameId: String): String {
        val hash = gameId.hashCode()
        val major = (hash and 0xFF) % 10
        val minor = ((hash shr 8) and 0xFF) % 10
        val patch = ((hash shr 16) and 0xFF) % 10
        return "1.$major.$minor"
    }

    private fun getGameFormat(game: GameEntry): String {
        val path = game.relativePath.lowercase()
        return when {
            path.endsWith(".iso") -> "ISO"
            path.endsWith(".xiso") -> "XISO"
            path.endsWith(".xiso.iso") -> "XISO"
            path.endsWith(".cso") -> "CSO (Compressed)"
            path.endsWith(".cci") -> "CCI"
            else -> "Unknown"
        }
    }

    private fun getFolderSize(dir: File): Long {
        if (!dir.exists()) return 0

        var size = 0L
        if (dir.isDirectory) {
            dir.listFiles()?.forEach { file ->
                size += if (file.isFile) file.length() else getFolderSize(file)
            }
        }
        return size
    }

    private fun getPathFromUri(uri: Uri): String? {
        val projection = arrayOf(android.provider.MediaStore.MediaColumns.DATA)
        try {
            contentResolver.query(uri, projection, null, null, null)?.use { cursor ->
                val columnIndex = cursor.getColumnIndexOrThrow(android.provider.MediaStore.MediaColumns.DATA)
                if (cursor.moveToFirst()) {
                    return cursor.getString(columnIndex)
                }
            }
        } catch (e: Exception) {

        }
        return null
    }


    private fun setLoading(loading: Boolean, message: String? = null) {
        if (message != null) {
            loadingText.text = message
        }
        loadingSpinner.visibility = if (loading) View.VISIBLE else View.GONE
        loadingText.visibility = if (loading) View.VISIBLE else View.GONE
    }

    private fun formatSize(bytes: Long): String {
        if (bytes <= 0L) {
            return "None"
        }
        val units = arrayOf("B", "KB", "MB", "GB", "TB")
        var value = bytes.toDouble()
        var unitIndex = 0
        while (value >= 1024.0 && unitIndex < units.lastIndex) {
            value /= 1024.0
            unitIndex++
        }
        return String.format(Locale.US, "%.1f %s", value, units[unitIndex])
    }

    private fun isFolderReady(uri: Uri?): Boolean {
        if (uri == null) return false
        if (!hasPersistedReadPermission(uri)) return false

        return try {
            val root = DocumentFile.fromTreeUri(this, uri)
            root != null && root.exists() && root.isDirectory
        } catch (e: Exception) {
            false
        }
    }

    private fun persistUriPermission(uri: Uri) {
        val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        try {
            contentResolver.takePersistableUriPermission(uri, flags)
        } catch (e: SecurityException) {

        }
    }

    private fun hasPersistedReadPermission(uri: Uri): Boolean {
        return try {
            contentResolver.persistedUriPermissions.any { perm ->
                perm.uri == uri && perm.isReadPermission
            }
        } catch (e: Exception) {
            false
        }
    }

    private fun showToast(message: String) {
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
    }

    private fun showToastLong(message: String) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    private fun showInfoDialog() {
        val message = """
            Xanite - Original Xbox Emulator for Android

            Version: 1.0

            This emulator allows you to play your favorite Original Xbox games on Android devices.

            Features:
            • High compatibility with Xbox games
            • Save States (5 slots per game)
            • Controller support
            • Customizable controls

            Developed by: Xanite Team
            License: GPL v2

            For more information:

            GitHub
            https://github.com/dev-Ali2008/xanite

            Web
            https://dev-ali2008.github.io/Xanite.io/#social

            © 2024 Xanite Team
        """.trimIndent()

        val spannableString = SpannableStringBuilder().apply {
            append(message)

            val githubStart = message.indexOf("https://github.com/dev-Ali2008/xanite")
            if (githubStart >= 0) {
                setSpan(
                    object : ClickableSpan() {
                        override fun onClick(widget: View) {
                            openExternalLink("https://github.com/dev-Ali2008/xanite")
                        }
                    },
                    githubStart,
                    githubStart + "https://github.com/dev-Ali2008/xanite".length,
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }

            val webStart = message.indexOf("https://dev-ali2008.github.io/Xanite.io/#social")
            if (webStart >= 0) {
                setSpan(
                    object : ClickableSpan() {
                        override fun onClick(widget: View) {
                            openExternalLink("https://dev-ali2008.github.io/Xanite.io/#social")
                        }
                    },
                    webStart,
                    webStart + "https://dev-ali2008.github.io/Xanite.io/#social".length,
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
        }

        val dialog = MaterialAlertDialogBuilder(this)
            .setTitle(R.string.app_name)
            .setMessage(spannableString)
            .setPositiveButton(android.R.string.ok, null)
            .show()

        dialog.findViewById<TextView>(android.R.id.message)?.apply {
            movementMethod = LinkMovementMethod.getInstance()
            linksClickable = true
        }
    }

    private fun openExternalLink(url: String) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url)).apply {
            addCategory(Intent.CATEGORY_BROWSABLE)
        }
        try {
            startActivity(intent)
        } catch (e: Exception) {
            showToast(getString(R.string.library_about_open_failed))
        }
    }

    private fun copyUriToFile(uri: Uri, target: File): Boolean {
        return try {
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(target).use { output ->
                    input.copyTo(output)
                }
            } ?: return false
            true
        } catch (e: IOException) {
            false
        }
    }

    private fun copyFileToUri(source: File, targetUri: Uri): Boolean {
        return try {
            contentResolver.openOutputStream(targetUri, "w")?.use { output ->
                FileInputStream(source).use { input ->
                    input.copyTo(output)
                }
            } ?: return false
            true
        } catch (e: IOException) {
            false
        }
    }
}

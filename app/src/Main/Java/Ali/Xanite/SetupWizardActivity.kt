package Ali.Xanite

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.concurrent.Executors

class SetupWizardActivity : AppCompatActivity() {

    private val prefs by lazy { getSharedPreferences("xanite_prefs", MODE_PRIVATE) }
    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    private lateinit var setupCard: MaterialCardView
    private lateinit var welcomeSection: LinearLayout
    private lateinit var progressIndicator: TextView

    private lateinit var pageWelcome: LinearLayout
    private lateinit var pageBios: LinearLayout
    private lateinit var pageGames: LinearLayout
    private lateinit var pageComplete: LinearLayout
    private lateinit var pages: List<LinearLayout>

    private lateinit var mcpxPathText: TextView
    private lateinit var flashPathText: TextView
    private lateinit var hddPathText: TextView
    private lateinit var gamesPathText: TextView

    private lateinit var summaryMcpx: TextView
    private lateinit var summaryFlash: TextView
    private lateinit var summaryHdd: TextView
    private lateinit var summaryGames: TextView

    private lateinit var btnBack: MaterialButton
    private lateinit var btnNext: MaterialButton
    private lateinit var btnFinish: MaterialButton

    private var mcpxUri: Uri? = null
    private var flashUri: Uri? = null
    private var hddUri: Uri? = null
    private var gamesFolderUri: Uri? = null

    private var mcpxPath: String? = null
    private var flashPath: String? = null
    private var hddPath: String? = null

    private var currentStep = 0
    private var isCopying = false
    private var filesReady = 0

    private val mcpxExts = setOf("bin", "rom", "img")
    private val flashExts = setOf("bin", "rom", "img")
    private val hddExts = setOf("qcow2", "img")

    private val pickMcpx = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let {
            if (!isAllowedExtension(it, mcpxExts)) {
                return@registerForActivityResult
            }
            persistUriPermission(it)
            mcpxUri = it
            prefs.edit().putString("mcpxUri", it.toString()).apply()

            copyUriAsync(it, "mcpx.bin") { path ->
                if (path != null) {
                    mcpxPath = path
                    prefs.edit().putString("mcpxPath", path).apply()
                    filesReady++
                }
                updateMcpxSelection()
                updateAllSummaries()
                updateButtons()
                checkAllBiosFiles()
            }
        }
    }

    private val pickFlash = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let {
            if (!isAllowedExtension(it, flashExts)) {
                return@registerForActivityResult
            }
            persistUriPermission(it)
            flashUri = it
            prefs.edit().putString("flashUri", it.toString()).apply()

            copyUriAsync(it, "flash.bin") { path ->
                if (path != null) {
                    flashPath = path
                    prefs.edit().putString("flashPath", path).apply()
                    filesReady++
                }
                updateFlashSelection()
                updateAllSummaries()
                updateButtons()
                checkAllBiosFiles()
            }
        }
    }

    private val pickHdd = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let {
            if (!isAllowedExtension(it, hddExts)) {
                return@registerForActivityResult
            }
            persistUriPermission(it)
            hddUri = it
            prefs.edit().putString("hddUri", it.toString()).apply()

            copyUriAsync(it, "hdd.img") { path ->
                if (path != null) {
                    hddPath = path
                    prefs.edit().putString("hddPath", path).apply()
                    filesReady++
                }
                updateHddSelection()
                updateAllSummaries()
                updateButtons()
                checkAllBiosFiles()
            }
        }
    }

    private val pickGamesFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        uri?.let {
            persistUriPermission(it)
            gamesFolderUri = it
            prefs.edit().putString("gamesFolderUri", it.toString()).apply()

            updateGamesSelection()
            updateAllSummaries()
            updateButtons()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_setup_wizard)

        loadSavedData()

        if (isSetupComplete()) {
            goToLibrary()
            return
        }

        initializeViews()
        setupClickListeners()
        setupBackPressedHandler()

        filesReady = countReadyFiles()
        updateAllSelections()
        updateAllSummaries()

        showStep(0)
    }

    private fun countReadyFiles(): Int {
        var count = 0
        if (isFileReady(mcpxPath)) count++
        if (isFileReady(flashPath)) count++
        if (isFileReady(hddPath)) count++
        return count
    }

    private fun checkAllBiosFiles() {
        if (filesReady == 3 && currentStep == 1) {

            btnNext.isEnabled = true
        }
    }

    private fun loadSavedData() {
        mcpxPath = loadLocalPath("mcpxPath")
        flashPath = loadLocalPath("flashPath")
        hddPath = loadLocalPath("hddPath")

        mcpxUri = prefs.getString("mcpxUri", null)?.let(Uri::parse)
        flashUri = prefs.getString("flashUri", null)?.let(Uri::parse)
        hddUri = prefs.getString("hddUri", null)?.let(Uri::parse)
        gamesFolderUri = prefs.getString("gamesFolderUri", null)?.let(Uri::parse)

        Log.d(TAG, "Loaded: mcpx=$mcpxPath, flash=$flashPath, hdd=$hddPath, games=$gamesFolderUri")
    }

    private fun isSetupComplete(): Boolean {
        return prefs.getBoolean("setup_complete", false) &&
               isFileReady(mcpxPath) &&
               isFileReady(flashPath) &&
               isFileReady(hddPath)
    }

    private fun initializeViews() {
        setupCard = findViewById(R.id.setup_card)
        welcomeSection = findViewById(R.id.welcome_section)
        progressIndicator = findViewById<TextView>(R.id.progress_indicator).apply {
            visibility = View.GONE
        }

        pageWelcome = findViewById(R.id.page_welcome)
        pageBios = findViewById(R.id.page_bios)
        pageGames = findViewById(R.id.page_games)
        pageComplete = findViewById(R.id.page_complete)
        pages = listOf(pageWelcome, pageBios, pageGames, pageComplete)

        mcpxPathText = findViewById(R.id.mcpx_path_text)
        flashPathText = findViewById(R.id.flash_path_text)
        hddPathText = findViewById(R.id.hdd_path_text)
        gamesPathText = findViewById(R.id.games_path_text)

        summaryMcpx = findViewById(R.id.summary_mcpx)
        summaryFlash = findViewById(R.id.summary_flash)
        summaryHdd = findViewById(R.id.summary_hdd)
        summaryGames = findViewById(R.id.summary_games)

        btnBack = findViewById(R.id.btn_wizard_back)
        btnNext = findViewById(R.id.btn_wizard_next)
        btnFinish = findViewById(R.id.btn_wizard_finish)
    }

    private fun setupClickListeners() {
        findViewById<MaterialButton>(R.id.btn_pick_mcpx).setOnClickListener {
            pickMcpx.launch("*/*")
        }

        findViewById<MaterialButton>(R.id.btn_pick_flash).setOnClickListener {
            pickFlash.launch("*/*")
        }

        findViewById<MaterialButton>(R.id.btn_pick_hdd).setOnClickListener {
            pickHdd.launch("*/*")
        }

        findViewById<MaterialButton>(R.id.btn_pick_games).setOnClickListener {
            pickGamesFolder.launch(null)
        }

        btnBack.setOnClickListener {
            if (currentStep > 0) {
                showStep(currentStep - 1)
            }
        }

        btnNext.setOnClickListener {
            when (currentStep) {
                0 -> {

                    showStep(1)
                }
                1 -> {

                    if (filesReady == 3) {
                        showStep(2)
                    }
                }
                2 -> {

                    if (gamesFolderUri != null) {
                        showStep(3)
                        updateAllSummaries()
                    }
                }
            }
        }

        btnFinish.setOnClickListener {
            finishSetup()
        }
    }

    private fun setupBackPressedHandler() {
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (currentStep > 0) {
                    showStep(currentStep - 1)
                } else {
                    showExitConfirmationDialog()
                }
            }
        })
    }

    private fun showStep(step: Int) {
        if (isCopying) return

        progressIndicator.visibility = View.VISIBLE
        progressIndicator.text = "${step + 1}/${pages.size}"

        currentStep = step


        pages.forEachIndexed { index, page ->
            page.visibility = if (index == currentStep) View.VISIBLE else View.GONE
        }

        updateButtons()
    }

    private fun updateButtons() {
        when (currentStep) {
            0 -> { 
                btnBack.visibility = View.GONE
                btnNext.visibility = View.VISIBLE
                btnFinish.visibility = View.GONE
                btnNext.isEnabled = true
                btnNext.text = "Start Setup →"
            }
            1 -> { 
                btnBack.visibility = View.VISIBLE
                btnNext.visibility = View.VISIBLE
                btnFinish.visibility = View.GONE
                btnNext.isEnabled = filesReady == 3
                btnNext.text = if (filesReady == 3) "Next →" else "Select All Files"
            }
            2 -> { 
                btnBack.visibility = View.VISIBLE
                btnNext.visibility = View.VISIBLE
                btnFinish.visibility = View.GONE
                btnNext.isEnabled = gamesFolderUri != null
                btnNext.text = "Review →"
            }
            3 -> { 
                btnBack.visibility = View.GONE
                btnNext.visibility = View.GONE
                btnFinish.visibility = View.VISIBLE
                btnFinish.isEnabled = true
                btnFinish.text = "Start Playing"
            }
        }
    }

    private fun updateAllSelections() {
        updateMcpxSelection()
        updateFlashSelection()
        updateHddSelection()
        updateGamesSelection()
    }

    private fun updateMcpxSelection() {
        val displayText = if (isFileReady(mcpxPath)) {
            "✓ ${File(mcpxPath!!).name}"
        } else if (mcpxUri != null) {
            getFileName(mcpxUri!!) ?: "Selected"
        } else {
            "Tap to select MCPX file"
        }
        mcpxPathText.text = displayText
        mcpxPathText.setTextColor(if (isFileReady(mcpxPath)) resources.getColor(android.R.color.holo_green_light, theme) else resources.getColor(android.R.color.white, theme))
    }

    private fun updateFlashSelection() {
        val displayText = if (isFileReady(flashPath)) {
            "✓ ${File(flashPath!!).name}"
        } else if (flashUri != null) {
            getFileName(flashUri!!) ?: "Selected"
        } else {
            "Tap to select BIOS file"
        }
        flashPathText.text = displayText
        flashPathText.setTextColor(if (isFileReady(flashPath)) resources.getColor(android.R.color.holo_green_light, theme) else resources.getColor(android.R.color.white, theme))
    }

    private fun updateHddSelection() {
        val displayText = if (isFileReady(hddPath)) {
            "✓ ${File(hddPath!!).name}"
        } else if (hddUri != null) {
            getFileName(hddUri!!) ?: "Selected"
        } else {
            "Tap to select HDD image"
        }
        hddPathText.text = displayText
        hddPathText.setTextColor(if (isFileReady(hddPath)) resources.getColor(android.R.color.holo_green_light, theme) else resources.getColor(android.R.color.white, theme))
    }

    private fun updateGamesSelection() {
        val displayText = if (gamesFolderUri != null) {
            "✓ ${DocumentFile.fromTreeUri(this, gamesFolderUri!!)?.name ?: "Games folder"}"
        } else {
            "Tap to select games folder"
        }
        gamesPathText.text = displayText
        gamesPathText.setTextColor(if (gamesFolderUri != null) resources.getColor(android.R.color.holo_green_light, theme) else resources.getColor(android.R.color.white, theme))
    }

    private fun updateAllSummaries() {
        summaryMcpx.text = if (isFileReady(mcpxPath)) "✓ MCPX: ${File(mcpxPath!!).name}" else "○ MCPX: Not set"
        summaryFlash.text = if (isFileReady(flashPath)) "✓ BIOS: ${File(flashPath!!).name}" else "○ BIOS: Not set"
        summaryHdd.text = if (isFileReady(hddPath)) "✓ HDD: ${File(hddPath!!).name}" else "○ HDD: Not set"
        summaryGames.text = if (gamesFolderUri != null) {
            val name = DocumentFile.fromTreeUri(this, gamesFolderUri!!)?.name ?: "Games folder"
            "✓ Games: $name"
        } else {
            "○ Games: Not set"
        }
    }

    private fun finishSetup() {

        if (!isFileReady(mcpxPath) || !isFileReady(flashPath) || !isFileReady(hddPath)) {
            return
        }

        prefs.edit()
            .putBoolean("setup_complete", true)
            .putBoolean("skip_game_picker", false)
            .apply()

        Log.d(TAG, "Setup completed!")

        goToLibrary()
    }

    private fun goToLibrary() {
        startActivity(Intent(this, GameLibraryActivity::class.java))
        finish()
    }

    private fun showExitConfirmationDialog() {
        MaterialAlertDialogBuilder(this)
            .setTitle("Exit Setup?")
            .setMessage("Setup is required for first-time use. Are you sure you want to exit?")
            .setPositiveButton("Exit") { _, _ ->
                finish()
            }
            .setNegativeButton("Stay", null)
            .show()
    }

    private fun persistUriPermission(uri: Uri) {
        val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        try {
            contentResolver.takePersistableUriPermission(uri, flags)
        } catch (_: SecurityException) {
        }
    }

    private fun loadLocalPath(key: String): String? {
        val path = prefs.getString(key, null) ?: return null
        return if (File(path).isFile) path else null.also {
            prefs.edit().remove(key).apply()
        }
    }

    private fun isFileReady(path: String?): Boolean {
        return path != null && File(path).isFile
    }

    private fun getFileName(uri: Uri): String? {
        return contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (nameIndex >= 0 && cursor.moveToFirst()) cursor.getString(nameIndex) else null
        }
    }

    private fun copyUriAsync(uri: Uri, destName: String, onDone: (String?) -> Unit) {
        if (isCopying) return
        isCopying = true
        updateButtons()

        executor.execute {
            val path = copyUriToAppStorage(uri, destName)
            mainHandler.post {
                isCopying = false
                onDone(path)
            }
        }
    }

    private fun copyUriToAppStorage(uri: Uri, destName: String): String? {
        val base = getExternalFilesDir(null) ?: filesDir
        val dir = File(base, "xanite")
        if (!dir.exists() && !dir.mkdirs()) {
            Log.e(TAG, "Failed to create directory")
            return null
        }

        val target = File(dir, destName)
        return try {
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(target).use { output ->
                    input.copyTo(output)
                }
            } ?: return null
            Log.d(TAG, "File copied to: ${target.absolutePath}")
            target.absolutePath
        } catch (e: IOException) {
            Log.e(TAG, "Copy failed", e)
            null
        }
    }

    private fun isAllowedExtension(uri: Uri, allowed: Set<String>): Boolean {
        val name = getFileName(uri) ?: uri.lastPathSegment ?: return false
        val ext = name.substringAfterLast('.', "").lowercase()
        return ext.isNotEmpty() && allowed.contains(ext)
    }

    companion object {
        private const val TAG = "SetupWizard"
    }
}

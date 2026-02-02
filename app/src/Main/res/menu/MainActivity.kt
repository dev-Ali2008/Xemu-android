package og.xaniteog

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.documentfile.provider.DocumentFile
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "MainActivity"
    }

    // ===== UI Components =====
    private lateinit var recyclerViewGames: RecyclerView
    private lateinit var btnSelectFolder: TextView
    private lateinit var tvCurrentFolder: TextView
    private lateinit var layoutEmpty: LinearLayout
    private lateinit var tvTitle: TextView
    private lateinit var tvHelp: TextView
    private lateinit var menuButton: ImageButton
    private lateinit var sideMenuLayout: LinearLayout
    
    // ===== القائمة الجانبية =====
    private lateinit var btnMemory: TextView
    private lateinit var btnMusic: TextView
    private lateinit var btnXboxLive: TextView
    private lateinit var btnSettings: TextView
    private lateinit var btnLibrary: TextView
    private lateinit var btnAbout: TextView
    
    // ===== Games =====
    private var currentGameDir: File = Environment.getExternalStorageDirectory()
    
    // ===== Permissions & Launchers =====
    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val allGranted = permissions.values.all { it }
        if (allGranted) {
            loadGameList()
        } else {
            Toast.makeText(this, "Storage permission required", Toast.LENGTH_LONG).show()
        }
    }

    private val pickFolderLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == RESULT_OK) {
            result.data?.data?.let { uri ->
                try {
                    // منح صلاحية دائمة
                    contentResolver.takePersistableUriPermission(
                        uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    )
                    
                    val folderPath = getPathFromUri(uri)
                    folderPath?.let {
                        currentGameDir = File(it)
                        updateCurrentFolderDisplay()
                        loadGameList()
                        closeSideMenu()
                    }
                } catch (e: Exception) {
                    Toast.makeText(this, "Error accessing folder: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        initializeUI()
        checkPermissions()
    }

    private fun initializeUI() {
        // عناصر الواجهة الرئيسية
        recyclerViewGames = findViewById(R.id.recyclerViewGames)
        btnSelectFolder = findViewById(R.id.btnSelectFolder)
        tvCurrentFolder = findViewById(R.id.tvCurrentFolder)
        layoutEmpty = findViewById(R.id.layoutEmpty)
        tvTitle = findViewById(R.id.tvTitle)
        tvHelp = findViewById(R.id.tvHelp)
        menuButton = findViewById(R.id.menuButton)
        sideMenuLayout = findViewById(R.id.sideMenuLayout)
        
        // عناصر القائمة الجانبية
        btnMemory = findViewById(R.id.btnMemory)
        btnMusic = findViewById(R.id.btnMusic)
        btnXboxLive = findViewById(R.id.btnXboxLive)
        btnSettings = findViewById(R.id.btnSettings)
        btnLibrary = findViewById(R.id.btnLibrary)
        btnAbout = findViewById(R.id.btnAbout)
        
        // Setup games recycler view
        recyclerViewGames.layoutManager = LinearLayoutManager(this)
        recyclerViewGames.setHasFixedSize(true)
        
        // Setup buttons
        btnSelectFolder.setOnClickListener {
            selectGameFolder()
        }
        
        // زر القائمة الجانبية
        menuButton.setOnClickListener {
            toggleSideMenu()
        }
        
        // أحداث القائمة الجانبية
        setupSideMenuEvents()
        
        // تحديث عرض المجلد
        updateCurrentFolderDisplay()
        
        // تحميل قائمة الألعاب
        loadGameList()
        
        // إخفاء القائمة الجانبية في البداية
        sideMenuLayout.visibility = View.GONE
    }
    
    private fun setupSideMenuEvents() {
        // MEMORY - إدارة الذاكرة والمحفوظات
        btnMemory.setOnClickListener {
            showToast("Memory Management - Coming Soon")
            closeSideMenu()
        }
        
        // MUSIC - خيارات الصوت
        btnMusic.setOnClickListener {
            showToast("Sound & Audio Options - Coming Soon")
            closeSideMenu()
        }
        
        // XBOX LIVE - الميزات الشبكية
        btnXboxLive.setOnClickListener {
            showToast("Xbox Live Network Features - Coming Soon")
            closeSideMenu()
        }
        
        // SETTINGS - إعدادات Xemu
        btnSettings.setOnClickListener {
            showToast("Xemu Settings - Coming Soon")
            closeSideMenu()
        }
        
        // LIBRARY - تصفح وتشغيل الألعاب
        btnLibrary.setOnClickListener {
            closeSideMenu()
            // نقوم بالفعل في المكتبة
        }
        
        // ABOUT - المعلومات القانونية
        btnAbout.setOnClickListener {
            showAboutDialog()
            closeSideMenu()
        }
    }
    
    private fun toggleSideMenu() {
        if (sideMenuLayout.visibility == View.VISIBLE) {
            closeSideMenu()
        } else {
            openSideMenu()
        }
    }
    
    private fun openSideMenu() {
        sideMenuLayout.visibility = View.VISIBLE
        sideMenuLayout.animate()
            .translationX(0f)
            .setDuration(300)
            .start()
        
        // تظليل الخلفية
        val overlay: View = findViewById(R.id.overlay)
        overlay.visibility = View.VISIBLE
        overlay.animate()
            .alpha(0.5f)
            .setDuration(300)
            .start()
    }
    
    private fun closeSideMenu() {
        sideMenuLayout.animate()
            .translationX((-sideMenuLayout.width).toFloat())
            .setDuration(300)
            .withEndAction {
                sideMenuLayout.visibility = View.GONE
            }
            .start()
        
        // إزالة التظليل
        val overlay: View = findViewById(R.id.overlay)
        overlay.animate()
            .alpha(0f)
            .setDuration(300)
            .withEndAction {
                overlay.visibility = View.GONE
            }
            .start()
    }

    private fun updateCurrentFolderDisplay() {
        tvCurrentFolder.text = "Current folder: ${currentGameDir.absolutePath}"
    }

    private fun checkPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                loadGameList()
            } else {
                requestStoragePermission()
            }
        } else {
            val permissions = arrayOf(
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            )
            
            val allGranted = permissions.all { 
                ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED 
            }
            
            if (allGranted) {
                loadGameList()
            } else {
                requestPermissionLauncher.launch(permissions)
            }
        }
    }

    private fun requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                intent.addCategory("android.intent.category.DEFAULT")
                intent.data = Uri.parse("package:$packageName")
                startActivity(intent)
            } catch (e: Exception) {
                val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                startActivity(intent)
            }
        }
    }

    private fun selectGameFolder() {
        try {
            val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            intent.addFlags(
                Intent.FLAG_GRANT_READ_URI_PERMISSION or
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            )
            
            pickFolderLauncher.launch(intent)
        } catch (e: Exception) {
            Toast.makeText(this, "Cannot open folder selector: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun getPathFromUri(uri: Uri): String? {
        return try {
            when {
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q -> {
                    val documentFile = DocumentFile.fromTreeUri(this, uri)
                    documentFile?.uri?.path?.let { extractRealPath(it) }
                }
                else -> {
                    uri.path?.let { extractRealPath(it) }
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }

    private fun extractRealPath(path: String): String {
        var realPath = path
        
        // إزالة البادئات الشائعة
        val prefixes = listOf(
            "/tree/primary:",
            "/tree/",
            "/document/primary:"
        )
        
        prefixes.forEach { prefix ->
            if (realPath.contains(prefix)) {
                val colonIndex = realPath.indexOf(":")
                if (colonIndex != -1) {
                    realPath = realPath.substring(colonIndex + 1)
                }
            }
        }
        
        // إضافة المسار الأساسي إذا لزم الأمر
        if (!realPath.startsWith("/")) {
            realPath = "/$realPath"
        }
        
        // التأكد من المسار الصحيح
        val externalStorage = Environment.getExternalStorageDirectory().absolutePath
        return if (realPath.startsWith(externalStorage)) {
            realPath
        } else {
            "$externalStorage$realPath"
        }.replace("//", "/")
    }

    private fun loadGameList() {
        val gameFolders = findGameFolders(currentGameDir)
        
        if (gameFolders.isEmpty()) {
            showEmptyLibraryMessage()
            return
        }
        
        // ترتيب المجلدات أبجدياً
        val sortedFolders = gameFolders.sortedBy { it.name.lowercase() }
        
        // إنشاء الـ Adapter
        val adapter = GameAdapter(sortedFolders) { gameFolder ->
            startGame(gameFolder)
        }
        
        recyclerViewGames.adapter = adapter
        layoutEmpty.visibility = View.GONE
        recyclerViewGames.visibility = View.VISIBLE
        
        tvTitle.text = "Xbox Games (${sortedFolders.size})"
    }

    private fun findGameFolders(rootDir: File): List<File> {
        val gameFolders = mutableListOf<File>()
        
        // التحقق من وجود المجلد
        if (!rootDir.exists() || !rootDir.isDirectory) {
            showToast("Directory not found: ${rootDir.absolutePath}")
            return gameFolders
        }
        
        // مسح المجلدات
        val files = rootDir.listFiles() ?: return gameFolders
        
        for (file in files) {
            if (file.isDirectory && !file.name.startsWith(".")) {
                // التحقق من وجود ملفات XBE
                if (hasXbeFile(file)) {
                    gameFolders.add(file)
                } else {
                    // البحث في المستوى الثاني
                    file.listFiles()?.forEach { subFile ->
                        if (subFile.isDirectory && hasXbeFile(subFile)) {
                            gameFolders.add(subFile)
                        }
                    }
                }
            }
        }
        
        return gameFolders
    }

    private fun hasXbeFile(folder: File): Boolean {
        val files = folder.listFiles() ?: return false
        return files.any { file ->
            file.isFile && (
                file.name.equals("default.xbe", ignoreCase = true) ||
                file.name.lowercase().endsWith(".xbe")
            )
        }
    }

    private fun showEmptyLibraryMessage() {
        layoutEmpty.visibility = View.VISIBLE
        recyclerViewGames.visibility = View.GONE
        
        tvHelp.text = "No .xbe files found in:\n${currentGameDir.absolutePath}\n\n" +
                     "• Ensure game folders contain .xbe files\n" +
                     "• Try selecting a different folder"
    }

    private fun showToast(message: String) {
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
    }
    
    private fun showAboutDialog() {
        val dialog = android.app.AlertDialog.Builder(this)
            .setTitle("About Xemu")
            .setMessage(
                "Xemu Xbox Emulator v0.8\n\n" +
                "Developed by: Xemu Team\n" +
                "License: GPL v2\n\n" +
                "This is an experimental Xbox emulator for Android.\n" +
                "Not all games are compatible yet."
            )
            .setPositiveButton("OK") { dialog, _ ->
                dialog.dismiss()
            }
            .create()
        
        dialog.show()
    }

    private fun startGame(gameFolder: File) {
        // الانتقال إلى شاشة المحاكاة
        val intent = Intent(this, EmulatorActivity::class.java)
        intent.putExtra("GAME_PATH", gameFolder.absolutePath)
        intent.putExtra("GAME_NAME", gameFolder.name)
        startActivity(intent)
        
        // تأثير الانتقال
        overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out)
        
        // إغلاق القائمة الجانبية إذا كانت مفتوحة
        closeSideMenu()
    }

    override fun onBackPressed() {
        if (sideMenuLayout.visibility == View.VISIBLE) {
            closeSideMenu()
        } else {
            super.onBackPressed()
        }
    }

    /* ===============================
       Game Adapter
       =============================== */

    inner class GameAdapter(
        private val gameFolders: List<File>,
        private val onGameClick: (File) -> Unit
    ) : RecyclerView.Adapter<GameAdapter.GameViewHolder>() {

        inner class GameViewHolder(view: View) : RecyclerView.ViewHolder(view) {
            val gameItem: LinearLayout = view.findViewById(R.id.gameItem)
            val tvGameName: TextView = view.findViewById(R.id.tvGameName)
            val tvGamePath: TextView = view.findViewById(R.id.tvGamePath)
            val tvGameFiles: TextView = view.findViewById(R.id.tvGameFiles)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameViewHolder {
            val view = LayoutInflater.from(parent.context)
                .inflate(R.layout.item_game, parent, false)
            return GameViewHolder(view)
        }

        override fun onBindViewHolder(holder: GameViewHolder, position: Int) {
            val gameFolder = gameFolders[position]
            
            // عرض اسم اللعبة
            holder.tvGameName.text = gameFolder.name
            holder.tvGamePath.text = gameFolder.parentFile?.name ?: "Root"
            
            // عد الملفات
            val files = gameFolder.listFiles() ?: arrayOf()
            val fileCount = files.size
            val xbeCount = files.count { 
                it.isFile && it.name.lowercase().endsWith(".xbe")
            }
            
            holder.tvGameFiles.text = "$xbeCount XBE file(s), $fileCount total"
            
            // حدث الضغط
            holder.gameItem.setOnClickListener {
                onGameClick(gameFolder)
            }
            
            // تأثير الضغط
            holder.gameItem.setOnLongClickListener {
                showToast("Launching ${gameFolder.name}...")
                true
            }
        }

        override fun getItemCount(): Int = gameFolders.size
    }
}
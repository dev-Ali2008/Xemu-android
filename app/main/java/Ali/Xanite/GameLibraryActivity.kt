package Ali.Xanite

import android.content.res.Configuration
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.net.Uri
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.method.LinkMovementMethod
import android.text.style.ClickableSpan
import android.view.LayoutInflater
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.Space
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.documentfile.provider.DocumentFile
import coil.load
import android.view.animation.DecelerateInterpolator
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.ArrayDeque
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

class GameLibraryActivity : BaseActivity() {
  companion object {
    const val EXTRA_RESTART_LAST_GAME = "Ali.Xanite.extra.RESTART_LAST_GAME"
    private const val SNAPSHOT_PREVIEW_HEADER_SIZE = 12
    private const val TOTAL_SNAPSHOT_SLOTS = 10
    private const val XDVDFS_SECTOR_SIZE = 2048L
    private const val XDVDFS_VOLUME_DESCRIPTOR_OFFSET = 32L * XDVDFS_SECTOR_SIZE
    private const val XDVDFS_VOLUME_DESCRIPTOR_SIZE = 0x800
    private const val XDVDFS_TRAILING_MAGIC_OFFSET = 0x7EC
    private const val XDVDFS_XGD1_OFFSET = 405_798_912L
    private const val XDVDFS_XGD2_OFFSET = 265_879_552L
    private const val XDVDFS_XGD3_OFFSET = 34_078_720L
    private val XDVDFS_VOLUME_MAGIC = "MICROSOFT*XBOX*MEDIA".toByteArray(Charsets.US_ASCII)
    private val XDVDFS_IMAGE_OFFSETS =
      longArrayOf(0L, XDVDFS_XGD1_OFFSET, XDVDFS_XGD2_OFFSET, XDVDFS_XGD3_OFFSET)
  }

  private enum class DiscImageFormat {
    OTHER,
    XISO,
    REGULAR_ISO,
    UNKNOWN_ISO,
  }

  private enum class GameContextAction {
    PER_GAME_SETTINGS,
    ADD_SHORTCUT,
    SET_CUSTOM_COVER,
    REMOVE_CUSTOM_COVER,
    DELETE_GAME,
    ABOUT,
  }

  private data class GameEntry(
    val title: String,
    val uri: Uri,
    val relativePath: String,
    val sizeBytes: Long,
    val discImageFormat: DiscImageFormat,
  )

  private data class CoverEntry(
    val collapsed: String,
    val tokens: Set<String>,
    val numericTokens: Set<String>,
    val url: String
  )

  private data class SnapshotSlotPreview(
    val slot: Int,
    val slotLabel: String,
    val gameTitle: String,
    val thumbnail: Bitmap?,
  )

  private val prefs by lazy { getSharedPreferences("xaniteog_prefs", MODE_PRIVATE) }
  private val gameExts = setOf("iso", "xiso", "cso", "cci")
  private val titleStopWords = setOf("the", "a", "an", "and", "of", "for", "in", "on", "to")
  private val coverRepoBaseUrl = "https://raw.githubusercontent.com/izzy2lost/X1_Covers/main/"
  private val boxArtCache = ConcurrentHashMap<String, String>()
  private val boxArtMisses = ConcurrentHashMap.newKeySet<String>()
  private val persistedCoverWrites = ConcurrentHashMap.newKeySet<String>()
  private val coverIndex = ConcurrentHashMap<String, String>()
  private val coverCollapsedIndex = ConcurrentHashMap<String, String>()
  private val coverEntries = ArrayList<CoverEntry>()
  private val coverIndexLock = Any()
  private val discFormatCacheLock = Any()
  private val discFormatCache = HashMap<String, DiscImageFormat>()
  private val coverPersistenceExecutor = Executors.newSingleThreadExecutor()
  @Volatile private var coverIndexLoaded = false
  @Volatile private var discFormatCacheLoaded = false
  @Volatile private var discFormatCacheDirty = false

  private lateinit var loadingSpinner: ProgressBar
  private lateinit var convertProgressBar: ProgressBar
  private lateinit var loadingText: TextView
  private lateinit var emptyText: TextView
  private lateinit var gamesListContainer: LinearLayout
  private lateinit var gamesGridContainer: LinearLayout
  private lateinit var gamesListScroll: View
  private lateinit var gamesGridScroll: View

  private lateinit var btnConvertIso: MaterialButton
  private lateinit var btnViewList: MaterialButton
  private lateinit var btnViewGrid: MaterialButton

  private lateinit var detailPanel: View
  private lateinit var detailTitle: TextView
  private lateinit var detailSaves: TextView
  private lateinit var detailContent: TextView
  private lateinit var detailBlocks: TextView
  private lateinit var selectedIcon: GlowingCircleView
  private lateinit var statsText: TextView
  private lateinit var ymenuOverlay: View
  private lateinit var ymenuPanel: View
  private lateinit var ymenuItems: LinearLayout
  private lateinit var btnBBack: View
  private lateinit var btnASelect: View
  private var selectedGame: GameEntry? = null
  private var isYMenuOpen = false
  private var ymenuActions = mutableListOf<() -> Unit>()
  private var ymenuSelectedIndex = 0

  private var gamesFolderUri: Uri? = null
  private var scanGeneration = 0
  private var currentGames: List<GameEntry> = emptyList()
  private var useCoverGrid = false
  private var boxArtLookupEnabled = true
  @Volatile private var isConvertingIso = false

  private val pickGamesFolder =
    registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
      if (uri != null) {
        persistUriPermission(uri)
        gamesFolderUri = uri
        prefs.edit().putString("gamesFolderUri", uri.toString()).apply()
        boxArtCache.clear()
        boxArtMisses.clear()
        loadGames()
      }
    }

  private var pendingCustomCoverGame: GameEntry? = null
  private val pickCustomCover =
    registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
      val game = pendingCustomCoverGame ?: return@registerForActivityResult
      pendingCustomCoverGame = null
      if (uri != null) {
        saveCustomCover(game, uri)
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_game_library)
    EdgeToEdgeHelper.enable(this)
    EdgeToEdgeHelper.applySystemBarPadding(findViewById(R.id.library_scroll))
    enableFullScreen()

    if (tryRestartLastGameFromIntent()) {
      return
    }

    loadingSpinner = findViewById(R.id.library_loading)
    convertProgressBar = findViewById(R.id.library_convert_progress)
    loadingText = findViewById(R.id.library_loading_text)
    emptyText = findViewById(R.id.library_empty_text)
    gamesListContainer = findViewById(R.id.library_games_container)
    gamesGridContainer = findViewById(R.id.library_games_grid_container)
    gamesListScroll = findViewById(R.id.library_list_scroll)
    gamesGridScroll = findViewById(R.id.library_grid_scroll)

    btnConvertIso = findViewById(R.id.btn_convert_iso)
    btnViewList = findViewById(R.id.btn_view_list)
    btnViewGrid = findViewById(R.id.btn_view_grid)
    btnViewList.isCheckable = true
    btnViewGrid.isCheckable = true

    detailPanel = findViewById(R.id.library_detail_panel)
    detailTitle = findViewById(R.id.library_detail_title)
    detailSaves = findViewById(R.id.library_detail_saves)
    detailContent = findViewById(R.id.library_detail_content)
    detailBlocks = findViewById(R.id.library_detail_blocks)
    selectedIcon = findViewById(R.id.library_selected_icon)
    statsText = findViewById(R.id.library_stats_text)
    ymenuOverlay = findViewById(R.id.ymenu_overlay)
    ymenuPanel = findViewById(R.id.ymenu_panel)
    ymenuItems = findViewById(R.id.ymenu_items)
    btnBBack = findViewById(R.id.btn_library_b_back)
    btnASelect = findViewById(R.id.btn_library_a_select)
    adaptLayoutForOrientation()
    findViewById<View>(R.id.ymenu_scrim).setOnClickListener { hideYMenu() }

    btnBBack.setOnClickListener { onBButton() }
    btnASelect.setOnClickListener { onAButton() }

    gamesFolderUri = prefs.getString("gamesFolderUri", null)?.let(Uri::parse)
    useCoverGrid = prefs.getBoolean("library_cover_grid", false)
    boxArtLookupEnabled = BuildConfig.REMOTE_COVER_ART_ENABLED
    prefs.edit().putBoolean("library_box_art_lookup", boxArtLookupEnabled).apply()

    syncDisplayModeUi()


    btnConvertIso.setOnClickListener {
      showIsoConversionPicker()
    }
    btnViewList.setOnClickListener { setDisplayMode(false) }
    btnViewGrid.setOnClickListener { setDisplayMode(true) }
    updateConvertButtonState()

    if (!isFolderReady(gamesFolderUri)) {
      Toast.makeText(this, getString(R.string.setup_pick_disc), Toast.LENGTH_SHORT).show()
      pickGamesFolder.launch(gamesFolderUri)
      return
    }

    loadGames()
  }

  override fun onResume() {
    super.onResume()
    checkForUnseenCrash()
    checkForUnseenSessionIssue()
    CrashReportManager.pruneOldSessionLogs(this)
  }

  override fun onKeyDown(keyCode: Int, event: android.view.KeyEvent): Boolean {
    return when (keyCode) {
      android.view.KeyEvent.KEYCODE_BUTTON_Y -> {
        if (!isYMenuOpen) showYMenu() else hideYMenu()
        true
      }
      android.view.KeyEvent.KEYCODE_BUTTON_B -> {
        onBButton()
        true
      }
      android.view.KeyEvent.KEYCODE_BUTTON_A -> {
        onAButton()
        true
      }
      android.view.KeyEvent.KEYCODE_DPAD_UP -> {
        if (isYMenuOpen) {
          navigateYMenu(-1)
        } else {
          navigateList(-1)
        }
        true
      }
      android.view.KeyEvent.KEYCODE_DPAD_DOWN -> {
        if (isYMenuOpen) {
          navigateYMenu(1)
        } else {
          navigateList(1)
        }
        true
      }
      android.view.KeyEvent.KEYCODE_DPAD_LEFT -> {
        clearSelection()
        true
      }
      else -> super.onKeyDown(keyCode, event)
    }
  }

  private fun navigateYMenu(direction: Int) {
    val count = ymenuItems.childCount
    if (count == 0) return
    (ymenuItems.getChildAt(ymenuSelectedIndex) as? GameBladeView)?.bladeSelected = false
    ymenuSelectedIndex = (ymenuSelectedIndex + direction + count) % count
    (ymenuItems.getChildAt(ymenuSelectedIndex) as? GameBladeView)?.bladeSelected = true
  }

  private fun navigateList(direction: Int) {
    val count = gamesListContainer.childCount
    if (count == 0) return
    val currentIndex = currentGames.indexOfFirst { it.title == selectedGame?.title }
    val newIndex = (currentIndex + direction + count) % count
    if (newIndex in currentGames.indices) {
      selectGame(currentGames[newIndex])
    }
  }

  /**
   * Runs every time this screen becomes visible — including right after
   * MainActivity (the running game, in the separate :xaniteog process) dies,
   * since Android returns here automatically. XaniteApplication's JVM crash
   * handler already copies Java-level crashes to the clipboard the instant
   * they happen; this is the catch-all for everything that handler can't
   * see — most importantly native (signal-level) crashes in the emulator
   * core, which don't go through the JVM at all.
   */
  private fun checkForUnseenCrash() {
    if (!CrashReportManager.hasUnseenCrash(this)) return
    val copied = CrashReportManager.copyLatestCrashToClipboard(this)
    CrashReportManager.markCrashSeen(this)
    if (copied) {
      Toast.makeText(this, R.string.library_crash_detected_toast, Toast.LENGTH_LONG).show()
    }
  }

  /**
   * Covers the non-crash failure modes: the game ran without ever crashing
   * but never actually booted, went unstable mid-session, or hit repeated
   * rendering errors. xaniteog_android.cpp writes a session log for every game
   * session and flags these specific conditions with WARN markers —
   * hasUnseenSessionIssue() only returns true when one of those markers is
   * actually present, so a normal healthy session (which also produces a
   * log) never triggers this.
   */
  private fun checkForUnseenSessionIssue() {
    if (!CrashReportManager.isActiveDevLogsEnabled(this)) return
    if (!CrashReportManager.hasUnseenSessionIssue(this)) return
    val copied = CrashReportManager.copyLatestSessionLogToClipboard(this)
    CrashReportManager.markSessionIssueSeen(this)
    if (copied) {
      Toast.makeText(this, R.string.library_session_issue_detected_toast, Toast.LENGTH_LONG).show()
    }
  }

  override fun onDestroy() {
    super.onDestroy()
    coverPersistenceExecutor.shutdownNow()
  }

  private fun tryRestartLastGameFromIntent(): Boolean {
    if (!intent.getBooleanExtra(EXTRA_RESTART_LAST_GAME, false)) {
      return false
    }

    val dvdUri = prefs.getString("dvdUri", null)?.let(Uri::parse)
    val dvdPath = prefs.getString("dvdPath", null)
    val hasDvd = when {
      dvdUri != null -> hasPersistedReadPermission(dvdUri)
      dvdPath != null -> File(dvdPath).isFile
      else -> false
    }
    if (!hasDvd) {
      Toast.makeText(this, getString(R.string.library_restart_failed), Toast.LENGTH_SHORT).show()
      return false
    }

    prefs.edit()
      .putBoolean("skip_game_picker", false)
      .apply()

    launchMainActivityForRestart()
    return true
  }

  private fun launchMainActivityForRestart() {
    startActivity(Intent(this, MainActivity::class.java))
    finish()
  }

  private fun resolveConfiguredLocalHddFile(): File? {
    val path = prefs.getString("hddPath", null) ?: return null
    val file = File(path)
    return file.takeIf { it.isFile }
  }

  private fun slotName(slot: Int) = "android_slot_$slot"

  private fun snapshotPreviewDir(): File = File(filesDir, "xaniteog/snapshots")

  private fun snapshotPreviewDirs(): List<File> {
    val dirs = ArrayList<File>(2)
    dirs.add(snapshotPreviewDir())
    getExternalFilesDir(null)?.let { dirs.add(File(it, "xaniteog/snapshots")) }
    return dirs.distinctBy { it.absolutePath }
  }

  private fun slotNameAliases(slot: Int): List<String> {
    val aliases = linkedSetOf(
      slotName(slot),
      "slot_$slot",
      "slot$slot",
      "snapshot_$slot",
    )
    return aliases.toList()
  }

  private fun resolveSnapshotPreviewFile(slot: Int, extension: String): File? {
    for (dir in snapshotPreviewDirs()) {
      for (name in slotNameAliases(slot)) {
        val file = File(dir, "$name.$extension")
        if (file.isFile) {
          return file
        }
      }
    }
    return null
  }

  private fun extractDisplayName(rawName: String?): String? {
    if (rawName.isNullOrBlank()) {
      return null
    }
    val decoded = Uri.decode(rawName)
    val leaf = decoded.substringAfterLast('/').substringAfterLast(':')
    if (leaf.isBlank()) {
      return null
    }
    val stem = leaf.substringBeforeLast('.', leaf).trim()
    return stem.takeIf { it.isNotEmpty() }
  }

  private fun fallbackCurrentGameName(): String {
    val pathName = extractDisplayName(prefs.getString("dvdPath", null)?.let { File(it).name })
    if (!pathName.isNullOrEmpty()) {
      return pathName
    }
    val uriName = extractDisplayName(prefs.getString("dvdUri", null))
    if (!uriName.isNullOrEmpty()) {
      return uriName
    }
    return getString(R.string.snapshot_unknown_game)
  }

  private fun readSnapshotGameTitle(slot: Int): String {
    val title = runCatching {
      val file = resolveSnapshotPreviewFile(slot, "title")
      if (file != null && file.exists()) {
        file.readText(Charsets.UTF_8).trim()
      } else {
        ""
      }
    }.getOrDefault("")

    if (title.isNotEmpty()) {
      return title
    }

    return if (resolveSnapshotPreviewFile(slot, "thm") != null) {
      fallbackCurrentGameName()
    } else {
      getString(R.string.snapshot_empty_slot)
    }
  }

  private fun decodeSnapshotThumbnail(slot: Int): Bitmap? {
    val sourceFile = resolveSnapshotPreviewFile(slot, "thm") ?: return null
    val bytes = runCatching { sourceFile.readBytes() }.getOrNull() ?: return null
    if (bytes.size < SNAPSHOT_PREVIEW_HEADER_SIZE) {
      return null
    }

    if (bytes[0] != 'X'.code.toByte() ||
      bytes[1] != '1'.code.toByte() ||
      bytes[2] != 'T'.code.toByte() ||
      bytes[3] != 'H'.code.toByte()) {
      return null
    }

    val header = ByteBuffer.wrap(bytes, 4, 8).order(ByteOrder.LITTLE_ENDIAN)
    val version = header.short.toInt() and 0xFFFF
    val width = header.short.toInt() and 0xFFFF
    val height = header.short.toInt() and 0xFFFF
    val channels = header.short.toInt() and 0xFFFF

    if (version != 1 || channels != 4 || width <= 0 || height <= 0) {
      return null
    }

    val pixelBytesLong = width.toLong() * height.toLong() * channels.toLong()
    if (pixelBytesLong <= 0 || pixelBytesLong > Int.MAX_VALUE) {
      return null
    }

    val pixelBytes = pixelBytesLong.toInt()
    if (bytes.size < SNAPSHOT_PREVIEW_HEADER_SIZE + pixelBytes) {
      return null
    }

    val pixels = IntArray(width * height)
    var src = SNAPSHOT_PREVIEW_HEADER_SIZE
    for (y in 0 until height) {
      val dstRow = (height - 1 - y) * width
      for (x in 0 until width) {
        val r = bytes[src].toInt() and 0xFF
        val g = bytes[src + 1].toInt() and 0xFF
        val b = bytes[src + 2].toInt() and 0xFF
        pixels[dstRow + x] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
        src += 4
      }
    }

    return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
  }

  private fun loadSnapshotSlotPreviews(): List<SnapshotSlotPreview> {
    return (1..TOTAL_SNAPSHOT_SLOTS).map { slot ->
      SnapshotSlotPreview(
        slot = slot,
        slotLabel = getString(R.string.snapshot_slot_label, slot),
        gameTitle = readSnapshotGameTitle(slot),
        thumbnail = decodeSnapshotThumbnail(slot),
      )
    }
  }

  private fun showSnapshotPreviewDialog(preview: SnapshotSlotPreview) {
    val bitmap = preview.thumbnail ?: return
    val image = ImageView(this).apply {
      setImageBitmap(bitmap)
      adjustViewBounds = true
      scaleType = ImageView.ScaleType.FIT_CENTER
      setPadding(16, 16, 16, 16)
    }

    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(getString(R.string.snapshot_preview_title, preview.slot, preview.gameTitle))
      .setView(image)
      .setPositiveButton(android.R.string.ok, null)
      .show()
  }

  private fun launchMainActivityWithSnapshot(slot: Int) {
    val intent = Intent(this, MainActivity::class.java).apply {
      putExtra(MainActivity.EXTRA_AUTO_LOAD_SNAPSHOT_SLOT, slot)
    }
    startActivity(intent)
    finish()
  }

  private fun showSnapshotStartupPicker() {
    val previews = loadSnapshotSlotPreviews()
    val listView = ListView(this)
    lateinit var dialog: androidx.appcompat.app.AlertDialog

    val adapter = object : BaseAdapter() {
      override fun getCount(): Int = previews.size
      override fun getItem(position: Int): SnapshotSlotPreview = previews[position]
      override fun getItemId(position: Int): Long = previews[position].slot.toLong()

      override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val view = convertView ?: layoutInflater.inflate(R.layout.item_snapshot_slot, parent, false)
        val preview = getItem(position)

        val slotLabel = view.findViewById<TextView>(R.id.snapshot_slot_label)
        val gameTitle = view.findViewById<TextView>(R.id.snapshot_game_title)
        val previewHint = view.findViewById<TextView>(R.id.snapshot_preview_hint)
        val thumbnail = view.findViewById<ImageView>(R.id.snapshot_thumbnail)

        slotLabel.text = preview.slotLabel
        gameTitle.text = preview.gameTitle

        if (preview.thumbnail != null) {
          thumbnail.setImageBitmap(preview.thumbnail)
          previewHint.text = getString(R.string.snapshot_preview_tap_hint)
          previewHint.visibility = View.VISIBLE
          thumbnail.setOnClickListener {
            showSnapshotPreviewDialog(preview)
          }
        } else {
          thumbnail.setImageResource(R.drawable.ic_xaniteog_image_placeholder)
          previewHint.text = getString(R.string.snapshot_preview_unavailable)
          previewHint.visibility = View.VISIBLE
          thumbnail.setOnClickListener(null)
        }

        return view
      }
    }

    listView.adapter = adapter
    listView.setOnItemClickListener { _, _, position, _ ->
      val slot = previews[position].slot
      dialog.dismiss()
      launchMainActivityWithSnapshot(slot)
    }

    dialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.snapshot_select_load_slot)
      .setView(listView)
      .setNegativeButton(android.R.string.cancel, null)
      .create()

    dialog.show()
  }

  private fun loadGames() {
    val folderUri = gamesFolderUri
    if (!isFolderReady(folderUri)) {
      setLoading(false)
      currentGames = emptyList()
      renderGames()
      return
    }
    val readyFolderUri = folderUri ?: return

    setLoading(true, getString(R.string.library_loading_games))

    val generation = ++scanGeneration
    Thread {
      loadDiscFormatCacheIfNeeded()
      val games = scanFolderForGames(readyFolderUri)
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

  private fun syncDisplayModeUi() {
    btnViewList.isChecked = !useCoverGrid
    btnViewGrid.isChecked = useCoverGrid
    gamesListScroll.visibility = if (useCoverGrid) View.GONE else View.VISIBLE
    gamesGridScroll.visibility = if (useCoverGrid) View.VISIBLE else View.GONE
  }

  private fun setDisplayMode(nextGrid: Boolean) {
    if (nextGrid == useCoverGrid) {
      return
    }
    useCoverGrid = nextGrid
    prefs.edit().putBoolean("library_cover_grid", useCoverGrid).apply()
    syncDisplayModeUi()
    renderGames()
  }

  private fun setLoading(loading: Boolean, message: String? = null) {
    if (message != null) {
      loadingText.text = message
    }
    loadingSpinner.visibility = if (loading) View.VISIBLE else View.GONE
    loadingText.visibility = if (loading) View.VISIBLE else View.GONE
  }

  private fun renderGames() {
    val games = currentGames
    syncDisplayModeUi()
    updateConvertButtonState(games)
    updateStats(games)
    gamesListContainer.removeAllViews()
    gamesGridContainer.removeAllViews()
    emptyText.visibility = if (games.isEmpty()) View.VISIBLE else View.GONE
    if (games.isEmpty()) {
      return
    }

    if (useCoverGrid) {
      renderCoverGrid(games)
    } else {
      renderList(games)
    }
  }

  private fun updateConvertButtonState(games: List<GameEntry> = currentGames) {
    val hasConvertible = XisoConverterNative.isAvailable() &&
      !isConvertingIso &&
      games.any { game -> isConvertibleIso(game) }
    btnConvertIso.visibility = if (hasConvertible) View.VISIBLE else View.GONE
  }

  private fun updateStats(games: List<GameEntry>) {
    val total = games.size
    val available = games.size
    statsText.text = getString(R.string.library_stats, total, available)
  }

  private fun countSavesForGame(title: String): Int {
    return (1..TOTAL_SNAPSHOT_SLOTS).count { slot ->
      val t = readSnapshotGameTitle(slot)
      t.isNotEmpty() && t == title
    }
  }

  private fun selectGame(game: GameEntry) {
    selectedGame = game

    for (i in 0 until gamesListContainer.childCount) {
      val child = gamesListContainer.getChildAt(i)
      if (child is GameBladeView) {
        child.bladeSelected = child.gameTitle == game.title
      }
    }

    detailPanel.visibility = View.VISIBLE
    detailTitle.text = game.title
    val saves = countSavesForGame(game.title)
    detailSaves.text = "$saves save${if (saves != 1) "s" else ""}"
    detailContent.text = "0 content items"
    detailBlocks.text = "${game.sizeBytes / 1024L / 1024L} MB"
    selectedIcon.fillColor = android.graphics.Color.parseColor("#1A170D")
    bindCoverArt(selectedIcon, game)

    detailPanel.alpha = 0f
    detailPanel.animate()
      .alpha(1f)
      .setDuration(200)
      .start()
  }

  private fun clearSelection() {
    selectedGame = null
    for (i in 0 until gamesListContainer.childCount) {
      val child = gamesListContainer.getChildAt(i)
      if (child is GameBladeView) {
        child.bladeSelected = false
      }
    }
    detailPanel.visibility = View.GONE
  }

  private fun onBButton() {
    if (isYMenuOpen) {
      hideYMenu()
    } else if (selectedGame != null) {
      clearSelection()
    } else {
      finish()
    }
  }

  private fun onAButton() {
    if (isYMenuOpen) {
      val selectedYAction = getSelectedYMenuAction()
      if (selectedYAction != null) {
        selectedYAction.invoke()
      }
      hideYMenu()
    } else {
      selectedGame?.let { launchGame(it) }
    }
  }

  private fun showYMenu() {
    if (selectedGame == null) return
    isYMenuOpen = true
    ymenuSelectedIndex = 0
    ymenuActions.clear()
    ymenuItems.removeAllViews()

    val game = selectedGame!!

    data class YMenuItem(val label: String, val action: () -> Unit)

    val items = buildList<YMenuItem> {
      add(YMenuItem(getString(R.string.library_per_game_settings_option)) {
        startActivity(
          Intent(this@GameLibraryActivity, PerGameSettingsActivity::class.java)
            .putExtra(PerGameSettingsActivity.EXTRA_GAME_TITLE, game.title)
            .putExtra(PerGameSettingsActivity.EXTRA_GAME_RELATIVE_PATH, game.relativePath)
        )
      })
      add(YMenuItem("Load Save") {
        showSnapshotStartupPicker()
      })
      add(YMenuItem(getString(R.string.library_add_shortcut_option)) {
        createGameShortcut(game)
      })
      add(YMenuItem(getString(R.string.library_custom_cover_set_option)) {
        pendingCustomCoverGame = game
        pickCustomCover.launch("image/*")
      })
      val hasCustom = getCustomCoverFile(game).exists()
      if (hasCustom) {
        add(YMenuItem(getString(R.string.library_custom_cover_remove_option)) {
          removeCustomCover(game)
        })
      }
      add(YMenuItem(getString(R.string.library_delete_option)) {
        confirmDeleteGame(game)
      })
      add(YMenuItem(getString(R.string.library_about_button_desc)) {
        showAboutDialog()
      })
    }

    ymenuActions = items.map { it.action }.toMutableList()

    items.forEachIndexed { i, item ->
      val blade = GameBladeView(this).apply {
        useMenuLabelStyle()
        gameTitle = item.label
        bladeSelected = i == 0
        layoutParams = LinearLayout.LayoutParams(
          LinearLayout.LayoutParams.MATCH_PARENT,
          LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply { bottomMargin = dp(4) }
        setOnClickListener {
          item.action()
          hideYMenu()
        }
      }
      ymenuItems.addView(blade)
    }

    ymenuOverlay.visibility = View.VISIBLE
    ymenuOverlay.bringToFront()
    ymenuPanel.translationX = -280f
    ymenuPanel.animate()
      .translationX(0f)
      .setDuration(250)
      .setInterpolator(DecelerateInterpolator())
      .start()
  }

  private fun adaptLayoutForOrientation() {
    if (resources.configuration.orientation != Configuration.ORIENTATION_PORTRAIT) {
      return
    }

    val mainContent = findViewById<LinearLayout>(R.id.library_main_content)
    val libraryPanel = findViewById<View>(R.id.library_scroll)
    val detailContentLayout =
      findViewById<LinearLayout>(R.id.library_selected_detail_content_layout)
    val infoPanel = findViewById<View>(R.id.library_info_panel)
    val density = resources.displayMetrics.density
    fun dp(value: Int) = (value * density).toInt()

    mainContent.orientation = LinearLayout.VERTICAL
    libraryPanel.layoutParams = LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      0,
      1f,
    ).apply {
      setMargins(dp(10), dp(10), dp(10), dp(4))
    }
    detailPanel.layoutParams = LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      dp(176),
    ).apply {
      setMargins(dp(10), dp(4), dp(10), dp(8))
    }

    detailContentLayout.orientation = LinearLayout.HORIZONTAL
    detailContentLayout.gravity = Gravity.CENTER_VERTICAL
    findViewById<View>(R.id.library_detail_spacer_before).visibility = View.GONE
    findViewById<View>(R.id.library_detail_spacer_after).visibility = View.GONE
    selectedIcon.layoutParams = LinearLayout.LayoutParams(dp(84), dp(84))
    infoPanel.layoutParams = LinearLayout.LayoutParams(
      0,
      ViewGroup.LayoutParams.WRAP_CONTENT,
      1f,
    ).apply {
      marginStart = dp(12)
    }
  }

  private fun hideYMenu() {
    isYMenuOpen = false
    ymenuPanel.animate()
      .translationX(-280f)
      .setDuration(200)
      .setInterpolator(DecelerateInterpolator())
      .withEndAction {
        ymenuOverlay.visibility = View.GONE
      }
      .start()
  }

  private fun getSelectedYMenuAction(): (() -> Unit)? {
    return ymenuActions.getOrNull(ymenuSelectedIndex)
  }

  private fun renderList(games: List<GameEntry>) {
    for ((index, game) in games.withIndex()) {
      val blade = GameBladeView(this).apply {
        gameTitle = game.title
        subtitle = buildGameSizeText(game).toString()
        layoutParams = LinearLayout.LayoutParams(
          LinearLayout.LayoutParams.MATCH_PARENT,
          LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply { bottomMargin = dp(6) }
        setOnClickListener {
          if (selectedGame == game) {
            launchGame(game)
          } else {
            selectGame(game)
          }
        }
        setOnLongClickListener { showGameContextMenu(game); true }
      }

      bindCoverArt(blade.coverImage, game)

      if (game.discImageFormat == DiscImageFormat.REGULAR_ISO) {
        blade.subtitle = "${buildGameSizeText(game)}  ! ${getString(R.string.library_regular_iso_badge)}"
      }

      gamesListContainer.addView(blade)

      blade.alpha = 0f
      blade.translationX = 120f
      blade.animate()
        .alpha(1f)
        .translationX(0f)
        .setStartDelay((index * 40).toLong())
        .setDuration(350)
        .setInterpolator(DecelerateInterpolator())
        .start()
    }
  }

  private fun renderCoverGrid(games: List<GameEntry>) {
    val inflater = LayoutInflater.from(this)
    var row: LinearLayout? = null
    val columns = resolveCoverGridColumns()
    val spacingPx = dp(8)
    val halfSpacingPx = spacingPx / 2

    for ((index, game) in games.withIndex()) {
      val columnIndex = index % columns
      if (columnIndex == 0) {
        row = LinearLayout(this).apply {
          orientation = LinearLayout.HORIZONTAL
        }
        val rowLp = LinearLayout.LayoutParams(
          LinearLayout.LayoutParams.MATCH_PARENT,
          LinearLayout.LayoutParams.WRAP_CONTENT
        )
        if (index >= columns) {
          rowLp.topMargin = dp(12)
        }
        gamesGridContainer.addView(row, rowLp)
      }

      val item = inflater.inflate(R.layout.item_game_cover, row, false)
      val itemLp = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
      itemLp.marginStart = if (columnIndex == 0) 0 else halfSpacingPx
      itemLp.marginEnd = if (columnIndex == columns - 1) 0 else halfSpacingPx
      row!!.addView(item, itemLp)

      val nameText = item.findViewById<TextView>(R.id.game_cover_name_text)
      val convertWarningText = item.findViewById<TextView>(R.id.game_cover_convert_warning_text)
      val coverImage = item.findViewById<ImageView>(R.id.game_cover_image)

      nameText.text = game.title
      bindConvertibleWarning(convertWarningText, game)
      item.setOnClickListener { launchGame(game) }
      item.setOnLongClickListener { showGameContextMenu(game); true }
      bindCoverArt(coverImage, game)

      item.alpha = 0f
      item.scaleX = 0.92f
      item.scaleY = 0.92f
      item.animate()
        .alpha(1f)
        .scaleX(1f)
        .scaleY(1f)
        .setStartDelay((index * 30).toLong())
        .setDuration(350)
        .setInterpolator(DecelerateInterpolator())
        .start()
    }

    val remainder = games.size % columns
    if (remainder != 0) {
      for (columnIndex in remainder until columns) {
        val filler = Space(this)
        val fillerLp = LinearLayout.LayoutParams(0, 0, 1f)
        fillerLp.marginStart = if (columnIndex == 0) 0 else halfSpacingPx
        fillerLp.marginEnd = if (columnIndex == columns - 1) 0 else halfSpacingPx
        row?.addView(filler, fillerLp)
      }
    }
  }

  private fun resolveCoverGridColumns(): Int {
    val widthDp = resources.configuration.screenWidthDp
    val suggested = (widthDp / 180).coerceIn(2, 4)
    return if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
      maxOf(3, suggested)
    } else {
      suggested
    }
  }

  private fun bindCoverArt(coverView: ImageView, game: GameEntry) {
    coverView.tag = game.uri.toString()
    showCoverPlaceholder(coverView)

    val customFile = getCustomCoverFile(game)
    if (customFile.exists()) {
      loadCoverArtIntoView(coverView, customFile)
      return
    }

    val key = normalizeCoverKey(game.title)
    val persistedFile = getPersistedCoverFile(key)
    if (persistedFile.exists() && persistedFile.length() > 0L) {
      loadCoverArtIntoView(coverView, persistedFile)
      return
    }

    if (!boxArtLookupEnabled) {
      return
    }

    val cachedUrl = boxArtCache[key]
    if (cachedUrl != null) {
      applyBoxArtToView(coverView, key, cachedUrl)
      return
    }

    val url = lookupBoxArtUrl(game.title) ?: return
    boxArtCache[key] = url
    if (coverView.tag == game.uri.toString()) {
      applyBoxArtToView(coverView, key, url)
    }
  }

  private fun applyBoxArtToView(coverView: ImageView, key: String, url: String) {
    persistDownloadedCoverAsync(key, url)
    loadCoverArtIntoView(coverView, url)
  }

  private fun loadCoverArtIntoView(coverView: ImageView, model: Any) {
    coverView.load(model) {
      crossfade(true)
      placeholder(R.drawable.ic_xaniteog_cover_unknown)
      error(R.drawable.ic_xaniteog_cover_unknown)
      listener(
        onSuccess = { _, _ ->
          if (coverView.tag != null) {
            coverView.background = null
          }
        },
        onError = { _, _ ->
          showCoverPlaceholder(coverView)
        }
      )
    }
  }

  private fun showCoverPlaceholder(coverView: ImageView) {
    if (coverView.id == R.id.library_selected_icon) {
      coverView.background = null
      coverView.setImageResource(R.drawable.ic_xaniteog_cover_unknown)
      return
    }
    val backgroundRes = if (coverView.id == R.id.game_list_cover_image) {
      R.drawable.game_list_cover_background
    } else {
      R.drawable.game_cover_image_background
    }
    coverView.setBackgroundResource(backgroundRes)
    coverView.setImageResource(R.drawable.ic_xaniteog_cover_unknown)
  }

  private fun persistedCoversDir(): File = File(filesDir, "downloaded_covers")

  private fun getPersistedCoverFile(coverKey: String): File {
    val collapsed = collapseCoverKey(coverKey)
    val fileName = if (collapsed.isNotEmpty()) {
      collapsed
    } else {
      "cover_" + Integer.toHexString(coverKey.hashCode())
    }
    return File(persistedCoversDir(), "$fileName.png")
  }

  private fun persistDownloadedCoverAsync(coverKey: String, url: String) {
    val target = getPersistedCoverFile(coverKey)
    if ((target.exists() && target.length() > 0L) || !persistedCoverWrites.add(coverKey)) {
      return
    }

    try {
      coverPersistenceExecutor.execute {
        var connection: HttpURLConnection? = null
        try {
          val dir = persistedCoversDir()
          if (!dir.exists() && !dir.mkdirs()) {
            return@execute
          }

          val tempFile = File(dir, "${target.name}.tmp")
          connection = URL(url).openConnection() as? HttpURLConnection
          connection?.connectTimeout = 5000
          connection?.readTimeout = 10000
          connection?.instanceFollowRedirects = true
          connection?.connect()
          val source = connection?.inputStream ?: URL(url).openStream()
          source.use { input ->
            FileOutputStream(tempFile).use { output ->
              input.copyTo(output)
            }
          }
          if (tempFile.length() <= 0L) {
            tempFile.delete()
            return@execute
          }
          if (target.exists()) {
            target.delete()
          }
          if (!tempFile.renameTo(target)) {
            FileInputStream(tempFile).use { input ->
              FileOutputStream(target).use { output ->
                input.copyTo(output)
              }
            }
            tempFile.delete()
          }
        } catch (_: IOException) {
        } finally {
          connection?.disconnect()
          persistedCoverWrites.remove(coverKey)
        }
      }
    } catch (_: RuntimeException) {
      persistedCoverWrites.remove(coverKey)
    }
  }

  private fun lookupBoxArtUrl(title: String): String? {
    ensureCoverIndexLoaded()
    val candidates = linkedSetOf<String>()
    val cleanTitle = normalizeLookupTitle(title)
    val normalizedTitle = normalizeCoverKey(cleanTitle)
    if (normalizedTitle.isBlank()) {
      return null
    }
    if (boxArtMisses.contains(normalizedTitle)) {
      return null
    }
    addCoverLookupCandidates(candidates, title)
    addCoverLookupCandidates(candidates, cleanTitle)
    addCoverLookupCandidates(candidates, cleanTitle.replace(":", ""))
    addCoverLookupCandidates(candidates, cleanTitle.substringBefore(" - ").trim())
    addCoverLookupCandidates(candidates, cleanTitle.substringBefore(":").trim())

    for (key in candidates) {
      val found = coverIndex[key]
      if (!found.isNullOrBlank()) {
        return found
      }
    }

    for (key in candidates) {
      val collapsed = collapseCoverKey(key)
      if (collapsed.isBlank()) {
        continue
      }
      val found = coverCollapsedIndex[collapsed]
      if (!found.isNullOrBlank()) {
        coverIndex.putIfAbsent(key, found)
        return found
      }
    }

    val fuzzyMatch = findClosestCoverUrl(candidates)
    if (!fuzzyMatch.isNullOrBlank()) {
      for (key in candidates) {
        coverIndex.putIfAbsent(key, fuzzyMatch)
        val collapsed = collapseCoverKey(key)
        if (collapsed.isNotBlank()) {
          coverCollapsedIndex.putIfAbsent(collapsed, fuzzyMatch)
        }
      }
      return fuzzyMatch
    }

    boxArtMisses.add(normalizedTitle)
    return null
  }

  private fun addCoverLookupCandidates(out: MutableSet<String>, raw: String) {
    if (raw.isBlank()) {
      return
    }
    val normalized = normalizeCoverKey(raw)
    if (normalized.isBlank()) {
      return
    }
    out.add(normalized)
    out.add(stripTrailingRegion(normalized))
  }

  private fun ensureCoverIndexLoaded() {
    if (coverIndexLoaded) {
      return
    }
    synchronized(coverIndexLock) {
      if (coverIndexLoaded) {
        return
      }
      try {
        val lines = assets.open("X1_Covers.txt").bufferedReader().use { it.readLines() }
        val seenEntries = HashSet<String>()
        for (line in lines) {
          val fileName = line.trim()
          if (fileName.isEmpty() || !fileName.endsWith(".png", ignoreCase = true)) {
            continue
          }
          val gameName = fileName.removeSuffix(".png").trim()
          val encoded = URLEncoder.encode(fileName, "UTF-8").replace("+", "%20")
          val url = coverRepoBaseUrl + encoded

          val exactKey = normalizeCoverKey(gameName)
          val strippedKey = stripTrailingRegion(exactKey)
          if (exactKey.isNotEmpty()) {
            coverIndex.putIfAbsent(exactKey, url)
          }
          if (strippedKey.isNotEmpty()) {
            coverIndex.putIfAbsent(strippedKey, url)
          }
          val canonical = if (strippedKey.isNotEmpty()) strippedKey else exactKey
          val collapsed = collapseCoverKey(canonical)
          if (collapsed.isNotEmpty()) {
            coverCollapsedIndex.putIfAbsent(collapsed, url)
          }
          if (canonical.isNotEmpty() && seenEntries.add("$canonical|$url")) {
            val tokens = tokenizeCoverKey(canonical)
            coverEntries.add(
              CoverEntry(
                collapsed = collapsed,
                tokens = tokens,
                numericTokens = tokens.filterTo(HashSet()) { token -> token.any(Char::isDigit) },
                url = url
              )
            )
          }
        }
      } catch (_: Exception) {
        // Keep empty index; grid will show placeholders if the asset is unavailable.
      }
      coverIndexLoaded = true
    }
  }

  private fun normalizeLookupTitle(input: String): String {
    var title = input.trim()
    title = title.replace('_', ' ')
    title = title.replace(Regex("\\[[^\\]]*\\]"), " ")
    title = title.replace(Regex("\\([^\\)]*\\)"), " ")
    title = title.replace(Regex("\\s+"), " ").trim()
    return title
  }

  private fun normalizeCoverKey(input: String): String {
    var title = input.lowercase(Locale.ROOT).trim()
    title = title.replace('_', ' ')
    title = title.replace('\u2019', '\'')
    title = title.replace("’", "'")
    title = title.replace(Regex("\\s+"), " ")
    return title
  }

  private fun stripTrailingRegion(input: String): String {
    return input.replace(Regex("\\s*\\([^\\)]*\\)\\s*$"), "").trim()
  }

  private fun collapseCoverKey(input: String): String {
    return normalizeCoverKey(input).replace(Regex("[^a-z0-9]+"), "")
  }

  private fun tokenizeCoverKey(input: String): Set<String> {
    return normalizeCoverKey(input)
      .replace(Regex("[^a-z0-9]+"), " ")
      .split(' ')
      .asSequence()
      .map { it.trim() }
      .filter { it.length >= 2 }
      .filter { it !in titleStopWords }
      .toSet()
  }

  private fun findClosestCoverUrl(candidates: Set<String>): String? {
    var bestUrl: String? = null
    var bestScore = 0
    for (candidate in candidates) {
      val collapsed = collapseCoverKey(candidate)
      val tokens = tokenizeCoverKey(candidate)
      if (collapsed.isBlank() || tokens.isEmpty()) {
        continue
      }
      val numericTokens = tokens.filterTo(HashSet()) { token -> token.any(Char::isDigit) }
      for (entry in coverEntries) {
        val score = scoreCoverMatch(collapsed, tokens, numericTokens, entry)
        if (score > bestScore) {
          bestScore = score
          bestUrl = entry.url
        }
      }
    }
    return if (bestScore >= 55) bestUrl else null
  }

  private fun scoreCoverMatch(
    candidateCollapsed: String,
    candidateTokens: Set<String>,
    candidateNumericTokens: Set<String>,
    entry: CoverEntry
  ): Int {
    if (candidateCollapsed == entry.collapsed) {
      return 100
    }

    if (candidateNumericTokens.isNotEmpty() &&
      entry.numericTokens.isNotEmpty() &&
      candidateNumericTokens != entry.numericTokens
    ) {
      return 0
    }

    val overlapCount = candidateTokens.count { token -> entry.tokens.contains(token) }
    if (overlapCount == 0) {
      return 0
    }

    val maxTokenCount = maxOf(candidateTokens.size, entry.tokens.size)
    var score = (overlapCount * 70) / maxTokenCount

    if (candidateCollapsed.contains(entry.collapsed) || entry.collapsed.contains(candidateCollapsed)) {
      score += 20
    }

    val lengthDelta = kotlin.math.abs(candidateCollapsed.length - entry.collapsed.length)
    if (lengthDelta <= 4) {
      score += 10
    } else if (lengthDelta <= 10) {
      score += 5
    }

    return score
  }

  private fun showIsoConversionPicker() {
    if (isConvertingIso) {
      Toast.makeText(this, getString(R.string.library_convert_busy), Toast.LENGTH_SHORT).show()
      return
    }

    val isoGames = currentGames.filter { game -> isConvertibleIso(game) }
    if (isoGames.isEmpty()) {
      Toast.makeText(this, getString(R.string.library_convert_none), Toast.LENGTH_SHORT).show()
      return
    }

    val labels = isoGames.map { game ->
      "${game.title}\n${game.relativePath}"
    }.toTypedArray()

    val dp = resources.displayMetrics.density
    lateinit var convertDialog: androidx.appcompat.app.AlertDialog

    val buttonList = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding((20 * dp).toInt(), (12 * dp).toInt(), (20 * dp).toInt(), 0)
      labels.forEachIndexed { i, label ->
        addView(MaterialButton(this@GameLibraryActivity, null,
          com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
          text = label
          isAllCaps = false
          layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
          ).also { lp -> lp.bottomMargin = (8 * dp).toInt() }
          setOnClickListener {
            convertDialog.dismiss()
            confirmIsoConversion(isoGames[i])
          }
        })
      }
    }

    convertDialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.library_convert_pick_title)
      .setView(buttonList)
      .setNegativeButton(android.R.string.cancel, null)
      .create()
    convertDialog.show()
  }

  private fun confirmIsoConversion(game: GameEntry) {
    val folderUri = gamesFolderUri
    if (folderUri == null) {
      Toast.makeText(this, getString(R.string.library_no_folder), Toast.LENGTH_SHORT).show()
      return
    }
    if (!hasPersistedWritePermission(folderUri)) {
      Toast.makeText(
        this,
        getString(R.string.library_convert_write_permission),
        Toast.LENGTH_LONG
      ).show()
      pickGamesFolder.launch(folderUri)
      return
    }

    val root = DocumentFile.fromTreeUri(this, folderUri)
    val parent = root?.let { resolveParentDirectory(it, game.relativePath) }
    if (parent == null) {
      Toast.makeText(this, getString(R.string.library_convert_resolve_failed), Toast.LENGTH_LONG).show()
      return
    }

    val sourceName = game.relativePath.substringAfterLast('/')
    val outputName = buildXisoFileName(sourceName)
    val existing = parent.findFile(outputName)
    if (existing != null && existing.isDirectory) {
      Toast.makeText(this, getString(R.string.library_convert_create_output_failed), Toast.LENGTH_LONG).show()
      return
    }

    MaterialAlertDialogBuilder(this)
      .setTitle(R.string.library_convert_confirm_title)
      .setMessage(getString(R.string.library_convert_confirm_message, outputName))
      .setPositiveButton(R.string.library_convert_action) { _, _ ->
        if (existing != null) {
          MaterialAlertDialogBuilder(this)
            .setTitle(R.string.library_convert_overwrite_title)
            .setMessage(getString(R.string.library_convert_overwrite_message, outputName))
            .setPositiveButton(R.string.library_convert_action) { _, _ ->
              startIsoConversion(game, outputName, overwrite = true)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
        } else {
          startIsoConversion(game, outputName, overwrite = false)
        }
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun startIsoConversion(game: GameEntry, outputName: String, overwrite: Boolean) {
    if (isConvertingIso) {
      Toast.makeText(this, getString(R.string.library_convert_busy), Toast.LENGTH_SHORT).show()
      return
    }
    if (!XisoConverterNative.isAvailable()) {
      Toast.makeText(this, getString(R.string.library_convert_unavailable), Toast.LENGTH_LONG).show()
      return
    }

    isConvertingIso = true
    updateConvertButtonState()
    convertProgressBar.progress = 0
    convertProgressBar.visibility = View.VISIBLE
    loadingText.text = getString(R.string.library_converting_game_percent, game.title, 0)
    loadingText.visibility = View.VISIBLE

    val onProgress = { percent: Int ->
      runOnUiThread {
        convertProgressBar.progress = percent
        loadingText.text = getString(R.string.library_converting_game_percent, game.title, percent)
      }
    }

    Thread {
      val error = convertIsoToXisoInFolder(game, outputName, overwrite, onProgress)
      runOnUiThread {
        isConvertingIso = false
        convertProgressBar.visibility = View.GONE
        convertProgressBar.progress = 0
        setLoading(false, getString(R.string.library_loading_games))
        updateConvertButtonState()
        if (error == null) {
          Toast.makeText(
            this,
            getString(R.string.library_convert_success, outputName),
            Toast.LENGTH_LONG
          ).show()
          loadGames()
        } else {
          Toast.makeText(
            this,
            getString(R.string.library_convert_failed, error),
            Toast.LENGTH_LONG
          ).show()
        }
      }
    }.start()
  }

  private fun convertIsoToXisoInFolder(
    game: GameEntry,
    outputName: String,
    overwrite: Boolean,
    onProgress: ((Int) -> Unit)? = null
  ): String? {
    val folderUri = gamesFolderUri ?: return getString(R.string.library_no_folder)
    val root = DocumentFile.fromTreeUri(this, folderUri)
      ?: return getString(R.string.library_convert_resolve_failed)
    val parent = resolveParentDirectory(root, game.relativePath)
      ?: return getString(R.string.library_convert_resolve_failed)

    val existing = parent.findFile(outputName)
    if (existing != null) {
      if (!overwrite) {
        return getString(R.string.library_convert_overwrite_message, outputName)
      }
      if (!existing.delete()) {
        return getString(R.string.library_convert_create_output_failed)
      }
    }

    val outputDoc = parent.createFile("application/octet-stream", outputName)
      ?: return getString(R.string.library_convert_create_output_failed)

    val stageDir = File(getExternalFilesDir(null) ?: filesDir, "xiso-convert")
    if (!stageDir.exists() && !stageDir.mkdirs()) {
      outputDoc.delete()
      return getString(R.string.library_convert_create_output_failed)
    }

    val token = System.currentTimeMillis()
    val inputTemp = File(stageDir, "input-$token.iso")
    val outputTemp = File(stageDir, "output-$token.xiso.iso")
    var success = false
    try {
      // Phase 1 (0-40%): copy input from SAF to local temp
      if (!copyUriToFile(game.uri, inputTemp, game.sizeBytes) { copyPercent ->
        onProgress?.invoke((copyPercent * 40) / 100)
      }) {
        return getString(R.string.library_convert_copy_input_failed)
      }
      onProgress?.invoke(40)

      // Phase 2 (40-90%): native XISO conversion
      val nativeProgressCallback = if (onProgress != null) {
        XisoProgressCallback { current, total ->
          val convertPercent = if (total > 0) (current * 100) / total else 0
          onProgress(40 + (convertPercent * 50) / 100)
        }
      } else null

      val nativeError = if (nativeProgressCallback != null) {
        XisoConverterNative.convertIsoToXiso(inputTemp.absolutePath, outputTemp.absolutePath, nativeProgressCallback)
      } else {
        XisoConverterNative.convertIsoToXiso(inputTemp.absolutePath, outputTemp.absolutePath)
      }
      // Free input temp space before output copy
      inputTemp.delete()

      if (!nativeError.isNullOrBlank()) {
        return nativeError
      }
      onProgress?.invoke(90)

      if (!outputTemp.exists() || outputTemp.length() <= 0L) {
        return "Converted image was empty"
      }

      // Phase 3 (90-100%): copy converted output back to SAF
      if (!copyFileToUri(outputTemp, outputDoc.uri) { copyPercent ->
        onProgress?.invoke(90 + (copyPercent * 10) / 100)
      }) {
        return getString(R.string.library_convert_copy_output_failed)
      }
      onProgress?.invoke(100)
      success = true
      return null
    } finally {
      if (!success) {
        outputDoc.delete()
      }
      inputTemp.delete()
      outputTemp.delete()
    }
  }

  private fun resolveParentDirectory(root: DocumentFile, relativePath: String): DocumentFile? {
    var dir = root
    val parts = relativePath
      .split('/')
      .map { part -> part.trim() }
      .filter { part -> part.isNotEmpty() }
    if (parts.size <= 1) {
      return dir
    }
    for (index in 0 until (parts.size - 1)) {
      val child = dir.findFile(parts[index]) ?: return null
      if (!child.isDirectory) {
        return null
      }
      dir = child
    }
    return dir
  }

  private fun copyUriToFile(
    uri: Uri,
    target: File,
    totalSize: Long = -1L,
    onProgress: ((Int) -> Unit)? = null,
  ): Boolean {
    return try {
      val input = contentResolver.openInputStream(uri) ?: return false
      input.use { stream ->
        FileOutputStream(target).use { output ->
          copyWithProgress(stream, output, totalSize, onProgress)
        }
      }
      true
    } catch (_: IOException) {
      false
    }
  }

  private fun copyFileToUri(
    source: File,
    targetUri: Uri,
    onProgress: ((Int) -> Unit)? = null,
  ): Boolean {
    return try {
      val totalSize = source.length()
      val output = contentResolver.openOutputStream(targetUri, "w") ?: return false
      FileInputStream(source).use { input ->
        output.use { stream ->
          copyWithProgress(input, stream, totalSize, onProgress)
        }
      }
      true
    } catch (_: IOException) {
      false
    }
  }

  private fun copyWithProgress(
    input: InputStream,
    output: OutputStream,
    totalSize: Long,
    onProgress: ((Int) -> Unit)?,
  ) {
    val buffer = ByteArray(128 * 1024)
    var bytesCopied = 0L
    var lastPercent = -1
    while (true) {
      val bytes = input.read(buffer)
      if (bytes < 0) break
      output.write(buffer, 0, bytes)
      bytesCopied += bytes
      if (totalSize > 0 && onProgress != null) {
        val percent = ((bytesCopied * 100) / totalSize).toInt().coerceAtMost(100)
        if (percent != lastPercent) {
          lastPercent = percent
          onProgress(percent)
        }
      }
    }
  }

  private fun bindConvertibleWarning(view: TextView, game: GameEntry) {
    view.visibility = if (game.discImageFormat == DiscImageFormat.REGULAR_ISO) {
      View.VISIBLE
    } else {
      View.GONE
    }
  }

  private fun buildGameSizeText(game: GameEntry): CharSequence {
    return getString(R.string.library_game_size, formatSize(game.sizeBytes))
  }

  private fun isConvertibleIso(game: GameEntry): Boolean {
    return game.discImageFormat == DiscImageFormat.REGULAR_ISO
  }

  private fun buildXisoFileName(sourceName: String): String {
    val lower = sourceName.lowercase(Locale.ROOT)
    val stem = if (lower.endsWith(".iso")) sourceName.dropLast(4) else sourceName
    return "$stem.xiso.iso"
  }

  override fun finish() {
    super.finish()
    overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
  }

  private fun launchGame(game: GameEntry) {
    persistUriPermission(game.uri)
    // Shared with home-screen shortcuts so both paths apply the same per-game
    // overrides and flush them synchronously - MainActivity runs in :xaniteog
    // and reads SharedPreferences during startup.
    GameLauncher.launch(this, game.uri, game.relativePath)
    finish()
  }

  /** Offers to pin a game to the home screen, using its cover art as the icon. */
  private fun createGameShortcut(game: GameEntry) {
    if (!ShortcutManagerCompat.isRequestPinShortcutSupported(this)) {
      Toast.makeText(this, R.string.shortcut_unsupported, Toast.LENGTH_LONG).show()
      return
    }

    persistUriPermission(game.uri)

    val intent = Intent(this, ShortcutLaunchActivity::class.java).apply {
      action = Intent.ACTION_VIEW
      putExtra(ShortcutLaunchActivity.EXTRA_GAME_URI, game.uri.toString())
      putExtra(ShortcutLaunchActivity.EXTRA_GAME_RELATIVE_PATH, game.relativePath)
    }

    val icon = shortcutIconFor(game)
    val builder = ShortcutInfoCompat.Builder(this, "game_" + game.relativePath.hashCode())
      .setShortLabel(shortcutLabelFor(game.title))
      .setLongLabel(game.title.take(48))
      .setIntent(intent)
    if (icon != null) {
      builder.setIcon(icon)
    } else {
      builder.setIcon(IconCompat.createWithResource(this, R.mipmap.ic_launcher))
    }

    val shortcut = builder.build()
    // Refreshes an already-pinned shortcut with the same id, so re-adding a
    // game repairs its icon/label in place rather than needing a manual
    // remove-and-re-add.
    runCatching { ShortcutManagerCompat.updateShortcuts(this, listOf(shortcut)) }
    ShortcutManagerCompat.requestPinShortcut(this, shortcut, null)
  }

  /**
   * Cover art as a shortcut icon, falling back to null if none is usable.
   *
   * Box art is portrait, but adaptive icons require a square bitmap - handing
   * the raw cover to createWithAdaptiveBitmap() stretched it to fill the
   * square, which is why shortcuts looked squashed. Draw it letterboxed and
   * centred on a square canvas at its real aspect ratio instead, kept inside
   * the safe zone so the launcher's circular/squircle mask doesn't crop it.
   */
  private fun shortcutIconFor(game: GameEntry): IconCompat? {
    val file = sequenceOf(
      getCustomCoverFile(game),
      getPersistedCoverFile(normalizeCoverKey(game.title)),
    ).firstOrNull { it.exists() && it.length() > 0 } ?: return null

    return try {
      val src = BitmapFactory.decodeFile(file.absolutePath) ?: return null
      if (src.width <= 0 || src.height <= 0) return null

      val size = 512
      val out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
      val canvas = Canvas(out)
      canvas.drawColor(Color.parseColor("#0D0B08"))

      // Adaptive icons reserve the outer third for masking/parallax, so only
      // the centre ~66% is guaranteed visible. Fit the art inside that.
      val safe = size * 0.62f
      val scale = minOf(safe / src.width, safe / src.height)
      val w = src.width * scale
      val h = src.height * scale
      val left = (size - w) / 2f
      val top = (size - h) / 2f
      canvas.drawBitmap(
        src,
        null,
        RectF(left, top, left + w, top + h),
        Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG),
      )
      src.recycle()

      IconCompat.createWithAdaptiveBitmap(out)
    } catch (t: Throwable) {
      DebugLog.w("GameLibraryActivity") { "shortcut icon decode failed for ${game.title}: $t" }
      null
    }
  }

  /**
   * Launchers only show ~10-12 characters, so "Crash Bandicoot - The Wrath of
   * Cortex" became "Crash Bandic...". Drop the subtitle after " - " so the
   * part that identifies the game survives.
   */
  private fun shortcutLabelFor(title: String): String =
    title.substringBefore(" - ").trim().ifEmpty { title }

  private fun scanFolderForGames(folderUri: Uri): List<GameEntry> {
    val root = DocumentFile.fromTreeUri(this, folderUri) ?: return emptyList()
    val stack = ArrayDeque<Pair<DocumentFile, String>>()
    stack.add(root to "")

    val games = ArrayList<GameEntry>()
    val seenDiscFormatKeys = HashSet<String>()
    while (stack.isNotEmpty()) {
      val (node, prefix) = stack.removeLast()
      val files = try {
        node.listFiles()
      } catch (_: Exception) {
        emptyArray()
      }
      for (child in files) {
        val name = child.name ?: continue
        if (child.isDirectory) {
          stack.add(child to (prefix + name + "/"))
          continue
        }
        if (!child.isFile || !isSupportedGame(name)) {
          continue
        }
        val sizeBytes = child.length()
        games.add(
          GameEntry(
            title = toGameTitle(name),
            uri = child.uri,
            relativePath = prefix + name,
            sizeBytes = sizeBytes,
            discImageFormat = resolveDiscImageFormat(child.uri, name, sizeBytes, seenDiscFormatKeys),
          )
        )
      }
    }

    games.sortBy { it.title.lowercase(Locale.ROOT) }
    persistDiscFormatCache(seenDiscFormatKeys)
    return games
  }

  private fun discFormatCacheFile(): File = File(filesDir, "disc_format_cache.tsv")

  private fun loadDiscFormatCacheIfNeeded() {
    if (discFormatCacheLoaded) {
      return
    }
    synchronized(discFormatCacheLock) {
      if (discFormatCacheLoaded) {
        return
      }
      val cacheFile = discFormatCacheFile()
      if (cacheFile.isFile) {
        runCatching {
          cacheFile.forEachLine(Charsets.UTF_8) { line ->
            val split = line.lastIndexOf('\t')
            if (split <= 0 || split >= line.lastIndex) {
              return@forEachLine
            }
            val key = line.substring(0, split)
            val formatName = line.substring(split + 1)
            runCatching { DiscImageFormat.valueOf(formatName) }
              .getOrNull()
              ?.let { format -> discFormatCache[key] = format }
          }
        }
      }
      discFormatCacheLoaded = true
    }
  }

  private fun persistDiscFormatCache(seenKeys: Set<String>) {
    synchronized(discFormatCacheLock) {
      if (!discFormatCacheLoaded) {
        return
      }

      var changed = discFormatCacheDirty
      val iterator = discFormatCache.keys.iterator()
      while (iterator.hasNext()) {
        if (iterator.next() !in seenKeys) {
          iterator.remove()
          changed = true
        }
      }

      if (!changed) {
        return
      }

      val cacheFile = discFormatCacheFile()
      if (discFormatCache.isEmpty()) {
        cacheFile.delete()
        discFormatCacheDirty = false
        return
      }

      runCatching {
        cacheFile.parentFile?.mkdirs()
        cacheFile.bufferedWriter(Charsets.UTF_8).use { writer ->
          discFormatCache.entries.forEach { (key, format) ->
            writer.append(key)
            writer.append('\t')
            writer.append(format.name)
            writer.append('\n')
          }
        }
        discFormatCacheDirty = false
      }
    }
  }

  private fun discFormatCacheKey(uri: Uri, sizeBytes: Long): String =
    uri.toString() + "\t" + sizeBytes

  private fun resolveDiscImageFormat(
    uri: Uri,
    fileName: String,
    sizeBytes: Long,
    seenKeys: MutableSet<String>,
  ): DiscImageFormat {
    val lower = fileName.lowercase(Locale.ROOT)
    if (!isDiscImageFormatDetectable(lower)) {
      return DiscImageFormat.OTHER
    }

    val cacheKey = discFormatCacheKey(uri, sizeBytes)
    seenKeys.add(cacheKey)

    synchronized(discFormatCacheLock) {
      discFormatCache[cacheKey]?.let { cached ->
        return cached
      }
    }

    val detected = detectDiscImageFormat(uri, fileName)
    synchronized(discFormatCacheLock) {
      if (discFormatCache[cacheKey] != detected) {
        discFormatCache[cacheKey] = detected
        discFormatCacheDirty = true
      }
    }
    return detected
  }

  private fun detectDiscImageFormat(uri: Uri, fileName: String): DiscImageFormat {
    val lower = fileName.lowercase(Locale.ROOT)
    if (!isDiscImageFormatDetectable(lower)) {
      return DiscImageFormat.OTHER
    }

    return try {
      contentResolver.openFileDescriptor(uri, "r")?.use { descriptor ->
        readDiscImageFormat(descriptor)
      } ?: DiscImageFormat.UNKNOWN_ISO
    } catch (_: Exception) {
      DiscImageFormat.UNKNOWN_ISO
    }
  }

  private fun isDiscImageFormatDetectable(lowerFileName: String): Boolean {
    return lowerFileName.endsWith(".iso") || lowerFileName.endsWith(".xiso")
  }

  private fun readDiscImageFormat(descriptor: ParcelFileDescriptor): DiscImageFormat {
    FileInputStream(descriptor.fileDescriptor).use { input ->
      val channel = input.channel
      val imageSize = channel.size()
      val header = ByteBuffer.allocate(XDVDFS_VOLUME_DESCRIPTOR_SIZE.toInt())

      for (offset in XDVDFS_IMAGE_OFFSETS) {
        val descriptorOffset = offset + XDVDFS_VOLUME_DESCRIPTOR_OFFSET
        if (imageSize < descriptorOffset + XDVDFS_VOLUME_DESCRIPTOR_SIZE) {
          continue
        }

        header.clear()
        channel.position(descriptorOffset)

        while (header.hasRemaining()) {
          val read = channel.read(header)
          if (read <= 0) {
            break
          }
        }

        if (header.hasRemaining()) {
          continue
        }

        if (isValidXdvdfsVolumeDescriptor(header.array())) {
          return if (offset == 0L) DiscImageFormat.XISO else DiscImageFormat.REGULAR_ISO
        }
      }
    }

    return DiscImageFormat.UNKNOWN_ISO
  }

  private fun isValidXdvdfsVolumeDescriptor(header: ByteArray): Boolean {
    return matchesXdvdfsMagic(header, 0) &&
      matchesXdvdfsMagic(header, XDVDFS_TRAILING_MAGIC_OFFSET)
  }

  private fun matchesXdvdfsMagic(buffer: ByteArray, startIndex: Int): Boolean {
    if (buffer.size < startIndex + XDVDFS_VOLUME_MAGIC.size) {
      return false
    }

    for (index in XDVDFS_VOLUME_MAGIC.indices) {
      if (buffer[startIndex + index] != XDVDFS_VOLUME_MAGIC[index]) {
        return false
      }
    }

    return true
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
    val rawTitle = when {
      lower.endsWith(".xiso.iso") -> fileName.dropLast(".xiso.iso".length)
      fileName.contains('.') -> fileName.substringBeforeLast('.')
      else -> fileName
    }
    val cleanedTitle = rawTitle
      .replace(Regex("(\\s*(\\([^\\)]*\\)|\\[[^\\]]*\\]))+$"), "")
      .replace(Regex("\\s+"), " ")
      .trim()
    return cleanedTitle.ifEmpty { rawTitle.trim() }
  }

  private fun formatSize(bytes: Long): String {
    if (bytes <= 0L) {
      return "Unknown"
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
    if (uri == null || !hasPersistedReadPermission(uri)) {
      return false
    }
    val root = DocumentFile.fromTreeUri(this, uri) ?: return false
    return root.exists() && root.isDirectory
  }

  private fun persistUriPermission(uri: Uri) {
    val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
    try {
      contentResolver.takePersistableUriPermission(uri, flags)
    } catch (_: SecurityException) {
    }
  }

  private fun hasPersistedReadPermission(uri: Uri): Boolean {
    return contentResolver.persistedUriPermissions.any { perm ->
      perm.uri == uri && perm.isReadPermission
    }
  }

  private fun hasPersistedWritePermission(uri: Uri): Boolean {
    return contentResolver.persistedUriPermissions.any { perm ->
      perm.uri == uri && perm.isWritePermission
    }
  }

  private fun dp(value: Int): Int {
    return (value * resources.displayMetrics.density).toInt()
  }

  private fun showAboutDialog() {
    val links = listOf(
      getString(R.string.library_about_link_discord) to getString(R.string.library_about_url_discord),
      getString(R.string.library_about_link_telegram) to getString(R.string.library_about_url_telegram),
      getString(R.string.library_about_link_github) to getString(R.string.library_about_url_github),
      getString(R.string.library_about_link_youtube) to getString(R.string.library_about_url_youtube),
      getString(R.string.library_about_link_patreon) to getString(R.string.library_about_url_patreon)
    )

    val message = SpannableStringBuilder().apply {
      append(getString(R.string.library_about_message))
      append("\n\n")
      links.forEachIndexed { index, pair ->
        val label = pair.first
        val url = pair.second
        append(label)
        append('\n')
        val start = length
        append(url)
        setSpan(
          object : ClickableSpan() {
            override fun onClick(widget: View) {
              openExternalLink(url)
            }
          },
          start,
          length,
          Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        if (index != links.lastIndex) {
          append("\n\n")
        }
      }
    }

    val dialog = MaterialAlertDialogBuilder(this)
      .setTitle(R.string.library_about_title)
      .setMessage(message)
      .setPositiveButton(android.R.string.ok, null)
      .show()

    dialog.findViewById<TextView>(android.R.id.message)?.apply {
      movementMethod = LinkMovementMethod.getInstance()
      linksClickable = true
    }
  }

  private fun customCoversDir(): File = File(filesDir, "custom_covers")

  private fun getCustomCoverFile(game: GameEntry): File =
    File(customCoversDir(), "${collapseCoverKey(game.title)}.png")

  private fun saveCustomCover(game: GameEntry, uri: Uri) {
    val dir = customCoversDir()
    if (!dir.exists()) dir.mkdirs()
    val target = getCustomCoverFile(game)
    try {
      val input = contentResolver.openInputStream(uri) ?: run {
        Toast.makeText(this, getString(R.string.library_custom_cover_failed), Toast.LENGTH_SHORT).show()
        return
      }
      input.use { stream ->
        FileOutputStream(target).use { output ->
          stream.copyTo(output)
        }
      }
      boxArtCache.remove(normalizeCoverKey(game.title))
      renderGames()
      Toast.makeText(this, getString(R.string.library_custom_cover_set), Toast.LENGTH_SHORT).show()
    } catch (_: IOException) {
      Toast.makeText(this, getString(R.string.library_custom_cover_failed), Toast.LENGTH_SHORT).show()
    }
  }

  private fun removeCustomCover(game: GameEntry) {
    val file = getCustomCoverFile(game)
    if (file.exists()) {
      file.delete()
      boxArtCache.remove(normalizeCoverKey(game.title))
      renderGames()
      Toast.makeText(this, getString(R.string.library_custom_cover_removed), Toast.LENGTH_SHORT).show()
    }
  }

  private fun confirmDeleteGame(game: GameEntry) {
    val folderUri = gamesFolderUri
    if (folderUri == null) {
      Toast.makeText(this, getString(R.string.library_no_folder), Toast.LENGTH_SHORT).show()
      return
    }
    if (!hasPersistedWritePermission(folderUri)) {
      Toast.makeText(
        this,
        getString(R.string.library_delete_write_permission),
        Toast.LENGTH_LONG
      ).show()
      pickGamesFolder.launch(folderUri)
      return
    }

    MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(R.string.library_delete_title)
      .setMessage(getString(R.string.library_delete_confirm_message, game.title))
      .setPositiveButton(R.string.library_delete_action) { _, _ ->
        deleteGame(game)
      }
      .setNegativeButton(android.R.string.cancel, null)
      .show()
  }

  private fun deleteGame(game: GameEntry) {
    if (isConvertingIso) {
      Toast.makeText(this, getString(R.string.library_convert_busy), Toast.LENGTH_SHORT).show()
      return
    }

    val folderUri = gamesFolderUri
    if (folderUri == null) {
      Toast.makeText(this, getString(R.string.library_no_folder), Toast.LENGTH_SHORT).show()
      return
    }

    val root = DocumentFile.fromTreeUri(this, folderUri)
    val parent = root?.let { resolveParentDirectory(it, game.relativePath) }
    val sourceName = game.relativePath.substringAfterLast('/')
    val target = parent?.findFile(sourceName)

    if (target == null || !target.isFile) {
      Toast.makeText(this, getString(R.string.library_delete_failed, game.title), Toast.LENGTH_LONG).show()
      loadGames()
      return
    }

    val deleted = runCatching { target.delete() }.getOrDefault(false)
    if (!deleted) {
      Toast.makeText(this, getString(R.string.library_delete_failed, game.title), Toast.LENGTH_LONG).show()
      return
    }

    val customCover = getCustomCoverFile(game)
    if (customCover.exists()) {
      customCover.delete()
    }
    PerGameSettingsManager.clearOverrides(this, game.relativePath)
    boxArtCache.remove(normalizeCoverKey(game.title))
    boxArtMisses.remove(normalizeCoverKey(game.title))

    Toast.makeText(this, getString(R.string.library_delete_success, game.title), Toast.LENGTH_SHORT).show()
    loadGames()
  }

  private fun showGameContextMenu(game: GameEntry) {
    val hasCustomCover = getCustomCoverFile(game).exists()
    val actions = buildList {
      add(GameContextAction.PER_GAME_SETTINGS)
      add(GameContextAction.ADD_SHORTCUT)
      add(GameContextAction.SET_CUSTOM_COVER)
      if (hasCustomCover) {
        add(GameContextAction.REMOVE_CUSTOM_COVER)
      }
      add(GameContextAction.DELETE_GAME)
      add(GameContextAction.ABOUT)
    }
    val options = actions.map { action ->
      when (action) {
        GameContextAction.PER_GAME_SETTINGS -> getString(R.string.library_per_game_settings_option)
        GameContextAction.ADD_SHORTCUT -> getString(R.string.library_add_shortcut_option)
        GameContextAction.SET_CUSTOM_COVER -> getString(R.string.library_custom_cover_set_option)
        GameContextAction.REMOVE_CUSTOM_COVER -> getString(R.string.library_custom_cover_remove_option)
        GameContextAction.DELETE_GAME -> getString(R.string.library_delete_option)
        GameContextAction.ABOUT -> getString(R.string.library_about_button_desc)
      }
    }.toTypedArray()

    val dp = resources.displayMetrics.density
    lateinit var contextDialog: androidx.appcompat.app.AlertDialog

    val buttonList = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding((20 * dp).toInt(), (12 * dp).toInt(), (20 * dp).toInt(), 0)
      options.forEachIndexed { i, label ->
        addView(MaterialButton(this@GameLibraryActivity, null,
          com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
          text = label
          layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
          ).also { lp -> lp.bottomMargin = (8 * dp).toInt() }
          setOnClickListener {
            contextDialog.dismiss()
            when (actions[i]) {
              GameContextAction.PER_GAME_SETTINGS -> {
                startActivity(
                  Intent(this@GameLibraryActivity, PerGameSettingsActivity::class.java)
                    .putExtra(PerGameSettingsActivity.EXTRA_GAME_TITLE, game.title)
                    .putExtra(PerGameSettingsActivity.EXTRA_GAME_RELATIVE_PATH, game.relativePath)
                )
              }
              GameContextAction.ADD_SHORTCUT -> createGameShortcut(game)
              GameContextAction.SET_CUSTOM_COVER -> {
                pendingCustomCoverGame = game
                pickCustomCover.launch("image/*")
              }
              GameContextAction.REMOVE_CUSTOM_COVER -> removeCustomCover(game)
              GameContextAction.DELETE_GAME -> confirmDeleteGame(game)
              GameContextAction.ABOUT -> showAboutDialog()
            }
          }
        })
      }
    }

    // Scrolls in landscape: the dialog is only a few hundred px tall there and
    // the lower entries (Delete Game, About XaniteOG) were clipped off with no
    // way to reach them.
    val scroller = ScrollView(this).apply {
      isFillViewport = true
      addView(buttonList)
    }

    contextDialog = MaterialAlertDialogBuilder(this, R.style.ThemeOverlay_Xaniteog_RoundedDialog)
      .setTitle(game.title)
      .setView(scroller)
      .setNegativeButton(android.R.string.cancel, null)
      .create()
    contextDialog.show()
  }

  private fun openExternalLink(url: String) {
    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url)).apply {
      addCategory(Intent.CATEGORY_BROWSABLE)
    }
    try {
      startActivity(intent)
    } catch (_: Exception) {
      Toast.makeText(this, getString(R.string.library_about_open_failed), Toast.LENGTH_SHORT).show()
    }
  }
}

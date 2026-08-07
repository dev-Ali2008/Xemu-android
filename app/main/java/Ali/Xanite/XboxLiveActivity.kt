package Ali.Xanite

import android.content.SharedPreferences
import android.content.res.ColorStateList
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.documentfile.provider.DocumentFile
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.button.MaterialButton
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.Locale

data class LobbyMember(
  val id: Int,
  val nickname: String,
  var ready: Boolean = false
)

class XboxLiveActivity : BaseActivity() {

  private lateinit var prefs: SharedPreferences
  private lateinit var emulatorPrefs: SharedPreferences
  private val EMULATOR_PREFS = "xaniteog_prefs"

  // Panels
  private lateinit var panelSystemLink: View
  private lateinit var panelGameCatalog: View
  private lateinit var panelLobbyConfig: View
  private lateinit var panelActiveLobby: View

  // Room list (main view)
  private lateinit var roomListRecycler: RecyclerView
  private lateinit var roomListStatus: TextView
  private val roomAdapter = RoomListAdapter()

  // Game catalog (Square+)
  private lateinit var catalogRecycler: RecyclerView
  private val catalogAdapter = GameListAdapter()
  private val onlineGames = mutableListOf<String>()
  private var allScannedGames = mutableListOf<String>()

  // Config
  private lateinit var configGameTitle: TextView
  private lateinit var configServerName: EditText
  private lateinit var configBtnPublic: MaterialButton
  private lateinit var configBtnPrivate: MaterialButton
  private lateinit var configPasswordSection: View
  private lateinit var configPassword: EditText
  private lateinit var configBtnOk: MaterialButton
  private lateinit var configBtnExit: MaterialButton
  private val maxMemberButtons = mutableListOf<MaterialButton>()
  private var selectedMaxMembers = 8
  private var isPrivate = false

  // Active lobby
  private lateinit var lobbyTitle: TextView
  private lateinit var lobbyGame: TextView
  private lateinit var lobbyMemberCount: TextView
  private lateinit var lobbyMembersList: LinearLayout
  private lateinit var lobbyBtnLeave: MaterialButton
  private lateinit var lobbyBtnPlay: MaterialButton
  private lateinit var lblReadyHint: TextView

  private var selectedGame: String? = null
  private val lobbyMembers = mutableListOf<LobbyMember>()
  private var amReady = false
  private var myNickname = "Player"
  private val tickHandler = Handler(Looper.getMainLooper())
  private val tickRunnable = object : Runnable {
    override fun run() {
      XblManager.tick()
      tickHandler.postDelayed(this, 250)
    }
  }

  // ── Lifecycle ──

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_xbox_live)
    enableFullScreen()

    prefs = getSharedPreferences("xbox_live_prefs", MODE_PRIVATE)
    emulatorPrefs = getSharedPreferences(EMULATOR_PREFS, MODE_PRIVATE)
    myNickname = prefs.getString("xbox_live_gamertag", null) ?: ""

    checkGamertag()
    bindViews()
    setupRoomList()
    setupGameCatalog()
    setupConfigPanel()
    setupActiveLobby()

    XblManager.addCallback(xblCallback)
    XblManager.init()
    tickHandler.postDelayed(tickRunnable, 250)

    loadOnlineGamesSet()
    scanGames()
    refreshRooms()
    // init() above has now tried a native call, so backend availability is
    // finally accurate and the status message can be trusted.
    updateRoomListStatus()
  }

  override fun onDestroy() {
    tickHandler.removeCallbacks(tickRunnable)
    XblManager.removeCallback(xblCallback)
    XblManager.leaveRoom()
    XblManager.shutdown()
    super.onDestroy()
  }

  // ── Gamertag check ──

  private fun checkGamertag() {
    if (myNickname.isNotEmpty()) return
    val input = EditText(this).apply {
      setHint(getString(R.string.syslink_gamertag_hint))
      setTextColor(0xFFFFF6EB.toInt())
      setHintTextColor(0x88FFFFFF.toInt())
      setBackgroundColor(0x1A1A1A.toInt())
      setPadding(16, 12, 16, 12)
    }
    // Uses the app's dialog theme like every other dialog in the app; this one
    // was a bare AlertDialog.Builder and looked out of place.
    com.google.android.material.dialog.MaterialAlertDialogBuilder(
      this, R.style.ThemeOverlay_Xaniteog_RoundedDialog
    )
      .setTitle(R.string.syslink_gamertag_title)
      .setMessage(R.string.syslink_gamertag_message)
      .setView(input)
      .setCancelable(false)
      .setPositiveButton(android.R.string.ok, null)
      .create()
      .apply {
        // Validate without dismissing: the old code called checkGamertag()
        // from inside the click handler, which re-entered while the first
        // dialog was still tearing down.
        setOnShowListener {
          getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            val nick = input.text.toString().trim()
            if (nick.isEmpty()) {
              Toast.makeText(
                this@XboxLiveActivity,
                R.string.syslink_gamertag_empty,
                Toast.LENGTH_SHORT,
              ).show()
            } else {
              myNickname = nick
              prefs.edit().putString("xbox_live_gamertag", nick).apply()
              dismiss()
            }
          }
        }
      }
      .show()
  }

  // ── View binding ──

  private fun bindViews() {
    panelSystemLink = findViewById(R.id.panel_system_link)
    panelGameCatalog = findViewById(R.id.panel_game_catalog)
    panelLobbyConfig = findViewById(R.id.panel_lobby_config)
    panelActiveLobby = findViewById(R.id.panel_active_lobby)

    roomListRecycler = findViewById(R.id.room_list)
    roomListRecycler.layoutManager = LinearLayoutManager(this)
    roomListRecycler.adapter = roomAdapter
    roomListStatus = findViewById(R.id.room_list_status)

    catalogRecycler = findViewById(R.id.game_catalog_list)
    catalogRecycler.layoutManager = LinearLayoutManager(this)
    catalogRecycler.adapter = catalogAdapter

    configGameTitle = findViewById(R.id.config_game_title)
    configServerName = findViewById(R.id.config_server_name)
    configBtnPublic = findViewById(R.id.config_btn_public)
    configBtnPrivate = findViewById(R.id.config_btn_private)
    configPasswordSection = findViewById(R.id.config_password_section)
    configPassword = findViewById(R.id.config_password)
    configBtnOk = findViewById(R.id.config_btn_ok)
    configBtnExit = findViewById(R.id.config_btn_exit)

    maxMemberButtons.addAll(listOf(
      findViewById(R.id.config_mem_2),
      findViewById(R.id.config_mem_4),
      findViewById(R.id.config_mem_8),
      findViewById(R.id.config_mem_16),
    ))

    lobbyTitle = findViewById(R.id.lobby_title)
    lobbyGame = findViewById(R.id.lobby_game)
    lobbyMemberCount = findViewById(R.id.lobby_member_count)
    lobbyMembersList = findViewById(R.id.lobby_members_list)
    lobbyBtnLeave = findViewById(R.id.lobby_btn_leave)
    lobbyBtnPlay = findViewById(R.id.lobby_btn_play)
    lblReadyHint = findViewById(R.id.lbl_ready_hint)
  }

  // ── Game scanning ──

  private fun loadOnlineGamesSet() {
    try {
      val reader = BufferedReader(InputStreamReader(assets.open("online_games.txt")))
      reader.useLines { lines ->
        onlineGames.addAll(lines.map { it.trim() }.filter { it.isNotEmpty() })
      }
    } catch (_: Exception) {}
  }

  private fun scanGames() {
    val uriStr = emulatorPrefs.getString("gamesFolderUri", null) ?: return
    val folderUri = Uri.parse(uriStr)
    val root = DocumentFile.fromTreeUri(this, folderUri) ?: return

    val found = mutableListOf<String>()
    val stack = ArrayDeque<DocumentFile>()
    stack.add(root)

    while (stack.isNotEmpty()) {
      val node = stack.removeLast()
      val files = try { node.listFiles() } catch (_: Exception) { emptyArray() }
      for (child in files) {
        val name = child.name ?: continue
        if (child.isDirectory) { stack.add(child); continue }
        if (!child.isFile || !isSupportedGame(name)) continue
        found.add(toGameTitle(name))
      }
    }

    found.sortBy { it.lowercase(Locale.ROOT) }
    allScannedGames = found
    /* Show only online-capable games in catalog */
    val filtered = found.filter { isOnlineGame(it) }
    catalogAdapter.submitList(filtered.ifEmpty { found })
  }

  private fun isSupportedGame(name: String): Boolean {
    val lower = name.lowercase(Locale.ROOT)
    return lower.endsWith(".iso") || lower.endsWith(".xiso") ||
           lower.endsWith(".cso") || lower.endsWith(".cci")
  }

  private fun toGameTitle(filename: String): String {
    var title = filename.substringBeforeLast(".")
    title = title.replace(Regex("""\s*\[.*?\]\s*"""), " ")
    title = title.replace(Regex("""\s*\(.*?\)\s*"""), " ").trim()
    return title
  }

  private fun isOnlineGame(title: String): Boolean {
    if (onlineGames.isEmpty()) return true
    val lower = title.lowercase(Locale.ROOT)
    return onlineGames.any { lower.contains(it.lowercase(Locale.ROOT)) }
  }

  // ── Room list (main view) ──

  private fun setupRoomList() {
    roomAdapter.onRoomClick = { room ->
      val nick = prefs.getString("xbox_live_gamertag", null) ?: "Player"
      val addr = room.hostIp.ifEmpty { "255.255.255.255" }
      val join = { pw: String? ->
        val ret = XblManager.joinRoom(addr, 27420, nick, pw)
        if (ret < 0) Toast.makeText(this, "Failed to join room", Toast.LENGTH_SHORT).show()
        else showActiveLobby(room.name, room.name)
      }
      if (room.hasPassword) {
        showPasswordDialog { pw -> join(pw) }
      } else {
        join(null)
      }
    }

    findViewById<View>(R.id.btn_a_refresh).setOnClickListener { refreshRooms() }
    findViewById<View>(R.id.btn_b_back_sl).setOnClickListener { finishWithTransition() }
  }

  private fun refreshRooms() {
    // Don't claim to be scanning when there is no backend to scan with.
    if (!XblManager.isBackendAvailable) {
      updateRoomListStatus(0)
      return
    }
    XblManager.requestRoomList("255.255.255.255", 27420)
    Toast.makeText(this, R.string.syslink_scanning, Toast.LENGTH_SHORT).show()
  }

  // ── Game catalog (Square+) ──

  private fun setupGameCatalog() {
    findViewById<View>(R.id.btn_square_plus).setOnClickListener {
      showPanel(panelGameCatalog)
    }

    findViewById<View>(R.id.btn_catalog_close).setOnClickListener {
      showPanel(panelSystemLink)
    }

    findViewById<View>(R.id.btn_b_back_catalog).setOnClickListener {
      showPanel(panelSystemLink)
    }

    /* A SELECT hint — clicking a game triggers this */
    findViewById<View>(R.id.btn_a_select_game).setOnClickListener {
      /* No-op, games are clicked directly */
    }

    catalogAdapter.onGameClick = { game ->
      selectedGame = game
      configGameTitle.text = game
      configServerName.setText(game)
      showPanel(panelLobbyConfig)
    }
  }

  // ── Config panel ──

  private fun setupConfigPanel() {
    configBtnPublic.setOnClickListener {
      isPrivate = false
      configBtnPublic.setTextColor(0xFFC6FF00.toInt())
      configBtnPublic.strokeColor = ColorStateList.valueOf(0xFFC6FF00.toInt())
      configBtnPrivate.setTextColor(0x88FFFFFF.toInt())
      configBtnPrivate.strokeColor = ColorStateList.valueOf(0x44FFFFFF.toInt())
      configPasswordSection.visibility = View.GONE
    }

    configBtnPrivate.setOnClickListener {
      isPrivate = true
      configBtnPrivate.setTextColor(0xFFC6FF00.toInt())
      configBtnPrivate.strokeColor = ColorStateList.valueOf(0xFFC6FF00.toInt())
      configBtnPublic.setTextColor(0x88FFFFFF.toInt())
      configBtnPublic.strokeColor = ColorStateList.valueOf(0x44FFFFFF.toInt())
      configPasswordSection.visibility = View.VISIBLE
    }

    val memberValues = listOf(2, 4, 8, 16)
    maxMemberButtons.forEachIndexed { i, btn ->
      btn.setOnClickListener {
        selectedMaxMembers = memberValues[i]
        maxMemberButtons.forEach { b ->
          val isSel = b == btn
          b.setTextColor(if (isSel) 0xFFC6FF00.toInt() else 0x88FFFFFF.toInt())
          b.strokeColor = ColorStateList.valueOf(if (isSel) 0xFFC6FF00.toInt() else 0x44FFFFFF.toInt())
        }
      }
    }

    configBtnOk.setOnClickListener {
      val game = selectedGame ?: return@setOnClickListener
      val serverName = configServerName.text.toString().trim()
      if (serverName.isEmpty()) {
        Toast.makeText(this, "Enter a server name", Toast.LENGTH_SHORT).show(); return@setOnClickListener
      }
      val password = if (isPrivate) configPassword.text.toString().trim() else null
      if (isPrivate && password.isNullOrEmpty()) {
        Toast.makeText(this, "Enter a password for private lobby", Toast.LENGTH_SHORT).show(); return@setOnClickListener
      }

      val ret = XblManager.hostRoom(serverName, password, selectedMaxMembers)
      if (ret < 0) {
        Toast.makeText(this, "Failed to create lobby (code $ret)", Toast.LENGTH_SHORT).show()
        return@setOnClickListener
      }
      /* Auto-join loopback */
      val nick = prefs.getString("xbox_live_gamertag", null) ?: "Player"
      XblManager.joinRoom("127.0.0.1", 27420, nick, password)

      showActiveLobby(serverName, game)
    }

    configBtnExit.setOnClickListener { showPanel(panelGameCatalog) }
  }

  // ── Active lobby with ready system ──

  private fun setupActiveLobby() {
    lobbyBtnLeave.setOnClickListener {
      XblManager.leaveRoom()
      amReady = false
      showPanel(panelSystemLink)
    }

    findViewById<View>(R.id.btn_xbox_live_close2).setOnClickListener {
      XblManager.leaveRoom()
      amReady = false
      showPanel(panelSystemLink)
    }

    findViewById<View>(R.id.btn_b_back_lobby).setOnClickListener {
      XblManager.leaveRoom()
      amReady = false
      showPanel(panelSystemLink)
    }

    /* Play/Ready button */
    lobbyBtnPlay.setOnClickListener {
      amReady = !amReady
      XblManager.sendReady(amReady)
      updateReadyButton()
    }

    findViewById<View>(R.id.btn_a_ready).setOnClickListener {
      amReady = !amReady
      XblManager.sendReady(amReady)
      updateReadyButton()
    }
  }

  private fun showActiveLobby(serverName: String, game: String) {
    lobbyTitle.text = serverName
    lobbyGame.text = "Game: $game"
    amReady = false
    lobbyMembers.clear()
    /* Restore any members already joined (from onMemberJoin callbacks) */
    for (m in XblManager.getMembers()) {
      if (lobbyMembers.none { it.id == m.first }) {
        lobbyMembers.add(LobbyMember(m.first, m.second))
      }
    }
    showPanel(panelActiveLobby)
    updateReadyButton()
    refreshMembers()
  }

  private fun updateReadyButton() {
    if (amReady) {
      lobbyBtnPlay.text = "CANCEL"
      lobbyBtnPlay.setBackgroundTintList(ColorStateList.valueOf(0xFFFF4444.toInt()))
      lblReadyHint.text = "UNREADY"
    } else {
      lobbyBtnPlay.text = "READY"
      lobbyBtnPlay.setBackgroundTintList(ColorStateList.valueOf(0xFFC6FF00.toInt()))
      lblReadyHint.text = "READY"
    }
  }

  private fun refreshMembers() {
    val count = XblManager.getMemberCount()
    lobbyMemberCount.text = "Members: $count / $selectedMaxMembers"

    lobbyMembersList.removeAllViews()
    for (member in lobbyMembers) {
      val readyIcon = if (member.ready) "●" else "○"
      val readyColor = if (member.ready) "#C6FF00" else "#666666"
      val tv = TextView(this).apply {
        text = "  $readyIcon ${member.nickname}"
        setTextColor(0xFFFFF6EB.toInt())
        textSize = 14f
        setPadding(0, 6, 0, 6)
      }
      lobbyMembersList.addView(tv)
    }
    /* Add local player if not in list */
    if (lobbyMembers.none { it.id == 0 }) {
      val readyIcon = if (amReady) "●" else "○"
      val tv = TextView(this).apply {
        text = "  $readyIcon $myNickname (you)"
        setTextColor(0xFFC6FF00.toInt())
        textSize = 14f
        setPadding(0, 6, 0, 6)
      }
      lobbyMembersList.addView(tv)
    }
  }

  private fun checkAllReady() {
    val count = XblManager.getMemberCount()
    if (count < 1) return
    /* Only start game when 2+ players AND all ready */
    if (count < 2) {
      Toast.makeText(this, "Ready! Waiting for other players...", Toast.LENGTH_SHORT).show()
      return
    }
    val allReady = lobbyMembers.all { it.ready } && amReady
    if (allReady) {
      Toast.makeText(this, "All ready! Starting game...", Toast.LENGTH_SHORT).show()
      finishWithTransition()
    }
  }

  // ── XBL callback ──

  private val xblCallback = object : XblManager.XblCallback {
    override fun onStateChanged(state: Int) {
      runOnUiThread { updateLobbyState(state) }
    }
    override fun onMemberJoined(id: Int, nickname: String) {
      runOnUiThread {
        if (lobbyMembers.none { it.id == id }) {
          lobbyMembers.add(LobbyMember(id, nickname))
        }
        refreshMembers()
      }
    }
    override fun onMemberLeft(id: Int, nickname: String) {
      runOnUiThread {
        lobbyMembers.removeAll { it.id == id }
        refreshMembers()
      }
    }
    override fun onChatMessage(id: Int, nickname: String, message: String) = Unit
    override fun onStatusMessage(type: Int, id: Int, nickname: String) = Unit
    override fun onSyslinkPacket(data: ByteArray) = Unit
    override fun onRoomList(rooms: List<XblRoomInfo>) {
      runOnUiThread { showRooms(rooms) }
    }
    override fun onMemberReady(id: Int, nickname: String, ready: Boolean) {
      runOnUiThread {
        if (id == 0xFF.toInt()) {
          /* ALL_READY signal */
          checkAllReady()
          return@runOnUiThread
        }
        val member = lobbyMembers.find { it.id == id }
        if (member != null) {
          member.ready = ready
        }
        refreshMembers()
      }
    }
    override fun onError(msg: String) {
      runOnUiThread { Toast.makeText(this@XboxLiveActivity, msg, Toast.LENGTH_SHORT).show() }
    }
  }

  private fun updateLobbyState(state: Int) {
    when (state) {
      XblManager.STATE_IDLE -> {
        if (panelActiveLobby.visibility == View.VISIBLE)
          showPanel(panelSystemLink)
      }
      XblManager.STATE_JOINED, XblManager.STATE_HOSTING -> {
        if (panelActiveLobby.visibility == View.VISIBLE)
          refreshMembers()
      }
    }
  }

  // ── Room display ──

  private fun showRooms(rooms: List<XblRoomInfo>) {
    roomAdapter.submitList(rooms)
    updateRoomListStatus(rooms.size)
  }

  /**
   * Explains an empty room list instead of leaving a blank page. Without this
   * the screen looked the same whether System Link was working with no rooms,
   * switched off in Settings, or missing its native backend entirely - the
   * backend error was only ever a single Toast that never fired again.
   */
  private fun updateRoomListStatus(roomCount: Int = roomAdapter.itemCount) {
    if (roomCount > 0) {
      roomListStatus.visibility = View.GONE
      roomListRecycler.visibility = View.VISIBLE
      return
    }

    val networkEnabled = emulatorPrefs.getBoolean("setting_network_enable", false)
    val message = when {
      !XblManager.isBackendAvailable -> getString(R.string.syslink_status_backend_missing)
      !networkEnabled -> getString(R.string.syslink_status_network_disabled)
      else -> getString(R.string.syslink_status_no_rooms)
    }

    roomListStatus.text = message
    roomListStatus.visibility = View.VISIBLE
    roomListRecycler.visibility = View.GONE
  }

  // ── Password dialog ──

  private fun showPasswordDialog(callback: (String) -> Unit) {
    val input = EditText(this).apply {
      setHint("Enter password")
      setTextColor(0xFFFFF6EB.toInt())
      setHintTextColor(0x88FFFFFF.toInt())
      setBackgroundColor(0x1A1A1A.toInt())
      setPadding(16, 12, 16, 12)
    }
    AlertDialog.Builder(this)
      .setTitle("Password Required")
      .setView(input)
      .setPositiveButton("Join") { _, _ ->
        val pw = input.text.toString().trim()
        if (pw.isNotEmpty()) callback(pw)
      }
      .setNegativeButton("Cancel") { _, _ -> }
      .show()
  }

  // ── Panel switching ──

  private fun showPanel(panel: View) {
    panelSystemLink.visibility = if (panel == panelSystemLink) View.VISIBLE else View.GONE
    panelGameCatalog.visibility = if (panel == panelGameCatalog) View.VISIBLE else View.GONE
    panelLobbyConfig.visibility = if (panel == panelLobbyConfig) View.VISIBLE else View.GONE
    panelActiveLobby.visibility = if (panel == panelActiveLobby) View.VISIBLE else View.GONE
  }

  override fun finish() {
    super.finish()
    overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
  }

  private fun finishWithTransition() {
    finish()
    overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
  }

  // ── Room list adapter ──

  private class RoomListAdapter : RecyclerView.Adapter<RoomListAdapter.Holder>() {
    private val items = mutableListOf<XblRoomInfo>()
    var onRoomClick: ((XblRoomInfo) -> Unit)? = null

    fun submitList(list: List<XblRoomInfo>) {
      items.clear(); items.addAll(list); notifyDataSetChanged()
    }

    override fun getItemCount() = items.size

    override fun onCreateViewHolder(parent: android.view.ViewGroup, viewType: Int): Holder {
      val root = LinearLayout(parent.context).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(0, 0, 0, 0)
      }
      val tv = TextView(parent.context).apply {
        setPadding(16, 14, 16, 14)
        textSize = 15f
        setTextColor(0xFFFFF6EB.toInt())
        setBackgroundResource(R.drawable.setup_wizard_path_background)
      }
      val lp = RecyclerView.LayoutParams(
        RecyclerView.LayoutParams.MATCH_PARENT,
        RecyclerView.LayoutParams.WRAP_CONTENT
      )
      lp.topMargin = 4
      lp.bottomMargin = 4
      root.layoutParams = lp
      root.addView(tv)
      return Holder(root, tv)
    }

    override fun onBindViewHolder(holder: Holder, position: Int) {
      val room = items[position]
      val pw = if (room.hasPassword) " [PW]" else ""
      holder.textView.text = "${room.hostNickname} — ${room.name}  (${room.memberCount}/${room.maxMembers})$pw"
      holder.itemView.setOnClickListener { onRoomClick?.invoke(room) }
    }

    class Holder(root: View, val textView: TextView) : RecyclerView.ViewHolder(root)
  }

  // ── Game list adapter ──

  private class GameListAdapter : RecyclerView.Adapter<GameListAdapter.Holder>() {
    private val items = mutableListOf<String>()
    var onGameClick: ((String) -> Unit)? = null

    fun submitList(list: List<String>) {
      items.clear(); items.addAll(list); notifyDataSetChanged()
    }

    override fun getItemCount() = items.size

    override fun onCreateViewHolder(parent: android.view.ViewGroup, viewType: Int): Holder {
      val tv = TextView(parent.context).apply {
        setPadding(16, 14, 16, 14)
        textSize = 15f
        setTextColor(0xFFFFF6EB.toInt())
        setBackgroundResource(R.drawable.setup_wizard_path_background)
      }
      val lp = RecyclerView.LayoutParams(
        RecyclerView.LayoutParams.MATCH_PARENT,
        RecyclerView.LayoutParams.WRAP_CONTENT
      )
      lp.topMargin = 4
      lp.bottomMargin = 4
      tv.layoutParams = lp
      return Holder(tv)
    }

    override fun onBindViewHolder(holder: Holder, position: Int) {
      val game = items[position]
      holder.textView.text = game
      holder.textView.setOnClickListener { onGameClick?.invoke(game) }
    }

    class Holder(val textView: TextView) : RecyclerView.ViewHolder(textView)
  }
}

package Ali.Xanite

@Suppress("unused")
object XblManager {
  const val STATE_IDLE = 0
  const val STATE_HOSTING = 1
  const val STATE_JOINING = 2
  const val STATE_JOINED = 3

  const val STATUS_MEMBER_JOIN = 1
  const val STATUS_MEMBER_LEAVE = 2
  const val STATUS_MEMBER_KICKED = 3

  private var nativeLoadError: String? = null
  private var nativeMethodsAvailable: Boolean = try {
    System.loadLibrary("xemu")
    true
  } catch (error: UnsatisfiedLinkError) {
    nativeLoadError = error.message
    false
  }
  private var backendErrorReported = false

  /**
   * False once a native call has failed. Note loadLibrary() succeeding proves
   * nothing here - libxemu.so exists, but the XblManager JNI entry points are
   * not implemented in it, so this only becomes accurate after init() has run.
   */
  val isBackendAvailable: Boolean get() = nativeMethodsAvailable

  private val callbacks = mutableListOf<XblCallback>()
  private var currentState = STATE_IDLE
  private val members = mutableListOf<Pair<Int, String>>()

  fun addCallback(cb: XblCallback) {
    callbacks.add(cb)
    if (!nativeMethodsAvailable) {
      backendErrorReported = false
      reportBackendUnavailable()
    }
  }
  fun removeCallback(cb: XblCallback) { callbacks.remove(cb) }

  fun init(): Int = callNative(-1) { nativeInit() }

  fun shutdown() {
    callNative(Unit) { nativeShutdown() }
    currentState = STATE_IDLE
    members.clear()
  }

  fun hostRoom(name: String, password: String? = null, maxMembers: Int = 8): Int =
    callNative(-1) { nativeHostRoom(name, password ?: "", maxMembers) }

  fun joinRoom(host: String, port: Int = 27420, nickname: String, password: String? = null): Int =
    callNative(-1) { nativeJoinRoom(host, port, nickname, password ?: "") }

  fun leaveRoom() { callNative(Unit) { nativeLeaveRoom() } }

  fun sendChat(message: String) { callNative(Unit) { nativeSendChat(message) } }

  fun sendReady(ready: Boolean) { callNative(Unit) { nativeSendReady(ready) } }

  fun requestRoomList(addr: String, port: Int = 27420) {
    callNative(Unit) { nativeRequestRoomList(addr, port) }
  }

  fun tick() { callNative(Unit) { nativeTick() } }

  fun getState(): Int = callNative(currentState) { nativeGetState() }

  fun getMemberCount(): Int = members.size

  fun getMembers(): List<Pair<Int, String>> = members.toList()

  fun sendSyslinkPacket(data: ByteArray) {
    callNative(Unit) { nativeSendSyslinkPacket(data) }
  }

  private inline fun <T> callNative(defaultValue: T, action: () -> T): T {
    if (!nativeMethodsAvailable) {
      reportBackendUnavailable()
      return defaultValue
    }
    return try {
      action()
    } catch (error: UnsatisfiedLinkError) {
      nativeMethodsAvailable = false
      nativeLoadError = error.message
      reportBackendUnavailable()
      defaultValue
    }
  }

  private fun reportBackendUnavailable() {
    if (backendErrorReported) return
    backendErrorReported = true
    val detail = nativeLoadError?.takeIf { it.isNotBlank() }
    val message = if (detail == null) {
      "System Link backend is unavailable in this build"
    } else {
      "System Link backend is unavailable in this build: $detail"
    }
    callbacks.toList().forEach { it.onError(message) }
  }

  // --- JNI natives ---
  private external fun nativeInit(): Int
  private external fun nativeShutdown()
  private external fun nativeHostRoom(name: String, password: String, maxMembers: Int): Int
  private external fun nativeJoinRoom(host: String, port: Int, nickname: String, password: String): Int
  private external fun nativeLeaveRoom()
  private external fun nativeSendChat(message: String)
  private external fun nativeSendReady(ready: Boolean)
  private external fun nativeRequestRoomList(addr: String, port: Int)
  private external fun nativeGetState(): Int
  private external fun nativeTick()
  private external fun nativeSendSyslinkPacket(data: ByteArray)

  // --- JNI callbacks (called from native code) ---
  @Suppress("unused")
  private fun onStateChange(state: Int) {
    currentState = state
    callbacks.forEach { it.onStateChanged(state) }
  }

  @Suppress("unused")
  private fun onMemberJoin(id: Int, nickname: String) {
    members.add(id to nickname)
    callbacks.forEach { it.onMemberJoined(id, nickname) }
  }

  @Suppress("unused")
  private fun onMemberLeave(id: Int, nickname: String) {
    members.removeAll { it.first == id }
    callbacks.forEach { it.onMemberLeft(id, nickname) }
  }

  @Suppress("unused")
  private fun onChat(id: Int, nickname: String, message: String) {
    callbacks.forEach { it.onChatMessage(id, nickname, message) }
  }

  @Suppress("unused")
  private fun onStatus(type: Int, id: Int, nickname: String) {
    callbacks.forEach { it.onStatusMessage(type, id, nickname) }
  }

  @Suppress("unused")
  private fun onSyslinkPacket(data: ByteArray) {
    callbacks.forEach { it.onSyslinkPacket(data) }
  }

  @Suppress("unused")
  private fun onMemberReady(id: Int, nickname: String, ready: Boolean) {
    callbacks.forEach { it.onMemberReady(id, nickname, ready) }
  }

  @Suppress("unused")
  private fun onRoomList(rooms: Array<XblRoomInfo>) {
    callbacks.forEach { it.onRoomList(rooms.toList()) }
  }

  @Suppress("unused")
  private fun onError(msg: String) {
    callbacks.forEach { it.onError(msg) }
  }

  interface XblCallback {
    fun onStateChanged(state: Int) = Unit
    fun onMemberJoined(id: Int, nickname: String) = Unit
    fun onMemberLeft(id: Int, nickname: String) = Unit
    fun onChatMessage(id: Int, nickname: String, message: String) = Unit
    fun onStatusMessage(type: Int, id: Int, nickname: String) = Unit
    fun onSyslinkPacket(data: ByteArray) = Unit
    fun onRoomList(rooms: List<XblRoomInfo>) = Unit
    fun onMemberReady(id: Int, nickname: String, ready: Boolean) = Unit
    fun onError(msg: String) = Unit
  }
}

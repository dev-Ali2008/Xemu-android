package Ali.Xanite

data class XblRoomInfo(
  val name: String,
  val memberCount: Int,
  val maxMembers: Int,
  val hasPassword: Boolean,
  val hostNickname: String,
  val hostIp: String = "",
)

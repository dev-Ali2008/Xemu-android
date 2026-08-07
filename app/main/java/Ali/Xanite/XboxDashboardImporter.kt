package Ali.Xanite

import java.io.File
import java.io.IOException

internal object XboxDashboardImporter {

  @Throws(IOException::class)
  fun importDashboard(
    hddFile: File,
    sourceRoot: File,
    backupRoot: File,
  ) {
    require(hddFile.isFile) { "No local HDD image is configured." }
    require(sourceRoot.isDirectory) { "The selected dashboard source is not available." }
    check(NativeBridge.available) { "Native dashboard import is not available on this device" }
    if (!backupRoot.exists() && !backupRoot.mkdirs()) {
      throw IOException("Failed to create the dashboard backup folder.")
    }

    NativeBridge.nativeImportDashboard(
      hddFile.absolutePath,
      sourceRoot.absolutePath,
      backupRoot.absolutePath,
    )
  }

  private object NativeBridge {
    val available: Boolean = try {
      System.loadLibrary("SDL2")
      System.loadLibrary("xemu")
      true
    } catch (_: UnsatisfiedLinkError) {
      false
    }

    external fun nativeImportDashboard(
      hddPath: String,
      sourceRoot: String,
      backupRoot: String,
    )
  }
}

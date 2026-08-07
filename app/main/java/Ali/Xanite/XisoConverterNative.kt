package Ali.Xanite

fun interface XisoProgressCallback {
  fun onProgress(current: Int, total: Int)
}

object XisoConverterNative {
  private val isLibraryLoaded: Boolean = try {
    System.loadLibrary("xiso_converter")
    true
  } catch (_: UnsatisfiedLinkError) {
    false
  }

  @JvmStatic
  private external fun nativeConvertIsoToXiso(inputPath: String, outputPath: String): String?

  @JvmStatic
  private external fun nativeConvertIsoToXisoWithProgress(
    inputPath: String,
    outputPath: String,
    progressCallback: XisoProgressCallback
  ): String?

  fun convertIsoToXiso(inputPath: String, outputPath: String): String? {
    if (!isLibraryLoaded) {
      return "ISO converter native library is unavailable"
    }
    return nativeConvertIsoToXiso(inputPath, outputPath)
  }

  fun convertIsoToXiso(
    inputPath: String,
    outputPath: String,
    progressCallback: XisoProgressCallback
  ): String? {
    if (!isLibraryLoaded) {
      return "ISO converter native library is unavailable"
    }
    return nativeConvertIsoToXisoWithProgress(inputPath, outputPath, progressCallback)
  }

  fun isAvailable(): Boolean = isLibraryLoaded
}

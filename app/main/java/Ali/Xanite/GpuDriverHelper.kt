package Ali.Xanite

import android.content.Context
import android.net.Uri
import android.os.Build
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.zip.ZipFile

object GpuDriverHelper {
  private const val TAG = "GpuDriverHelper"
  private const val META_JSON = "meta.json"
  private const val MAX_ARCHIVE_BYTES = 512L * 1024L * 1024L
  private const val MAX_EXTRACTED_BYTES = 768L * 1024L * 1024L
  private const val MAX_ENTRY_BYTES = 512L * 1024L * 1024L
  private const val MAX_METADATA_BYTES = 64 * 1024
  private const val MAX_ARCHIVE_ENTRIES = 4096

  private lateinit var appContext: Context

  val driverInstallDir: String get() = appContext.filesDir.absolutePath + "/gpu_driver/"
  val driverStorageDir: String
    get() = File(appContext.getExternalFilesDir(null) ?: appContext.filesDir, "gpu_drivers").absolutePath + "/"
  val hookLibDir: String get() = appContext.applicationInfo.nativeLibraryDir + "/"

  fun init(context: Context) {
    appContext = context.applicationContext
    File(driverInstallDir).mkdirs()
    File(driverStorageDir).mkdirs()
  }

  fun supportsCustomDriverLoading(): Boolean {
    return File("/dev/kgsl-3d0").exists()
  }

  fun initializeDriver(customDriverName: String? = null) {
    nativeInitializeDriver(hookLibDir, driverInstallDir, customDriverName)
  }

  fun installDriverFromUri(context: Context, uri: Uri): Boolean {
    init(context)

    val tmpFile = File(driverStorageDir, "driver_tmp.zip")
    tmpFile.delete()
    try {
      context.contentResolver.openInputStream(uri)?.use { input ->
        FileOutputStream(tmpFile).use { output ->
          copyWithLimit(input, output, MAX_ARCHIVE_BYTES)
        }
      } ?: return false
    } catch (e: Exception) {
      Log.e(TAG, "Failed to copy driver URI", e)
      tmpFile.delete()
      return false
    }

    val metadata = readMetadata(tmpFile)
    if (metadata == null) {
      Log.e(TAG, "Invalid driver ZIP: no meta.json found")
      tmpFile.delete()
      return false
    }

    if (metadata.minApi > Build.VERSION.SDK_INT) {
      Log.e(TAG, "Driver requires API ${metadata.minApi}, device is ${Build.VERSION.SDK_INT}")
      tmpFile.delete()
      return false
    }

    val safeName = metadata.name
      ?.trim()
      ?.replace(Regex("[^A-Za-z0-9._-]+"), "_")
      ?.trim('.', '_')
      ?.take(96)
      ?.takeIf { it.isNotEmpty() }
      ?: run {
        tmpFile.delete()
        return false
      }
    val namedFile = File(driverStorageDir, "$safeName.zip")
    try {
      tmpFile.copyTo(namedFile, overwrite = true)
      tmpFile.delete()
    } catch (e: IOException) {
      Log.e(TAG, "Failed to store driver ZIP", e)
      tmpFile.delete()
      return false
    }

    return installDriver(namedFile)
  }

  fun installDriver(driverZip: File): Boolean {
    val installDir = File(driverInstallDir)
    val parentDir = installDir.parentFile ?: return false
    val stagingDir = File(parentDir, "${installDir.name}.staging")
    val backupDir = File(parentDir, "${installDir.name}.backup")

    if (!installDir.exists() && backupDir.exists() && !backupDir.renameTo(installDir)) {
      Log.e(TAG, "Failed to recover the previously installed driver")
      return false
    }
    if (backupDir.exists() && !backupDir.deleteRecursively()) {
      Log.e(TAG, "Failed to remove a stale driver backup")
      return false
    }
    stagingDir.deleteRecursively()
    if (!stagingDir.mkdirs()) {
      Log.e(TAG, "Failed to create driver staging directory")
      return false
    }

    try {
      var entryCount = 0
      var totalBytes = 0L
      val canonicalRoot = stagingDir.canonicalFile
      val canonicalRootPrefix = canonicalRoot.path + File.separator

      ZipFile(driverZip).use { zip ->
        zip.entries().asSequence().forEach { entry ->
          entryCount++
          if (entryCount > MAX_ARCHIVE_ENTRIES) {
            throw IOException("Driver ZIP contains too many entries")
          }

          val entryName = entry.name.replace('\\', '/')
          if (entryName.isBlank()) {
            return@forEach
          }
          val outFile = File(stagingDir, entryName).canonicalFile
          if (outFile.path != canonicalRoot.path && !outFile.path.startsWith(canonicalRootPrefix)) {
            throw IOException("Driver ZIP contains an invalid path: ${entry.name}")
          }

          if (entry.isDirectory) {
            if (!outFile.exists() && !outFile.mkdirs()) {
              throw IOException("Failed to create driver directory: ${entry.name}")
            }
          } else {
            val declaredSize = entry.size
            if (declaredSize > MAX_ENTRY_BYTES) {
              throw IOException("Driver ZIP entry is too large: ${entry.name}")
            }
            outFile.parentFile?.let { parent ->
              if (!parent.exists() && !parent.mkdirs()) {
                throw IOException("Failed to create driver directory: ${parent.name}")
              }
            }
            zip.getInputStream(entry).use { input ->
              FileOutputStream(outFile).use { output ->
                val copied = copyWithLimit(input, output, MAX_ENTRY_BYTES)
                totalBytes += copied
                if (totalBytes > MAX_EXTRACTED_BYTES) {
                  throw IOException("Driver ZIP expands beyond the allowed size")
                }
              }
            }
          }
        }
      }

      val installedMetadata = readInstalledMetadata(stagingDir)
        ?: throw IOException("Driver ZIP must contain a valid root-level $META_JSON")
      val libraryName = installedMetadata.libraryName?.trim().orEmpty()
      val libraryFile = File(stagingDir, libraryName).canonicalFile
      if (libraryName.isEmpty() ||
        (libraryFile.path != canonicalRoot.path && !libraryFile.path.startsWith(canonicalRootPrefix)) ||
        !libraryFile.isFile
      ) {
        throw IOException("Driver metadata references a missing or invalid library")
      }

      backupDir.deleteRecursively()
      if (installDir.exists() && !installDir.renameTo(backupDir)) {
        throw IOException("Failed to preserve the currently installed driver")
      }
      if (!stagingDir.renameTo(installDir)) {
        if (backupDir.exists()) {
          backupDir.renameTo(installDir)
        }
        throw IOException("Failed to activate the new driver")
      }
      backupDir.deleteRecursively()
    } catch (e: Exception) {
      Log.e(TAG, "Failed to extract driver", e)
      stagingDir.deleteRecursively()
      if (!installDir.exists() && backupDir.exists() && !backupDir.renameTo(installDir)) {
        Log.e(TAG, "Failed to restore the previous driver after install failure")
      }
      return false
    }

    return true
  }

  fun installDefaultDriver() {
    File(driverInstallDir).deleteRecursively()
    File(driverInstallDir).mkdirs()
  }

  fun getInstalledDriverName(): String? {
    val metaFile = File(driverInstallDir, META_JSON)
    if (!metaFile.exists()) return null
    return try {
      val json = JSONObject(metaFile.readText())
      json.optNullableString("name")
    } catch (e: Exception) {
      null
    }
  }

  fun getInstalledDriverLibrary(): String? {
    val metaFile = File(driverInstallDir, META_JSON)
    if (!metaFile.exists()) return null
    return try {
      val json = JSONObject(metaFile.readText())
      json.optNullableString("libraryName")
    } catch (e: Exception) {
      null
    }
  }

  fun getAvailableDrivers(): List<DriverMetadata> {
    val dir = File(driverStorageDir)
    if (!dir.exists()) return emptyList()
    return dir.listFiles()
      ?.filter { it.extension == "zip" }
      ?.mapNotNull { readMetadata(it)?.copy(path = it.absolutePath) }
      ?.sortedBy { it.name }
      ?: emptyList()
  }

  fun readMetadata(zipFile: File): DriverMetadata? {
    if (!zipFile.isFile || zipFile.length() > MAX_ARCHIVE_BYTES) return null
    try {
      ZipFile(zipFile).use { zip ->
        val entries = zip.entries()
        while (entries.hasMoreElements()) {
          val entry = entries.nextElement()
          val normalizedName = entry.name.replace('\\', '/').trimStart('/')
          if (!entry.isDirectory && normalizedName.equals(META_JSON, ignoreCase = true)) {
            if (entry.size > MAX_METADATA_BYTES) {
              return null
            }
            zip.getInputStream(entry).use { input ->
              val text = readTextWithLimit(input, MAX_METADATA_BYTES)
              return parseMetadata(text, zipFile.absolutePath)
            }
          }
        }
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to read driver metadata from ${zipFile.name}", e)
    }
    return null
  }

  private fun readInstalledMetadata(root: File): DriverMetadata? {
    val metadataFile = File(root, META_JSON)
    if (!metadataFile.isFile || metadataFile.length() > MAX_METADATA_BYTES) {
      return null
    }
    return try {
      parseMetadata(metadataFile.readText(), null)
    } catch (_: Exception) {
      null
    }
  }

  private fun parseMetadata(text: String, path: String?): DriverMetadata? {
    val json = JSONObject(text)
    val name = json.optString("name", "").trim().takeIf { it.isNotEmpty() } ?: return null
    val libraryName = json.optString("libraryName", "").trim().takeIf { it.isNotEmpty() }
      ?: return null
    return DriverMetadata(
      name = name,
      description = json.optNullableString("description"),
      author = json.optNullableString("author"),
      libraryName = libraryName,
      minApi = json.optInt("minApi", 0).coerceAtLeast(0),
      path = path,
    )
  }

  private fun JSONObject.optNullableString(key: String): String? {
    if (!has(key) || isNull(key)) return null
    return optString(key).trim().takeIf { it.isNotEmpty() }
  }

  private fun copyWithLimit(
    input: java.io.InputStream,
    output: java.io.OutputStream,
    maxBytes: Long,
  ): Long {
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var total = 0L
    while (true) {
      val count = input.read(buffer)
      if (count < 0) break
      total += count
      if (total > maxBytes) {
        throw IOException("Input exceeds the allowed size")
      }
      output.write(buffer, 0, count)
    }
    return total
  }

  private fun readTextWithLimit(input: java.io.InputStream, maxBytes: Int): String {
    val output = java.io.ByteArrayOutputStream()
    copyWithLimit(input, output, maxBytes.toLong())
    return output.toString(Charsets.UTF_8.name())
  }

  private external fun nativeInitializeDriver(
    hookLibDir: String?,
    customDriverDir: String?,
    customDriverName: String?
  )

  data class DriverMetadata(
    val name: String? = null,
    val description: String? = null,
    val author: String? = null,
    val libraryName: String? = null,
    val minApi: Int = 0,
    val path: String? = null
  )
}

package com.xanite.utils

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object FileUtils {

    suspend fun getGamesDirectory(context: Context, folderName: String): File = withContext(Dispatchers.IO) {
        return@withContext File(context.getExternalFilesDir(null), folderName).apply {
            if (!exists()) {
                mkdirs()
            }
        }
    }

    suspend fun getFileNameFromUri(context: Context, uri: Uri): String? = withContext(Dispatchers.IO) {
        return@withContext when (uri.scheme) {
            "content" -> getContentFileName(context, uri)
            "file" -> uri.lastPathSegment?.let { File(it).name }
            else -> null
        }
    }

    suspend fun copyStreamToFile(inputStream: InputStream, destFile: File): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            inputStream.use { input ->
                destFile.outputStream().use { output ->
                    input.copyTo(output)
                    true
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }

    private suspend fun getContentFileName(context: Context, uri: Uri): String? = withContext(Dispatchers.IO) {
        return@withContext context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                cursor.getString(cursor.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
            } else {
                null
            }
        }
    }

    suspend fun copyFile(sourceFile: File, destFile: File): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            sourceFile.inputStream().use { input ->
                destFile.outputStream().use { output ->
                    input.copyTo(output)
                    true
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }
}
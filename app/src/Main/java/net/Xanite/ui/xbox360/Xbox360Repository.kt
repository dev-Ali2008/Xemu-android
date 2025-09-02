package com.xanite.xbox360

import android.content.Context
import android.net.Uri
import com.xanite.models.Game
import com.xanite.utils.FileUtils
import com.xanite.utils.ZipUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.IOException

class Xbox360Repository(private val context: Context) {
    private var gamesDir: File? = null

    private val supportedExtensions = listOf("iso", "xex", "god", "arc", "bin", "zip")
    private val supportedArchiveExtensions = listOf("zip")
    private val xbox360FolderPattern = Regex("[0-9A-Fa-f]{8}")

    suspend fun initialize() {
        gamesDir = FileUtils.getGamesDirectory(context, "xbox_360")
    }

    suspend fun getGames(): List<Game> = withContext(Dispatchers.IO) {
        val gamesDirectory = gamesDir ?: run {
            gamesDir = FileUtils.getGamesDirectory(context, "xbox_360")
            gamesDir!!
        }

        return@withContext gamesDirectory.listFiles()?.flatMap { file ->
            when {

                file.isDirectory && file.name.matches(xbox360FolderPattern) -> {
                    val gameFiles = file.walk()
                        .filter { it.isFile && it.parentFile?.name == "000D0000" }
                        .toList()

                    if (gameFiles.isNotEmpty()) {
                        listOf(createXbox360Game(file, gameFiles.first()))
                    } else {
                        emptyList()
                    }
                }

                file.extension.lowercase() in supportedArchiveExtensions -> {
                    handleArchiveFile(file)
                }

                file.extension.lowercase() in supportedExtensions -> {
                    listOf(Game(
                        path = file.absolutePath,
                        name = file.nameWithoutExtension,
                        type = file.extension.uppercase()
                    ))
                }
                else -> emptyList()
            }
        } ?: emptyList()
    }

    private fun createXbox360Game(folder: File, gameFile: File): Game {

        val originalFolderName = folder.parentFile?.name?.removeSuffix(".zip") ?: folder.name
        return Game(
            path = gameFile.absolutePath,
            name = extractGameName(originalFolderName),
            type = "XBOX360"
        )
    }

    private fun extractGameName(folderName: String): String {

        return folderName
            .replace(Regex("""\[.*?\]|\(.*?\)"""), "") 
            .replace(Regex("""^\s*-|\s*-\s*$"""), "") 
            .trim()
    }

    private suspend fun handleArchiveFile(file: File): List<Game> = withContext(Dispatchers.IO) {
        val gamesDirectory = gamesDir ?: run {
            gamesDir = FileUtils.getGamesDirectory(context, "xbox_360")
            gamesDir!!
        }

        val extractedDir = File(gamesDirectory, file.nameWithoutExtension)
        if (!extractedDir.exists()) {
            if (!ZipUtils.extractZip(file, extractedDir)) {
                return@withContext emptyList()
            }
        }


        return@withContext extractedDir.listFiles()?.flatMap { innerFile ->
            when {
                innerFile.isDirectory && innerFile.name.matches(xbox360FolderPattern) -> {
                    val gameFiles = innerFile.walk()
                        .filter { it.isFile && it.parentFile?.name == "000D0000" }
                        .toList()

                    gameFiles.map { gameFile ->
                        createXbox360Game(innerFile, gameFile)
                    }
                }
                else -> emptyList()
            }
        } ?: emptyList()
    }

    suspend fun addGame(uri: Uri): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            val fileName = FileUtils.getFileNameFromUri(context, uri) ?: return@withContext false
            val extension = fileName.substringAfterLast('.', "").lowercase()

            if (extension !in supportedExtensions) return@withContext false

            val gamesDirectory = gamesDir ?: run {
                gamesDir = FileUtils.getGamesDirectory(context, "xbox_360")
                gamesDir!!
            }

            val destFile = File(gamesDirectory, fileName)

            when (extension) {
                in supportedArchiveExtensions -> {
                    val inputStream = context.contentResolver.openInputStream(uri) ?: return@withContext false
                    FileUtils.copyStreamToFile(inputStream, destFile)


                    handleArchiveFile(destFile).isNotEmpty()
                }
                else -> {
                    val inputStream = context.contentResolver.openInputStream(uri) ?: return@withContext false
                    FileUtils.copyStreamToFile(inputStream, destFile)
                    true
                }
            }
        } catch (e: IOException) {
            e.printStackTrace()
            false
        }
    }
}

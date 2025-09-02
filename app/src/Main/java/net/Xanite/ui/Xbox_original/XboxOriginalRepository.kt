package com.xanite.xboxoriginal

import android.content.Context
import android.net.Uri
import com.xanite.models.Game
import com.xanite.utils.FileUtils
import com.xanite.utils.ZipUtils
import java.io.File
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope

class XboxOriginalRepository(private val context: Context) {
    
    private var gamesDir: File? = null
   
    private val supportedExtensions = listOf("xbe", "iso", "xiso", "zip")
    private val supportedArchiveExtensions = listOf("zip")

    private suspend fun getGamesDirectory(): File = withContext(Dispatchers.IO) {
        if (gamesDir == null) {
            gamesDir = File(context.getExternalFilesDir(null), "xbox_original").apply {
                if (!exists()) {
                    mkdirs()
                }
            }
        }
        return@withContext gamesDir!!
    }

    suspend fun getGames(): List<Game> = withContext(Dispatchers.IO) {
        val gamesDirectory = getGamesDirectory()
        println("🔍 DEBUG: Scanning games directory: ${gamesDirectory.absolutePath}")
        val files = gamesDirectory.listFiles()
        println("🔍 DEBUG: Found ${files?.size ?: 0} files/directories")
        
        return@withContext files?.flatMap { file ->
            when {
                
                file.extension.lowercase() in listOf("iso", "xiso") -> {
                    listOf(createXboxOriginalGame(file))
                }
                else -> emptyList()
            }
        } ?: emptyList()
    }
    
    private suspend fun handleGameDirectory(directory: File): List<Game> = withContext(Dispatchers.IO) {
        println("🔍 DEBUG: Handling directory: ${directory.name}")
        
        val actualPathFile = File(directory, ".actual_path")
        if (actualPathFile.exists()) {
            println("🔍 DEBUG: Found .actual_path file, creating folder game with Downloads path")
            return@withContext listOf(createXboxOriginalGameFromDirectory(directory, null))
        }
        
        val folderUriFile = File(directory, ".folder_uri")
        if (folderUriFile.exists()) {
            println("🔍 DEBUG: Found .folder_uri file, creating folder game")
            return@withContext listOf(createXboxOriginalGameFromDirectory(directory, null))
        }
        
        val xbeFiles = directory.walk()
            .filter { it.isFile && it.extension.lowercase() in supportedExtensions }
            .toList()
        
        return@withContext if (xbeFiles.isNotEmpty()) {
            
            listOf(createXboxOriginalGameFromDirectory(directory, xbeFiles.first()))
        } else {
            emptyList()
        }
    }
    
    private suspend fun createXboxOriginalGameFromDirectory(directory: File, mainXbeFile: File?): Game = withContext(Dispatchers.IO) {
        
        val actualPathFile = File(directory, ".actual_path")
        val actualPath = if (actualPathFile.exists()) {
            withContext(Dispatchers.IO) { actualPathFile.readText() }
        } else {
            directory.absolutePath
        }
        
        println("🔍 DEBUG: Creating game with path: $actualPath")
        
        return@withContext Game(
            path = actualPath, 
            name = extractGameName(directory.name),
            type = "FOLDER"
        )
    }

    private suspend fun createXboxOriginalGame(file: File): Game = withContext(Dispatchers.IO) {
        return@withContext Game(
            path = file.absolutePath,
            name = extractGameName(file.nameWithoutExtension),
            type = when (file.extension.lowercase()) {
                "xbe" -> "XBE"
                "iso" -> "ISO"
                "xiso" -> "XISO"
                else -> "UNKNOWN"
            }
        )
    }

    private suspend fun extractGameName(fileName: String): String = withContext(Dispatchers.IO) {
       
        return@withContext fileName
            .replace(Regex("""\[.*?\]|\(.*?\)"""), "") 
            .replace(Regex("""_"""), " ") 
            .replace(Regex("""(?i)\b(ntsc|pal|usa|eur|jap|demo|final|repack|xbox)\b"""), "") 
            .trim()
    }

    private suspend fun handleArchiveFile(file: File): List<Game> = withContext(Dispatchers.IO) {
        val gamesDirectory = getGamesDirectory()
        val extractedDir = File(gamesDirectory, file.nameWithoutExtension)
        if (!extractedDir.exists()) {
            if (!ZipUtils.extractZip(file, extractedDir)) {
                return@withContext emptyList()
            }
        }
        
        val gameFiles = extractedDir.walk()
            .filter { it.isFile && it.extension.lowercase() in supportedExtensions }
            .toList()

        coroutineScope {
            gameFiles.map { gameFile ->
                async { createXboxOriginalGame(gameFile) }
            }.awaitAll()
        }
    }

    suspend fun addGame(uri: Uri): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
           
            if (isDirectoryUri(uri)) {
                addGameFolder(uri)
            } else {
                addGameFile(uri)
            }
        } catch (e: IOException) {
            e.printStackTrace()
            false
        }
    }
    
    private suspend fun addGameFile(uri: Uri): Boolean = withContext(Dispatchers.IO) {
        val fileName = FileUtils.getFileNameFromUri(context, uri) ?: return@withContext false
        val extension = fileName.substringAfterLast('.', "").lowercase()
        
        if (extension !in supportedExtensions) {
           
            if (!isLikelyXbeFile(uri)) {
                return@withContext false
            }
        }
        
                    val gamesDirectory = getGamesDirectory()
            val destFile = File(gamesDirectory, fileName)
        
        return@withContext when (extension) {
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
    }
    
    private suspend fun addGameFolder(uri: Uri): Boolean = withContext(Dispatchers.IO) {
        try {
           
            val actualPath = getActualPathFromUri(uri)
            val folderName = getFolderNameFromUri(uri) ?: "GameFolder_${System.currentTimeMillis()}"
            
            if (actualPath != null) {
                
                val gamesDirectory = getGamesDirectory()
                val gameFolder = File(gamesDirectory, folderName)
                if (!gameFolder.exists()) {
                    gameFolder.mkdirs()
                }
                
               
                val pathInfoFile = File(gameFolder, ".actual_path")
                withContext(Dispatchers.IO) { pathInfoFile.writeText(actualPath) }
                
                println("🔍 DEBUG: Saved actual path: $actualPath")
                return@withContext true
            } else {
                
                val gamesDirectory = getGamesDirectory()
                val gameFolder = File(gamesDirectory, folderName)
                if (!gameFolder.exists()) {
                    gameFolder.mkdirs()
                }
                
                val folderInfoFile = File(gameFolder, ".folder_uri")
                withContext(Dispatchers.IO) { folderInfoFile.writeText(uri.toString()) }
                
                println("🔍 DEBUG: Saved URI: ${uri.toString()}")
                return@withContext true
            }
            
        } catch (e: Exception) {
            e.printStackTrace()
            return@withContext false
        }
    }
    
    private suspend fun getActualPathFromUri(uri: Uri): String? = withContext(Dispatchers.IO) {
        return@withContext try {
           
            val documentFile = androidx.documentfile.provider.DocumentFile.fromTreeUri(context, uri)
            val path = documentFile?.uri?.path
            
            if (path?.contains("/Download/") == true || path?.contains("/Downloads/") == true) {
                println("🔍 DEBUG: Found Downloads path: $path")
                return@withContext path
            }
            
            null
        } catch (e: Exception) {
            null
        }
    }
    
    private suspend fun isDirectoryUri(uri: Uri): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            uri.toString().contains("tree")
        } catch (e: Exception) {
            false
        }
    }
    
    private suspend fun getFolderNameFromUri(uri: Uri): String? = withContext(Dispatchers.IO) {
        return@withContext try {
            val documentFile = androidx.documentfile.provider.DocumentFile.fromTreeUri(context, uri)
            documentFile?.name ?: "GameFolder_${System.currentTimeMillis()}"
        } catch (e: Exception) {
            "GameFolder_${System.currentTimeMillis()}"
        }
    }
    
    private suspend fun isLikelyXbeFile(uri: Uri): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            val inputStream = context.contentResolver.openInputStream(uri) ?: return@withContext false
            val buffer = ByteArray(4)
            val bytesRead = inputStream.read(buffer)
            inputStream.close()
            
            if (bytesRead >= 4) {
                val signature = (buffer[3].toInt() shl 24) or 
                               (buffer[2].toInt() shl 16) or 
                               (buffer[1].toInt() shl 8) or 
                               buffer[0].toInt()
                return@withContext signature == 0x48454258 
            }
            false
        } catch (e: Exception) {
            false
        }
    }
    
    private suspend fun isLikelyXbeFile(file: File): Boolean = withContext(Dispatchers.IO) {
        return@withContext try {
            val inputStream = file.inputStream()
            val buffer = ByteArray(4)
            val bytesRead = inputStream.read(buffer)
            inputStream.close()
            
            if (bytesRead >= 4) {
                val signature = (buffer[3].toInt() shl 24) or 
                               (buffer[2].toInt() shl 16) or 
                               (buffer[1].toInt() shl 8) or 
                               buffer[0].toInt()
                return@withContext signature == 0x48454258 
            }
            false
        } catch (e: Exception) {
            false
        }
    }
}
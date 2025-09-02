package com.xanite.models

import java.io.File

data class Game(
    val path: String,                
    val name: String?,               
    val type: String = "",           
    val xbePath: String? = null,     
    val isoPath: String? = null,     
    val coverPath: String? = null,   
    val lastPlayed: Long = 0,        
    val description: String = "",    
    val size: Long = 0L,             
    val lastModified: Long = 0L      
) {

    fun getGameFile(): File = File(path)


    val fileSize: Long
        get() = getGameFile().length()


    fun getSizeInGB(): Double {
        return fileSize.toDouble() / (1024 * 1024 * 1024)
    }


    fun getFormattedSize(): String {
        return "%.2f GB".format(getSizeInGB())
    }


    fun hasXbe(): Boolean = !xbePath.isNullOrEmpty()


    fun hasIso(): Boolean = !isoPath.isNullOrEmpty()
}

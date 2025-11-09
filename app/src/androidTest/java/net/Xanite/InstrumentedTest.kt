package com.xanite

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.Assert.*

@RunWith(AndroidJUnit4::class)
class EmulatorInstrumentedTest {

    @Test
    fun testDeviceCompatibility() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        
        val deviceInfo = AndroidConfig.GetDeviceInfo()
        val isSupported = AndroidConfig.IsDeviceSupported(deviceInfo)
        
        assertTrue("Device should meet minimum requirements", isSupported)
        assertTrue("Should have enough RAM", deviceInfo.total_ram_mb >= 3072)
    }

    @Test
    fun testVulkanSupport() {
        val config = AndroidConfig()
        
        assertTrue("Vulkan 1.1+ required", config.IsVulkan11Supported())
        assertTrue("Vulkan compatibility check", config.CheckVulkanCompatibility())
    }

    @Test
    fun testAppDirectoriesCreation() {
        val success = AndroidConfig.CreateAppDirectories()
        assertTrue("Should create app directories", success)
    }

    @Test
    fun testSettingsPersistence() {
        val settings = AndroidSettings()
    
        val graphics = settings.GetGraphicsSettings()
        assertEquals(1.0f, graphics.resolution_scale, 0.01f)
        assertEquals(RenderAPI.VULKAN, graphics.render_api)
        
        val audio = settings.GetAudioSettings()
        assertTrue("Should mute on focus loss", audio.mute_on_focus_loss)
    }

    @Test
    fun testGameFileDetection() {
        val supported = GameMetadata.IsSupportedFileType("/test/game.xex")
        assertTrue("XEX files should be supported", supported)
        
        val unsupported = GameMetadata.IsSupportedFileType("/test/game.txt")
        assertFalse("TXT files should not be supported", unsupported)
    }
}

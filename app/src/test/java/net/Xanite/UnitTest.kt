package com.xanite

import org.junit.Assert.*
import org.junit.Test

class EmulatorUnitTest {

    @Test
    fun testVulkanConfigValidation() {
        val config = AndroidConfig()
        assertTrue("Vulkan should be supported", config.IsVulkan11Supported())
    }

    @Test
    fun testGameMetadataLoading() {
        val metadata = GameMetadata("/test/game.xex")
        assertFalse("Invalid path should not load", metadata.IsValid())
    }

    @Test
    fun testPerformanceSettings() {
        val settings = AndroidSettings()
        val perfSettings = settings.GetPerformanceSettings()
        
        assertEquals("Default should be balanced", 
            PerformanceMode.BALANCED, perfSettings.performance_mode)
        
        assertTrue("VSync should be enabled by default", 
            settings.GetGraphicsSettings().vsync)
    }

    @Test
    fun testInputDeadzone() {
        val input = NativeInput()
        
        assertEquals(0.0f, input.ApplyDeadzone(0.1f, 0.15f), 0.01f)
        assertTrue(input.ApplyDeadzone(0.5f, 0.15f) > 0.3f)
    }
}

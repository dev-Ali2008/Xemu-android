package com.xanite.xboxoriginal

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.xanite.R
import com.xanite.utils.RendererManager
import com.xanite.utils.EmulatorDisplayLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.delay
import java.io.File

class XboxShaderActivity : AppCompatActivity(), SurfaceHolder.Callback {
    
    private lateinit var emulatorSurface: SurfaceView
    private lateinit var gameDisplay: ImageView
    private lateinit var loadingOverlay: LinearLayout
    private lateinit var progressBar: ProgressBar
    private lateinit var statusText: TextView
    private lateinit var errorText: TextView
    
    private var gamePath: String? = null
    private var rendererType: RendererManager.RendererType? = null
    private var emulatorHandle: Long = 0L
    
    companion object {
        init {
            System.loadLibrary("xbox_emulator")
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        EmulatorDisplayLog.display("XboxShaderActivity", "onCreate started")
        
        setContentView(R.layout.activity_xbox_shader)
        EmulatorDisplayLog.display("XboxShaderActivity", "Layout set successfully")
        
        val path = intent.getStringExtra("game_path")
        val rendererTypeName = intent.getStringExtra("renderer_type")
        
        EmulatorDisplayLog.display("XboxShaderActivity", "Intent extras - path: $path, renderer: $rendererTypeName")
        
        if (path.isNullOrEmpty()) {
            showError("Game path is required")
            EmulatorDisplayLog.display("XboxShaderActivity", "Game path is missing. Schwarzbild möglich.")
            return
        }
        
        if (rendererTypeName.isNullOrEmpty()) {
            showError("Renderer type is required")
            EmulatorDisplayLog.display("XboxShaderActivity", "Renderer type is missing. Schwarzbild möglich.")
            return
        }
        
        val renderer = try {
            RendererManager.RendererType.valueOf(rendererTypeName)
        } catch (e: IllegalArgumentException) {
            showError("Invalid renderer type: $rendererTypeName")
            EmulatorDisplayLog.display("XboxShaderActivity", "Ungültiger Renderer-Typ: $rendererTypeName. Schwarzbild möglich.")
            return
        }
        
        gamePath = path
        rendererType = renderer
        
        EmulatorDisplayLog.display("XboxShaderActivity", "Game path and renderer type validated successfully")
        
        initializeViews()
        setupSurface()
        
        EmulatorDisplayLog.display("XboxShaderActivity", "Waiting for surface initialization...")
        Thread.sleep(200) 
        
        lifecycleScope.launch(Dispatchers.IO) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Starting emulation in background thread")
            
            try {
                EmulatorDisplayLog.display("XboxShaderActivity", "About to start emulation")
                startEmulation() 
                EmulatorDisplayLog.display("XboxShaderActivity", "Emulation started successfully")
                
                EmulatorDisplayLog.display("XboxShaderActivity", "Starting render loop to execute CPU code")
                startRenderLoop()
            } catch (e: Exception) {
                EmulatorDisplayLog.display("XboxShaderActivity", "Exception in emulation thread: ${e.message}")
            }
        }
    }
    
    private fun initializeViews() {
        EmulatorDisplayLog.display("XboxShaderActivity", "Initializing views")
        
        emulatorSurface = findViewById(R.id.emulator_surface)
        gameDisplay = findViewById(R.id.game_display)
        loadingOverlay = findViewById(R.id.loading_overlay)
        progressBar = findViewById(R.id.progress_bar)
        statusText = findViewById(R.id.status_text)
        errorText = findViewById(R.id.error_text)
              
        if (emulatorSurface == null) {
            EmulatorDisplayLog.display("XboxShaderActivity", "emulatorSurface is null - Schwarzbild möglich")
        } else {
            EmulatorDisplayLog.display("XboxShaderActivity", "emulatorSurface found successfully")
        }
        
        emulatorSurface.holder.addCallback(this)
        EmulatorDisplayLog.display("XboxShaderActivity", "Surface holder callback added")
    }
    
    private fun setupSurface() {
        EmulatorDisplayLog.display("XboxShaderActivity", "Setting up surface with fixed size: 1280x720")
        
        emulatorSurface.holder.setFixedSize(1280, 720)
        
        Thread.sleep(100)
       
        if (emulatorSurface.holder.surface == null) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Surface holder surface is null - Schwarzbild möglich")
        } else {
            EmulatorDisplayLog.display("XboxShaderActivity", "Surface holder initialized successfully")
                               
        val surface = emulatorSurface.holder.surface
        EmulatorDisplayLog.display("XboxShaderActivity", "Surface created successfully")
       
        try {
            val surfaceFrame = emulatorSurface.holder.surfaceFrame
            EmulatorDisplayLog.display("XboxShaderActivity", "Surface frame: $surfaceFrame")
        } catch (e: Exception) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Could not get surface frame: ${e.message}")
        }
        }
    }
    
    private suspend fun startEmulation() {
        EmulatorDisplayLog.display("XboxShaderActivity", "startEmulation started")
        showLoading("Initializing Xbox Emulator...")
        
        try {
            
            val renderer = rendererType
            if (renderer == null) {
                showError("Invalid renderer type")
                EmulatorDisplayLog.display("XboxShaderActivity", "Renderer type null. Schwarzbild möglich.")
                return
            }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Creating emulator instance with renderer: ${renderer.name}")
            emulatorHandle = withContext(Dispatchers.IO) {
                safeNativeCallLong { nativeCreateInstance() }
            }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Emulator instance created - handle: $emulatorHandle")
            
            if (emulatorHandle == 0L) {
                showError("Failed to create emulator instance")
                EmulatorDisplayLog.display("XboxShaderActivity", "Emulator-Instanz konnte nicht erstellt werden. Schwarzbild möglich.")
                return
            }
            
            showLoading("Loading BIOS files...")
            val biosDir = "/data/user/0/com.xanite/files/bios/"
            val complexBiosPath = "$biosDir/Complex_4627v1.03.bin"
            val mcpxBiosPath = "$biosDir/mcpx_1.0.bin"
            val hddImagePath = "$biosDir/xbox_hdd.qcow2"
            
            try {
               
                val renderer = rendererType
                if (renderer == null) {
                    showError("Renderer type is null")
                    EmulatorDisplayLog.display("XboxShaderActivity", "Renderer type is null. Schwarzbild möglich.")
                    return
                }
                EmulatorDisplayLog.display("XboxShaderActivity", "Using renderer from intent: ${renderer.name}")
                EmulatorDisplayLog.display("XboxShaderActivity", "Renderer type enum: $renderer")
                EmulatorDisplayLog.display("XboxShaderActivity", "Is Vulkan: ${renderer == RendererManager.RendererType.VULKAN}")
                EmulatorDisplayLog.display("XboxShaderActivity", "Is OpenGL: ${renderer == RendererManager.RendererType.OPENGL}")
                
                EmulatorDisplayLog.display("XboxShaderActivity", "Initializing emulator with BIOS files...")
                val biosInitSuccess = safeNativeCall { 
                    nativeInitEmulator(emulatorHandle, complexBiosPath, mcpxBiosPath, hddImagePath) 
                }
                
                if (!biosInitSuccess) {
                    showError("Failed to initialize BIOS")
                    EmulatorDisplayLog.display("XboxShaderActivity", "BIOS-Initialisierung fehlgeschlagen. Schwarzbild möglich.")
                    return
                }
                
                EmulatorDisplayLog.display("XboxShaderActivity", "BIOS initialized successfully, proceeding to renderer creation")
                
                EmulatorDisplayLog.display("XboxShaderActivity", "Creating renderer: ${renderer.name}")
                val success = when (renderer) {
                    RendererManager.RendererType.NV2A -> {
                        EmulatorDisplayLog.display("XboxShaderActivity", "Creating NV2A renderer")
                        safeNativeCall { nativeCreateNV2ARenderer(emulatorHandle) }
                    }
                    RendererManager.RendererType.VULKAN -> {
                        EmulatorDisplayLog.display("XboxShaderActivity", "Creating Vulkan renderer")
                        safeNativeCall { nativeCreateVulkanRenderer(emulatorHandle) }
                    }
                    RendererManager.RendererType.OPENGL -> {
                        EmulatorDisplayLog.display("XboxShaderActivity", "Creating OpenGL renderer")
                        
                        val rendererHandle = nativeCreateOpenGLRenderer(emulatorHandle)
                        if (rendererHandle != 0L) {
                            EmulatorDisplayLog.display("XboxShaderActivity", "OpenGL renderer created successfully with handle: $rendererHandle")
                            true
                        } else {
                            EmulatorDisplayLog.display("XboxShaderActivity", "Failed to create OpenGL renderer")
                            false
                        }
                    }
                    else -> {
                        EmulatorDisplayLog.display("XboxShaderActivity", "Unknown renderer type: ${renderer.name}")
                        false
                    }
                }
                
                EmulatorDisplayLog.display("XboxShaderActivity", "Renderer creation result: $success")
                
                if (!success) {
                    showError("Failed to initialize ${renderer.name} renderer")
                    EmulatorDisplayLog.display("XboxShaderActivity", "Renderer-Initialisierung (${renderer.name}) fehlgeschlagen. Schwarzbild möglich.")
                    return
                }
                
                loadGame()
                
            } catch (e: Exception) {
                showError("Error starting emulation: "+e.message)
                EmulatorDisplayLog.display("XboxShaderActivity", "Exception beim Starten der Emulation: ${e.message}. Schwarzbild möglich.")
            }
            
        } catch (e: Exception) {
            showError("Error starting emulation: "+e.message)
            EmulatorDisplayLog.display("XboxShaderActivity", "Exception beim Starten der Emulation: ${e.message}. Schwarzbild möglich.")
        }
    }
    
    private suspend fun loadGame() {
        EmulatorDisplayLog.display("XboxShaderActivity", "loadGame started")
        showLoading("Loading game...")
        
        try {
            
            val path = gamePath
            if (path.isNullOrEmpty()) {
                showError("Invalid game path")
                EmulatorDisplayLog.display("XboxShaderActivity", "Game path null/leer. Schwarzbild möglich.")
                return
            }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Game path validated: $path")
            
            if (emulatorHandle == 0L) {
                showError("Emulator not initialized")
                EmulatorDisplayLog.display("XboxShaderActivity", "Emulator-Handle 0. Schwarzbild möglich.")
                return
            }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Emulator handle validated: $emulatorHandle")
            
            val gamePathCopy = path.toString()
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Loading game from path: $gamePathCopy")
           
            val tempFile = File(externalCacheDir, "temp.xiso")
            val extractDir = File(getExternalFilesDir(null), "xiso_extracted")
            val tempPath = tempFile.absolutePath
            val extractPath = extractDir.absolutePath

            EmulatorDisplayLog.display("XboxShaderActivity", "Using temp file: $tempPath")
            EmulatorDisplayLog.display("XboxShaderActivity", "Using extract dir: $extractPath")

            val success = withContext(Dispatchers.IO) {
                safeNativeCall { 
                    nativeLoadGameFileAndStartWithPaths(emulatorHandle, gamePathCopy, tempPath, extractPath) 
                }
            }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Game loading result: $success")
            
            if (success) {
                hideLoading()
                startGameDisplay()
            } else {
                showError("Failed to load game")
                EmulatorDisplayLog.display("XboxShaderActivity", "Game konnte nicht geladen werden. Schwarzbild möglich.")
            }
        } catch (e: Exception) {
            showError("Error loading game: "+e.message)
            EmulatorDisplayLog.display("XboxShaderActivity", "Exception beim Laden des Spiels: ${e.message}. Schwarzbild möglich.")
        }
    }
    
    private fun startGameDisplay() {
        EmulatorDisplayLog.display("XboxShaderActivity", "Starting game display")
        
        runOnUiThread {
            loadingOverlay.visibility = View.GONE
            gameDisplay.visibility = View.VISIBLE
            emulatorSurface.visibility = View.VISIBLE
        }
        
        EmulatorDisplayLog.display("XboxShaderActivity", "Surface visibility set to VISIBLE")
        
        lifecycleScope.launch(Dispatchers.IO) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Waiting for game to initialize...")
            delay(1000) 
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Starting emulation in background thread")
            safeNativeCallUnit { nativeStartEmulation(emulatorHandle) }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Emulation started successfully")
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Starting render loop to execute CPU code")
            startRenderLoop()
        }
    }
    
    private fun startRenderLoop() {
        lifecycleScope.launch(Dispatchers.IO) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Render loop started - executing CPU frames")
            
            var frameCount = 0
            try {
                while (emulatorHandle != 0L) {
                    
                    when (rendererType) {
                        RendererManager.RendererType.VULKAN -> {
                            safeNativeCallUnit { nativeRunFrame(emulatorHandle) }
                        }
                        RendererManager.RendererType.OPENGL -> {
                            safeNativeCallUnit { nativeRunFrameWithOpenGL(emulatorHandle) }
                        }
                        RendererManager.RendererType.NV2A -> {
                            safeNativeCallUnit { nativeRunFrame(emulatorHandle) }
                        }
                        else -> {
                            safeNativeCallUnit { nativeRunFrame(emulatorHandle) }
                        }
                    }
                    
                    frameCount++
                    if (frameCount % 60 == 0) { 
                        EmulatorDisplayLog.display("XboxShaderActivity", "Render loop: Executed $frameCount frames with ${rendererType?.name ?: "unknown"}")
                    }
                    
                    delay(16)
                }
            } catch (e: Exception) {
                EmulatorDisplayLog.display("XboxShaderActivity", "Render loop error: ${e.message}")
            }
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Render loop stopped after $frameCount frames")
        }
    }
    
    private fun showLoading(message: String) {
        runOnUiThread {
            loadingOverlay.visibility = View.VISIBLE
            statusText.text = message
            progressBar.visibility = View.VISIBLE
        }
    }
    
    private fun hideLoading() {
        runOnUiThread {
            loadingOverlay.visibility = View.GONE
        }
    }
    
    private fun showError(message: String) {
        runOnUiThread {
            loadingOverlay.visibility = View.GONE
            errorText.text = message
            errorText.visibility = View.VISIBLE
        }
    }
    
    override fun surfaceCreated(holder: SurfaceHolder) {
     
        EmulatorDisplayLog.display("XboxShaderActivity", "Surface created - holder: ${holder.surface}")
        
        EmulatorDisplayLog.display("XboxShaderActivity", "Surface created successfully")
        
        emulatorSurface.holder.setFixedSize(1280, 720)
        
        if (emulatorHandle != 0L) {
            lifecycleScope.launch(Dispatchers.IO) {
                EmulatorDisplayLog.display("XboxShaderActivity", "Setting OpenGL surface for emulator handle: $emulatorHandle")
                safeNativeCallUnit { nativeSetOpenGLSurface(emulatorHandle, holder.surface) }
            }
        } else {
            EmulatorDisplayLog.display("XboxShaderActivity", "Cannot set surface - emulator handle is 0 - Schwarzbild möglich")
        }
    }
    
    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        EmulatorDisplayLog.display("XboxShaderActivity", "Surface changed - width: $width, height: $height, format: $format")
        
        if (width != 1280 || height != 720) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Surface dimension mismatch! Expected: 1280x720, Got: ${width}x${height} - Schwarzbild möglich")
            
            EmulatorDisplayLog.display("XboxShaderActivity", "Attempting to correct surface dimensions")
            emulatorSurface.holder.setFixedSize(1280, 720)
        } else {
            EmulatorDisplayLog.display("XboxShaderActivity", "Surface dimensions are correct")
        }
        
        if (emulatorHandle != 0L) {
            lifecycleScope.launch(Dispatchers.IO) {
                EmulatorDisplayLog.display("XboxShaderActivity", "Notifying native code of surface change")
                safeNativeCallUnit { nativeSurfaceChanged(emulatorHandle, width, height) }
            }
        } else {
            EmulatorDisplayLog.display("XboxShaderActivity", "Cannot notify surface change - emulator handle is 0 - Schwarzbild möglich")
        }
    }
    
    override fun surfaceDestroyed(holder: SurfaceHolder) {
       
    }
    
    override fun onDestroy() {
        super.onDestroy()
        if (emulatorHandle != 0L) {
            lifecycleScope.launch(Dispatchers.IO) {
                safeNativeCallUnit { nativeShutdown(emulatorHandle) }
            }
            emulatorHandle = 0L
        }
    }
    
    override fun onBackPressed() {
        if (emulatorHandle != 0L) {
            lifecycleScope.launch(Dispatchers.IO) {
                safeNativeCallUnit { nativeStopRenderer(emulatorHandle) }
            }
        }
        super.onBackPressed()
    }
    
    private external fun nativeCreateInstance(): Long
    private external fun nativeInitEmulator(handle: Long, complexBiosPath: String, mcpxBiosPath: String, hddImagePath: String): Boolean
    private external fun nativeCreateNV2ARenderer(handle: Long): Boolean
    private external fun nativeCreateVulkanRenderer(handle: Long): Boolean
    private external fun nativeCreateOpenGLRenderer(handle: Long): Long
    private external fun nativeSetOpenGLSurface(handle: Long, surface: android.view.Surface)
    private external fun nativeSurfaceChanged(handle: Long, width: Int, height: Int)
    private external fun nativeLoadGameFileAndStart(handle: Long, gamePath: String): Boolean
    private external fun nativeStartEmulation(handle: Long)
    private external fun nativeRunFrame(handle: Long)
    private external fun nativeStopRenderer(handle: Long)
    private external fun nativeShutdown(handle: Long)
    private external fun nativeRunFrameWithOpenGL(handle: Long)
    private external fun nativeLoadGameFileAndStartWithPaths(
        handle: Long,
        gamePath: String,
        tempXisoPath: String,
        extractDir: String
    ): Boolean
    
    private fun safeNativeCall(block: () -> Boolean): Boolean {
        return try {
            block()
        } catch (e: UnsatisfiedLinkError) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Native library not loaded: ${e.message} - Schwarzbild möglich")
            showError("Native library not loaded: ${e.message}")
            false
        } catch (e: Exception) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Native call failed: ${e.message} - Schwarzbild möglich")
            showError("Native call failed: ${e.message}")
            false
        }
    }
    
    private fun safeNativeCallLong(block: () -> Long): Long {
        return try {
            block()
        } catch (e: UnsatisfiedLinkError) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Native library not loaded: ${e.message} - Schwarzbild möglich")
            showError("Native library not loaded: ${e.message}")
            0L
        } catch (e: Exception) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Native call failed: ${e.message} - Schwarzbild möglich")
            showError("Native call failed: ${e.message}")
            0L
        }
    }
    
    private fun safeNativeCallUnit(block: () -> Unit) {
        try {
            block()
        } catch (e: UnsatisfiedLinkError) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Native library not loaded: ${e.message} - Schwarzbild möglich")
            showError("Native library not loaded: ${e.message}")
        } catch (e: Exception) {
            EmulatorDisplayLog.display("XboxShaderActivity", "Native call failed: ${e.message} - Schwarzbild möglich")
            showError("Native call failed: ${e.message}")
        }
    }
}

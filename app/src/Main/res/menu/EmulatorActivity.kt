package og.xaniteog

import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class EmulatorActivity : AppCompatActivity(), SurfaceHolder.Callback {

    // ===== UI Components =====
    private lateinit var emulatorSurface: SurfaceView
    private lateinit var tvGameTitle: TextView
    
    // ===== Emulator Core =====
    private lateinit var memory: XboxMemory
    private lateinit var gpu: XboxGPU
    private lateinit var renderer: XboxRenderer
    private lateinit var mmio: XboxMMIO
    private lateinit var kernel: XboxKernel
    private lateinit var cpu: XboxCPU
    private lateinit var bios: XboxBIOS
    private lateinit var loader: XbeLoader
    
    // ===== Game Info =====
    private lateinit var gameFolder: File
    private var isEmulatorRunning = false
    private var fpsCounter = 0
    private var lastFpsTime = 0L
    private val fpsHandler = Handler(Looper.getMainLooper())
    private val emulationThreadHandler = Handler(Looper.getMainLooper())
    
    // Runnable لتحديث FPS
    private val fpsRunnable = object : Runnable {
        override fun run() {
            updateFPS()
            fpsHandler.postDelayed(this, 1000)
        }
    }
    
    // Runnable لحلقة المحاكاة
    private val emulationRunnable = object : Runnable {
        override fun run() {
            if (isEmulatorRunning) {
                emulationStep()
                emulationThreadHandler.postDelayed(this, 16) // ~60 FPS
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // وضع الشاشة landscape
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        
        // إخفاء شريط الحالة وشريط التنقل
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        )
        
        // إبقاء الشاشة مضاءة
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        
        setContentView(R.layout.activity_emulator)
        
        // الحصول على بيانات اللعبة من Intent
        val gamePath = intent.getStringExtra("GAME_PATH")
        if (gamePath != null) {
            gameFolder = File(gamePath)
            initializeUI()
            initializeEmulator()
        } else {
            finish()
        }
    }

    private fun initializeUI() {
        emulatorSurface = findViewById(R.id.emulatorSurface)
        tvGameTitle = findViewById(R.id.tvGameTitle)
        
        // Setup surface
        emulatorSurface.holder.addCallback(this)
        
        // عرض اسم اللعبة لمدة 3 ثواني ثم إخفاؤه
        tvGameTitle.text = "Loading: ${gameFolder.name}"
        tvGameTitle.visibility = View.VISIBLE
        
        // إخفاء العنوان بعد 3 ثواني
        Handler(Looper.getMainLooper()).postDelayed({
            tvGameTitle.visibility = View.GONE
        }, 3000)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        // إذا لم تكن المحاكاة تعمل، ابدأها
        if (!isEmulatorRunning) {
            startEmulation()
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        // تحديث حجم العرض
        // renderer.resize(width, height) // مؤقتاً معطلة لأنها تحتاج إلى implementation
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        // إيقاف المحاكاة
        stopEmulation()
    }

    private fun initializeEmulator() {
        try {
            // ===== تهيئة الذاكرة =====
            memory = XboxMemory()
            
            // ===== MMIO =====
            mmio = XboxMMIO()
            
            // ===== GPU =====
            gpu = XboxGPU(memory, mmio) // تعديل المعلمات حسب constructor الفعلي
            
            // ===== Renderer =====
            renderer = XboxRenderer(this, emulatorSurface) // تعديل المعلمات حسب constructor الفعلي
            
            // ===== CPU =====
            cpu = XboxCPU(memory, mmio) // تعديل المعلمات حسب constructor الفعلي
            
            // ===== Kernel =====
            kernel = XboxKernel(memory, cpu)
            
            // ===== BIOS =====
            bios = XboxBIOS(memory, cpu)
            
            // ===== Loader =====
            loader = XbeLoader(memory)
            
        } catch (e: Exception) {
            e.printStackTrace()
            finish()
        }
    }

    private fun startEmulation() {
        try {
            // البحث عن ملف XBE
            val xbeFile = findXbeFile(gameFolder)
            if (xbeFile == null) {
                runOnUiThread {
                    tvGameTitle.text = "No XBE file found"
                    tvGameTitle.visibility = View.VISIBLE
                }
                return
            }
            
            // تحميل اللعبة
            loader.load(xbeFile)
            
            // بدء المحاكاة
            isEmulatorRunning = true
            
            // بدء عد FPS
            fpsHandler.post(fpsRunnable)
            
            // بدء حلقة المحاكاة
            emulationThreadHandler.post(emulationRunnable)
            
            // تهيئة BIOS
            Thread {
                bios.initialize()
            }.start()
            
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun emulationStep() {
        try {
            // تنفيذ تعليمات CPU (مثلاً 10000 دورة)
            cpu.execute(10000)
            
            // معالجة الرسومات
            gpu.step()  // استخدم step() بدلاً من render()
            
            // تحديث MMIO
            mmio.update()
            
        } catch (e: Exception) {
            e.printStackTrace()
            stopEmulation()
        }
    }

    private fun findXbeFile(folder: File): File? {
        val files = folder.listFiles() ?: return null
        return files.firstOrNull { file ->
            file.isFile && file.name.lowercase().endsWith(".xbe")
        }
    }

    private fun stopEmulation() {
        isEmulatorRunning = false
        fpsHandler.removeCallbacks(fpsRunnable)
        emulationThreadHandler.removeCallbacks(emulationRunnable)
    }

    private fun updateFPS() {
        // يمكن تحديث عرض FPS هنا إذا كان هناك عنصر واجهة مخصص
        runOnUiThread {
            tvGameTitle.text = "FPS: $fpsCounter"
            tvGameTitle.visibility = View.VISIBLE
            // إخفاؤه بعد ثانيتين
            tvGameTitle.postDelayed({
                tvGameTitle.visibility = View.GONE
            }, 2000)
        }
        fpsCounter = 0
    }

    override fun onPause() {
        super.onPause()
        stopEmulation()
    }

    override fun onResume() {
        super.onResume()
        if (emulatorSurface.holder.surface.isValid && !isEmulatorRunning) {
            startEmulation()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopEmulation()
        fpsHandler.removeCallbacksAndMessages(null)
        emulationThreadHandler.removeCallbacksAndMessages(null)
    }
    
    // زر الرجوع يخرج من المحاكاة
    override fun onBackPressed() {
        stopEmulation()
        finish()
        overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out)
    }
}
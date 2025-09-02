package com.xanite.settings

import android.annotation.SuppressLint
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.xanite.R

class DeviceInfoActivity : AppCompatActivity() {

    @SuppressLint("SetTextI18n")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_device_info)


        findViewById<Button>(R.id.btn_close).setOnClickListener {
            finish()
        }


        val gpuInfo = getGPUInfo()
        val ramInfo = getRAMInfo()
        val processorInfo = getProcessorInfo()
        val androidVersion = Build.VERSION.RELEASE
        val deviceModel = Build.MODEL
        val manufacturer = Build.MANUFACTURER


        findViewById<TextView>(R.id.tv_gpu).text = "GPU: $gpuInfo"
        findViewById<TextView>(R.id.tv_ram).text = "RAM: $ramInfo GB"
        findViewById<TextView>(R.id.tv_processor).text = "Processor: $processorInfo"
        findViewById<TextView>(R.id.tv_android).text = "Android: $androidVersion"
        findViewById<TextView>(R.id.tv_model).text = "Model: $deviceModel"
        findViewById<TextView>(R.id.tv_manufacturer).text = "Brand: $manufacturer"
    }

    private fun getGPUInfo(): String {
        return when (Build.HARDWARE) {
            "qcom" -> "Adreno (Snapdragon)"
            "kirin" -> "Mali (HiSilicon)"
            "exynos" -> "Mali (Exynos)"
            "mt" -> "PowerVR/ARM Mali (Mediatek)"
            else -> Build.HARDWARE
        } + " " + guessGPUModel()
    }

    private fun guessGPUModel(): String {

        return when(Build.BOARD) {
            "sm8150" -> "Adreno 640 (SD 855)"
            "sm8250" -> "Adreno 650 (SD 865)"
            "sm8350" -> "Adreno 660 (SD 888)"
            else -> ""
        }
    }

    private fun getRAMInfo(): Long {
        val activityManager = getSystemService(ACTIVITY_SERVICE) as android.app.ActivityManager
        val memoryInfo = android.app.ActivityManager.MemoryInfo()
        activityManager.getMemoryInfo(memoryInfo)
        return memoryInfo.totalMem / (1024 * 1024 * 1024) 
    }

    private fun getProcessorInfo(): String {
        return Build.HARDWARE + " (" + Build.BOARD + ")"
    }
}

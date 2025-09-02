package com.xanite.overlay

import android.content.res.Resources
import com.xanite.overlay.State
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect


class PadOverlayDpad(
    resources: Resources,
    buttonWidth: Int,
    buttonHeight: Int,
    val inputId: String,
    val bounds: Rect,
    private val digital: Int,
    upBitmap: Bitmap, val upBit: Int,
    leftBitmap: Bitmap, val leftBit: Int,
    rightBitmap: Bitmap, val rightBit: Int,
    downBitmap: Bitmap, val downBit: Int,
    multitouch: Boolean
) : PadOverlayItem {

    override fun draw(canvas: android.graphics.Canvas) {}
    override fun updatePosition(x: Int, y: Int, force: Boolean) {}
    override fun startDragging(startX: Int, startY: Int) {}
    override fun stopDragging() {}
    override fun setScale(percent: Int) {}
    override fun setOpacity(percent: Int) {}
    override fun resetConfigs() {}
    override fun onTouch(event: android.view.MotionEvent, pointerIndex: Int, padState: com.xanite.overlay.State): Boolean = false
    override fun contains(x: Int, y: Int): Boolean = false
    override fun bounds(): Rect = bounds
    override var dragging: Boolean = false
    override var enabled: Boolean = true

    fun getInfo(): Triple<String, Int, Int> {
        return Triple(inputId, 50, 50) 
    }
}

package com.xanite.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import androidx.core.content.ContextCompat;
import com.xanite.R;

public class TouchControlOverlay extends View {
    private Paint controlPaint;
    private Paint buttonPaint;
    private float leftStickX, leftStickY;
    private float rightStickX, rightStickY;
    private boolean[] buttonsPressed = new boolean[10];
    
    public TouchControlOverlay(Context context) {
        super(context);
        init();
    }
    
    public TouchControlOverlay(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }
    
    private void init() {
        controlPaint = new Paint();
        controlPaint.setColor(ContextCompat.getColor(getContext(), R.color.control_stick));
        controlPaint.setAlpha(128);
        controlPaint.setStyle(Paint.Style.FILL);
        
        buttonPaint = new Paint();
        buttonPaint.setColor(ContextCompat.getColor(getContext(), R.color.control_button));
        buttonPaint.setAlpha(160);
        buttonPaint.setStyle(Paint.Style.FILL);
    }
    
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        
        int width = getWidth();
        int height = getHeight();
        
        float leftStickCenterX = width * 0.2f;
        float leftStickCenterY = height * 0.7f;
        float stickRadius = width * 0.08f;
        
        canvas.drawCircle(leftStickCenterX, leftStickCenterY, stickRadius * 1.5f, controlPaint);
        canvas.drawCircle(leftStickCenterX + leftStickX * stickRadius, 
                         leftStickCenterY + leftStickY * stickRadius, 
                         stickRadius, buttonPaint);
        
        float rightStickCenterX = width * 0.8f;
        float rightStickCenterY = height * 0.7f;
        
        canvas.drawCircle(rightStickCenterX, rightStickCenterY, stickRadius * 1.5f, controlPaint);
        canvas.drawCircle(rightStickCenterX + rightStickX * stickRadius, 
                         rightStickCenterY + rightStickY * stickRadius, 
                         stickRadius, buttonPaint);
        
        drawButton(canvas, width * 0.85f, height * 0.5f, stickRadius, buttonsPressed[0]); // A
        drawButton(canvas, width * 0.92f, height * 0.42f, stickRadius, buttonsPressed[1]); // B
        drawButton(canvas, width * 0.78f, height * 0.42f, stickRadius, buttonsPressed[2]); // X
        drawButton(canvas, width * 0.85f, height * 0.34f, stickRadius, buttonsPressed[3]); // Y
    }
    
    private void drawButton(Canvas canvas, float x, float y, float radius, boolean pressed) {
        buttonPaint.setAlpha(pressed ? 200 : 160);
        canvas.drawCircle(x, y, radius, buttonPaint);
    }
    
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        
        return true;
    }
}

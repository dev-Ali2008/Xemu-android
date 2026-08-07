package Ali.Xanite

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PointF
import android.graphics.RectF
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.SoundEffectConstants
import android.view.View
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sqrt

/** A balanced, low-overhead touchscreen layout modeled after an Xbox controller. */
class OnScreenController @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val arrowPath = Path()
    private val drawRect = RectF()
    private val buttons = linkedMapOf<Button, ButtonState>()
    private val sticks = linkedMapOf<Stick, StickState>()

    private val panelColor = Color.rgb(18, 16, 10)
    private val goldColor = Color.rgb(255, 208, 38)
    private val textColor = Color.rgb(255, 248, 232)
    private val mutedTextColor = Color.rgb(225, 215, 190)
    private val faceColors = mapOf(
        Button.CROSS to Color.rgb(55, 181, 92),
        Button.CIRCLE to Color.rgb(222, 72, 72),
        Button.SQUARE to Color.rgb(67, 139, 222),
        Button.TRIANGLE to Color.rgb(245, 193, 45),
    )

    var onMenuButtonTapped: (() -> Unit)? = null
    var scaleFactor: Float = 1.0f
        set(value) {
            field = value.coerceIn(0.5f, 2.0f)
            if (width > 0 && height > 0) initializeControls(width, height)
            invalidate()
        }

    var vibrationEnabled: Boolean = true

    private val buttonLabels = mapOf(
        Button.CROSS to "A",
        Button.CIRCLE to "B",
        Button.SQUARE to "X",
        Button.TRIANGLE to "Y",
        Button.LT to "LT",
        Button.RT to "RT",
        Button.L3 to "L3",
        Button.R3 to "R3",
        Button.BLACK to "BLK",
        Button.WHITE to "WHT",
        Button.START to "START",
        Button.SELECT to "SELECT",
        Button.MENU to "MENU",
    )

    private var controllerListener: ControllerListener? = null

    enum class Button {
        CROSS, CIRCLE, SQUARE, TRIANGLE,
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
        LT, RT, L3, R3, BLACK, WHITE, START, SELECT, MENU
    }

    enum class Stick { LEFT, RIGHT }

    data class ButtonState(
        val center: PointF,
        val radius: Float,
        var isPressed: Boolean = false,
        var activePointerId: Int = -1,
        val shape: Shape = Shape.CIRCLE,
        val width: Float = radius * 2f,
        val height: Float = radius * 2f,
    ) {
        enum class Shape { CIRCLE, ROUNDED_RECT, PILL }
    }

    data class StickState(
        val center: PointF,
        val radius: Float,
        val deadZone: Float = 0.14f,
        val currentPos: PointF = PointF(0f, 0f),
        var isActive: Boolean = false,
        var activePointerId: Int = -1,
    )

    interface ControllerListener {
        fun onButtonPressed(button: Button)
        fun onButtonReleased(button: Button)
        fun onStickMoved(stick: Stick, x: Float, y: Float)
    }

    init {
        setBackgroundColor(Color.TRANSPARENT)
        isClickable = true
        isFocusable = false
        isSoundEffectsEnabled = true
        isHapticFeedbackEnabled = true
    }

    fun setControllerListener(listener: ControllerListener) {
        controllerListener = listener
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        initializeControls(w, h)
    }

    private fun initializeControls(width: Int, height: Int) {
        buttons.clear()
        sticks.clear()

        val w = width.toFloat()
        val h = height.toFloat()
        // Base every control on the short edge. This keeps the controller
        // proportional on ultra-wide phones instead of producing giant buttons.
        val unit = min(w, h) * scaleFactor
        val triggerWidth = unit * 0.25f
        val triggerHeight = unit * 0.095f
        val utilityRadius = unit * 0.052f

        // Shoulder controls belong in two compact rows at the top. Keeping
        // them well above the stick/face-button band prevents their visual
        // and touch targets from overlapping the analog controls below.
        buttons[Button.LT] = ButtonState(
            PointF(w * 0.09f, h * 0.105f), triggerHeight / 2f,
            shape = ButtonState.Shape.ROUNDED_RECT,
            width = triggerWidth, height = triggerHeight,
        )
        buttons[Button.RT] = ButtonState(
            PointF(w * 0.91f, h * 0.105f), triggerHeight / 2f,
            shape = ButtonState.Shape.ROUNDED_RECT,
            width = triggerWidth, height = triggerHeight,
        )

        val utilityY = h * 0.25f
        val utilityGap = unit * 0.12f
        buttons[Button.BLACK] = ButtonState(PointF(w * 0.09f - utilityGap / 2f, utilityY), utilityRadius)
        buttons[Button.WHITE] = ButtonState(PointF(w * 0.09f + utilityGap / 2f, utilityY), utilityRadius)
        buttons[Button.L3] = ButtonState(PointF(w * 0.91f - utilityGap / 2f, utilityY), utilityRadius)
        buttons[Button.R3] = ButtonState(PointF(w * 0.91f + utilityGap / 2f, utilityY), utilityRadius)

        val stickRadius = unit * 0.14f
        val clusterY = h * 0.70f
        sticks[Stick.LEFT] = StickState(PointF(w * 0.13f, clusterY), stickRadius)
        sticks[Stick.RIGHT] = StickState(PointF(w * 0.67f, clusterY), stickRadius)

        val dpadCenter = PointF(w * 0.36f, clusterY)
        val dpadSpacing = unit * 0.098f
        val dpadRadius = unit * 0.050f
        val dpadSize = unit * 0.105f
        buttons[Button.DPAD_UP] = ButtonState(
            PointF(dpadCenter.x, dpadCenter.y - dpadSpacing), dpadRadius,
            shape = ButtonState.Shape.ROUNDED_RECT, width = dpadSize, height = dpadSize,
        )
        buttons[Button.DPAD_DOWN] = ButtonState(
            PointF(dpadCenter.x, dpadCenter.y + dpadSpacing), dpadRadius,
            shape = ButtonState.Shape.ROUNDED_RECT, width = dpadSize, height = dpadSize,
        )
        buttons[Button.DPAD_LEFT] = ButtonState(
            PointF(dpadCenter.x - dpadSpacing, dpadCenter.y), dpadRadius,
            shape = ButtonState.Shape.ROUNDED_RECT, width = dpadSize, height = dpadSize,
        )
        buttons[Button.DPAD_RIGHT] = ButtonState(
            PointF(dpadCenter.x + dpadSpacing, dpadCenter.y), dpadRadius,
            shape = ButtonState.Shape.ROUNDED_RECT, width = dpadSize, height = dpadSize,
        )

        val faceCenter = PointF(w * 0.88f, clusterY)
        val faceSpacing = unit * 0.115f
        val faceRadius = unit * 0.064f
        buttons[Button.CROSS] = ButtonState(PointF(faceCenter.x, faceCenter.y + faceSpacing), faceRadius)
        buttons[Button.CIRCLE] = ButtonState(PointF(faceCenter.x + faceSpacing, faceCenter.y), faceRadius)
        buttons[Button.SQUARE] = ButtonState(PointF(faceCenter.x - faceSpacing, faceCenter.y), faceRadius)
        buttons[Button.TRIANGLE] = ButtonState(PointF(faceCenter.x, faceCenter.y - faceSpacing), faceRadius)

        val centerY = h * 0.90f
        val pillWidth = unit * 0.13f
        val pillHeight = unit * 0.065f
        buttons[Button.SELECT] = ButtonState(
            PointF(w * 0.43f, centerY), pillHeight / 2f,
            shape = ButtonState.Shape.PILL, width = pillWidth, height = pillHeight,
        )
        buttons[Button.MENU] = ButtonState(
            PointF(w * 0.50f, centerY), pillHeight / 2f,
            shape = ButtonState.Shape.PILL, width = pillWidth, height = pillHeight,
        )
        buttons[Button.START] = ButtonState(
            PointF(w * 0.57f, centerY), pillHeight / 2f,
            shape = ButtonState.Shape.PILL, width = pillWidth, height = pillHeight,
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        sticks.values.forEach { drawStick(canvas, it) }
        buttons.forEach { (button, state) -> drawButton(canvas, button, state) }
    }

    private fun drawStick(canvas: Canvas, state: StickState) {
        paint.style = Paint.Style.FILL
        paint.color = withAlpha(panelColor, if (state.isActive) 205 else 150)
        canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)

        paint.style = Paint.Style.STROKE
        paint.strokeWidth = state.radius * 0.035f
        paint.color = withAlpha(if (state.isActive) goldColor else textColor, if (state.isActive) 235 else 120)
        canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)
        canvas.drawCircle(state.center.x, state.center.y, state.radius * 0.52f, paint)

        val stickX = state.center.x + state.currentPos.x * state.radius * 0.68f
        val stickY = state.center.y + state.currentPos.y * state.radius * 0.68f
        paint.style = Paint.Style.FILL
        paint.color = withAlpha(if (state.isActive) goldColor else mutedTextColor, if (state.isActive) 225 else 185)
        canvas.drawCircle(stickX, stickY, state.radius * 0.34f, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = state.radius * 0.025f
        paint.color = withAlpha(Color.WHITE, 145)
        canvas.drawCircle(stickX, stickY, state.radius * 0.34f, paint)
    }

    private fun drawButton(canvas: Canvas, button: Button, state: ButtonState) {
        val pressedScale = if (state.isPressed) 0.91f else 1f
        canvas.save()
        canvas.scale(pressedScale, pressedScale, state.center.x, state.center.y)

        val accent = faceColors[button] ?: goldColor
        paint.style = Paint.Style.FILL
        paint.color = if (state.isPressed) withAlpha(accent, 225) else withAlpha(panelColor, 165)
        drawButtonShape(canvas, state, paint)

        paint.style = Paint.Style.STROKE
        paint.strokeWidth = (state.radius * if (state.isPressed) 0.095f else 0.055f).coerceAtLeast(2f)
        paint.color = withAlpha(accent, if (state.isPressed) 255 else 205)
        drawButtonShape(canvas, state, paint)

        if (button.isDpad()) {
            drawDpadArrow(canvas, button, state, if (state.isPressed) panelColor else textColor)
        } else {
            buttonLabels[button]?.let { label ->
                paint.style = Paint.Style.FILL
                paint.color = if (state.isPressed && faceColors.containsKey(button)) Color.WHITE else textColor
                paint.textSize = when (button) {
                    Button.START, Button.SELECT, Button.MENU -> state.height * 0.32f
                    Button.LT, Button.RT -> state.height * 0.50f
                    else -> state.radius * 0.78f
                }.coerceAtLeast(12f)
                paint.textAlign = Paint.Align.CENTER
                paint.typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD)
                val baseline = state.center.y - (paint.ascent() + paint.descent()) / 2f
                canvas.drawText(label, state.center.x, baseline, paint)
            }
        }
        canvas.restore()
    }

    private fun drawButtonShape(canvas: Canvas, state: ButtonState, targetPaint: Paint) {
        when (state.shape) {
            ButtonState.Shape.CIRCLE -> canvas.drawCircle(state.center.x, state.center.y, state.radius, targetPaint)
            ButtonState.Shape.ROUNDED_RECT, ButtonState.Shape.PILL -> {
                drawRect.set(
                    state.center.x - state.width / 2f,
                    state.center.y - state.height / 2f,
                    state.center.x + state.width / 2f,
                    state.center.y + state.height / 2f,
                )
                val corner = if (state.shape == ButtonState.Shape.PILL) state.height / 2f else state.height * 0.24f
                canvas.drawRoundRect(drawRect, corner, corner, targetPaint)
            }
        }
    }

    private fun drawDpadArrow(canvas: Canvas, button: Button, state: ButtonState, color: Int) {
        val size = state.radius * 0.48f
        val cx = state.center.x
        val cy = state.center.y
        arrowPath.reset()
        when (button) {
            Button.DPAD_UP -> {
                arrowPath.moveTo(cx, cy - size)
                arrowPath.lineTo(cx - size, cy + size * 0.55f)
                arrowPath.lineTo(cx + size, cy + size * 0.55f)
            }
            Button.DPAD_DOWN -> {
                arrowPath.moveTo(cx, cy + size)
                arrowPath.lineTo(cx - size, cy - size * 0.55f)
                arrowPath.lineTo(cx + size, cy - size * 0.55f)
            }
            Button.DPAD_LEFT -> {
                arrowPath.moveTo(cx - size, cy)
                arrowPath.lineTo(cx + size * 0.55f, cy - size)
                arrowPath.lineTo(cx + size * 0.55f, cy + size)
            }
            Button.DPAD_RIGHT -> {
                arrowPath.moveTo(cx + size, cy)
                arrowPath.lineTo(cx - size * 0.55f, cy - size)
                arrowPath.lineTo(cx - size * 0.55f, cy + size)
            }
            else -> return
        }
        arrowPath.close()
        paint.style = Paint.Style.FILL
        paint.color = withAlpha(color, 220)
        canvas.drawPath(arrowPath, paint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val pointerId = event.getPointerId(pointerIndex)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN ->
                handleTouchDown(event.getX(pointerIndex), event.getY(pointerIndex), pointerId)
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    handleTouchMove(event.getX(i), event.getY(i), event.getPointerId(i))
                }
            }
            MotionEvent.ACTION_POINTER_UP -> handleTouchUp(pointerId)
            MotionEvent.ACTION_UP -> {
                handleTouchUp(pointerId)
                performClick()
            }
            MotionEvent.ACTION_CANCEL -> handleCancel()
        }
        // Coalesce rapid touch events into one redraw per display frame.
        postInvalidateOnAnimation()
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun handleTouchDown(x: Float, y: Float, pointerId: Int) {
        for ((stick, state) in sticks) {
            if (state.activePointerId == -1 && isPointInCircle(x, y, state.center, state.radius * 1.12f)) {
                state.activePointerId = pointerId
                state.isActive = true
                if (vibrationEnabled) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                updateStickPosition(stick, state, x, y)
                return
            }
        }
        for ((button, state) in buttons) {
            if (state.activePointerId == -1 && isInsideButton(x, y, state, 1.16f)) {
                pressButton(button, state, pointerId)
                return
            }
        }
    }

    private fun handleTouchMove(x: Float, y: Float, pointerId: Int) {
        for ((stick, state) in sticks) {
            if (state.activePointerId == pointerId) {
                updateStickPosition(stick, state, x, y)
                return
            }
        }
        for ((button, state) in buttons) {
            if (state.activePointerId == pointerId && !isInsideButton(x, y, state, 1.28f)) {
                releaseButton(button, state)
                return
            }
        }
    }

    private fun handleTouchUp(pointerId: Int) {
        for ((stick, state) in sticks) {
            if (state.activePointerId == pointerId) {
                state.activePointerId = -1
                state.isActive = false
                state.currentPos.set(0f, 0f)
                resetPreviousStick(stick)
                controllerListener?.onStickMoved(stick, 0f, 0f)
                return
            }
        }
        buttons.forEach { (button, state) ->
            if (state.activePointerId == pointerId) releaseButton(button, state)
        }
    }

    private fun handleCancel() {
        sticks.forEach { (stick, state) ->
            if (state.activePointerId != -1) {
                state.activePointerId = -1
                state.isActive = false
                state.currentPos.set(0f, 0f)
                resetPreviousStick(stick)
                controllerListener?.onStickMoved(stick, 0f, 0f)
            }
        }
        buttons.forEach { (button, state) ->
            if (state.isPressed) releaseButton(button, state)
        }
    }

    private fun pressButton(button: Button, state: ButtonState, pointerId: Int) {
        state.isPressed = true
        state.activePointerId = pointerId
        playSoundEffect(SoundEffectConstants.CLICK)
        if (vibrationEnabled) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        if (button != Button.MENU) controllerListener?.onButtonPressed(button)
    }

    private fun releaseButton(button: Button, state: ButtonState) {
        val wasPressed = state.isPressed
        state.isPressed = false
        state.activePointerId = -1
        if (button == Button.MENU) {
            if (wasPressed) post { onMenuButtonTapped?.invoke() }
        } else {
            controllerListener?.onButtonReleased(button)
        }
    }

    private var prevLeftX = 0f
    private var prevLeftY = 0f
    private var prevRightX = 0f
    private var prevRightY = 0f

    private fun updateStickPosition(stick: Stick, state: StickState, x: Float, y: Float) {
        val dx = x - state.center.x
        val dy = y - state.center.y
        val distance = sqrt(dx.pow(2) + dy.pow(2))
        val rawX = if (distance > state.radius) dx / distance else dx / state.radius
        val rawY = if (distance > state.radius) dy / distance else dy / state.radius
        val magnitude = sqrt(rawX.pow(2) + rawY.pow(2)).coerceAtMost(1f)

        if (magnitude < state.deadZone) {
            if (state.currentPos.x != 0f || state.currentPos.y != 0f) {
                state.currentPos.set(0f, 0f)
                resetPreviousStick(stick)
                controllerListener?.onStickMoved(stick, 0f, 0f)
            }
            return
        }

        // Preserve analog magnitude after removing the dead zone. The previous
        // implementation normalized every touch to 100%, which made games feel
        // twitchy and unstable even with tiny thumb movements.
        val adjustedMagnitude = ((magnitude - state.deadZone) / (1f - state.deadZone)).coerceIn(0f, 1f)
        val targetX = rawX / magnitude * adjustedMagnitude
        val targetY = rawY / magnitude * adjustedMagnitude
        val (prevX, prevY) = when (stick) {
            Stick.LEFT -> prevLeftX to prevLeftY
            Stick.RIGHT -> prevRightX to prevRightY
        }
        val smoothFactor = 0.42f
        val outX = prevX + (targetX - prevX) * smoothFactor
        val outY = prevY + (targetY - prevY) * smoothFactor

        when (stick) {
            Stick.LEFT -> { prevLeftX = outX; prevLeftY = outY }
            Stick.RIGHT -> { prevRightX = outX; prevRightY = outY }
        }
        state.currentPos.set(outX, outY)

        // Avoid flooding JNI/SDL with numerically identical motion events.
        if (abs(outX - prevX) > 0.002f || abs(outY - prevY) > 0.002f) {
            controllerListener?.onStickMoved(stick, outX, outY)
        }
    }

    private fun resetPreviousStick(stick: Stick) {
        when (stick) {
            Stick.LEFT -> { prevLeftX = 0f; prevLeftY = 0f }
            Stick.RIGHT -> { prevRightX = 0f; prevRightY = 0f }
        }
    }

    private fun isInsideButton(x: Float, y: Float, state: ButtonState, expansion: Float): Boolean {
        return when (state.shape) {
            ButtonState.Shape.CIRCLE -> isPointInCircle(x, y, state.center, state.radius * expansion)
            ButtonState.Shape.ROUNDED_RECT, ButtonState.Shape.PILL ->
                isPointInRect(x, y, state.center, state.width * expansion / 2f, state.height * expansion / 2f)
        }
    }

    private fun isPointInCircle(x: Float, y: Float, center: PointF, radius: Float): Boolean {
        val dx = x - center.x
        val dy = y - center.y
        return dx * dx + dy * dy <= radius * radius
    }

    private fun isPointInRect(x: Float, y: Float, center: PointF, halfW: Float, halfH: Float): Boolean {
        return x >= center.x - halfW && x <= center.x + halfW &&
            y >= center.y - halfH && y <= center.y + halfH
    }

    private fun Button.isDpad(): Boolean =
        this == Button.DPAD_UP || this == Button.DPAD_DOWN ||
            this == Button.DPAD_LEFT || this == Button.DPAD_RIGHT

    private fun withAlpha(color: Int, alpha: Int): Int =
        Color.argb(alpha.coerceIn(0, 255), Color.red(color), Color.green(color), Color.blue(color))

    fun setVisibility(visible: Boolean) {
        visibility = if (visible) VISIBLE else GONE
    }

    fun resetAllInputs() {
        handleCancel()
        invalidate()
    }

    override fun onDetachedFromWindow() {
        resetAllInputs()
        super.onDetachedFromWindow()
    }

    override fun onVisibilityChanged(changedView: View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        if (changedView === this && visibility != VISIBLE) resetAllInputs()
    }
}

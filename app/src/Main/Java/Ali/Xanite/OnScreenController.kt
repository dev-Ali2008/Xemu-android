package Ali.Xanite

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PointF
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.pow
import kotlin.math.sqrt

class OnScreenController @JvmOverloads constructor(
  context: Context,
  attrs: AttributeSet? = null,
  defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

  private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
  private val buttons = mutableMapOf<Button, ButtonState>()
  private val sticks = mutableMapOf<Stick, StickState>()

  private var controllerListener: ControllerListener? = null

  enum class Button {
  
    CROSS, CIRCLE, SQUARE, TRIANGLE,
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
    LT, RT,
    L3, R3,      
    BLACK, WHITE, 
    START, SELECT
  }

  enum class Stick {
    LEFT, RIGHT   
  }

  data class ButtonState(
    val center: PointF,
    val radius: Float,
    var isPressed: Boolean = false,
    var activePointerId: Int = -1,
    var shape: Shape = Shape.CIRCLE  
  ) {
    enum class Shape { CIRCLE, RECT }
  }

  data class StickState(
    val center: PointF,
    val radius: Float,
    val deadZone: Float = 0.15f,
    var currentPos: PointF = PointF(0f, 0f),
    var isPressed: Boolean = false,
    var activePointerId: Int = -1
  )

  interface ControllerListener {
    fun onButtonPressed(button: Button)
    fun onButtonReleased(button: Button)
    fun onStickMoved(stick: Stick, x: Float, y: Float)
    fun onStickPressed(stick: Stick)
    fun onStickReleased(stick: Stick)
  }

  init {
    setBackgroundColor(Color.TRANSPARENT)
  }

  fun setControllerListener(listener: ControllerListener) {
    this.controllerListener = listener
  }

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    initializeControls(w, h)
  }

  private fun initializeControls(width: Int, height: Int) {
    val w = width.toFloat()
    val h = height.toFloat()

    val leftX = w * 0.15f

    buttons[Button.LT] = ButtonState(
      center = PointF(leftX, h * 0.1f),
      radius = w * 0.045f,
      shape = ButtonState.Shape.RECT
    )

    buttons[Button.BLACK] = ButtonState(
      center = PointF(leftX - w * 0.03f, h * 0.3f), 
      radius = w * 0.028f
    )
    buttons[Button.WHITE] = ButtonState(
      center = PointF(leftX + w * 0.03f, h * 0.3f), 
      radius = w * 0.028f
    )

    sticks[Stick.LEFT] = StickState(
      center = PointF(leftX - w * 0.02f, h * 0.75f),
      radius = w * 0.085f  
    )

    val dpadCenter = PointF(leftX + w * 0.20f, h * 0.75f)  
    val dpadSpacing = w * 0.045f
    val dpadRadius = w * 0.028f
    buttons[Button.DPAD_UP] = ButtonState(
      center = PointF(dpadCenter.x, dpadCenter.y - dpadSpacing),
      radius = dpadRadius
    )
    buttons[Button.DPAD_DOWN] = ButtonState(
      center = PointF(dpadCenter.x, dpadCenter.y + dpadSpacing),
      radius = dpadRadius
    )
    buttons[Button.DPAD_LEFT] = ButtonState(
      center = PointF(dpadCenter.x - dpadSpacing, dpadCenter.y),
      radius = dpadRadius
    )
    buttons[Button.DPAD_RIGHT] = ButtonState(
      center = PointF(dpadCenter.x + dpadSpacing, dpadCenter.y),
      radius = dpadRadius
    )

    val rightX = w * 0.85f

    buttons[Button.RT] = ButtonState(
      center = PointF(rightX, h * 0.1f),
      radius = w * 0.045f,
      shape = ButtonState.Shape.RECT
    )

    val r3L3Y = h * 0.3f 
    buttons[Button.R3] = ButtonState(
      center = PointF(rightX + w * 0.035f, r3L3Y),
      radius = w * 0.025f
    )
    buttons[Button.L3] = ButtonState(
      center = PointF(rightX - w * 0.035f, r3L3Y),
      radius = w * 0.025f
    )

    val faceCenterX = rightX + w * 0.03f
    val faceCenterY = h * 0.75f
    val faceSpacing = w * 0.065f
    val faceRadius = w * 0.04f

    buttons[Button.CROSS] = ButtonState(
      center = PointF(faceCenterX, faceCenterY + faceSpacing),
      radius = faceRadius
    )
    buttons[Button.CIRCLE] = ButtonState(
      center = PointF(faceCenterX + faceSpacing, faceCenterY),
      radius = faceRadius
    )
    buttons[Button.SQUARE] = ButtonState(
      center = PointF(faceCenterX - faceSpacing, faceCenterY),
      radius = faceRadius
    )
    buttons[Button.TRIANGLE] = ButtonState(
      center = PointF(faceCenterX, faceCenterY - faceSpacing),
      radius = faceRadius
    )

    sticks[Stick.RIGHT] = StickState(
      center = PointF(faceCenterX - w * 0.18f, faceCenterY),
      radius = w * 0.065f
    )

    val bottomY = h * 0.94f
    val startSelectSpacing = w * 0.09f
    val centerX = w * 0.5f
    buttons[Button.SELECT] = ButtonState(
      center = PointF(centerX - startSelectSpacing, bottomY),
      radius = w * 0.028f
    )
    buttons[Button.START] = ButtonState(
      center = PointF(centerX + startSelectSpacing, bottomY),
      radius = w * 0.028f
    )
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)

    val normalColor = Color.argb(180, 200, 200, 200)
    val pressedColor = Color.argb(220, 255, 255, 255)
    val outlineColor = Color.argb(100, 255, 255, 255)

    sticks.forEach { (stick, state) ->

      paint.style = Paint.Style.STROKE
      paint.strokeWidth = 4f
      paint.color = outlineColor
      canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)

      paint.strokeWidth = 2f
      canvas.drawLine(state.center.x - state.radius * 0.3f, state.center.y,
                      state.center.x + state.radius * 0.3f, state.center.y, paint)
      canvas.drawLine(state.center.x, state.center.y - state.radius * 0.3f,
                      state.center.x, state.center.y + state.radius * 0.3f, paint)


      val stickX = state.center.x + state.currentPos.x * state.radius * 0.8f
      val stickY = state.center.y + state.currentPos.y * state.radius * 0.8f
      paint.style = Paint.Style.FILL
      paint.color = if (state.isPressed) pressedColor else normalColor
      canvas.drawCircle(stickX, stickY, state.radius * 0.4f, paint)
    }

    buttons.forEach { (button, state) ->
      val color = if (state.isPressed) pressedColor else normalColor
      paint.style = Paint.Style.FILL
      paint.color = color

      when (state.shape) {
        ButtonState.Shape.CIRCLE -> {
          canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)

          paint.style = Paint.Style.STROKE
          paint.strokeWidth = 2f
          paint.color = outlineColor
          canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)
        }
        ButtonState.Shape.RECT -> {
          val halfW = state.radius * 1.3f
          val halfH = state.radius * 0.9f
          val rect = RectF(
            state.center.x - halfW,
            state.center.y - halfH,
            state.center.x + halfW,
            state.center.y + halfH
          )
          canvas.drawRoundRect(rect, 15f, 15f, paint)

          paint.style = Paint.Style.STROKE
          paint.strokeWidth = 2f
          paint.color = outlineColor
          canvas.drawRoundRect(rect, 15f, 15f, paint)
        }
      }

      paint.style = Paint.Style.FILL
      paint.color = Color.argb(200, 0, 0, 0)
      paint.textSize = state.radius * 0.8f
      paint.textAlign = Paint.Align.CENTER
      val label = getButtonLabel(button)
      if (label.isNotEmpty()) {
        canvas.drawText(label, state.center.x, state.center.y + state.radius * 0.3f, paint)
      }
    }
  }

  private fun getButtonLabel(button: Button): String {
    return when (button) {
      Button.CROSS -> "A"
      Button.CIRCLE -> "B"
      Button.SQUARE -> "X"
      Button.TRIANGLE -> "Y"
      Button.DPAD_UP -> "▲"
      Button.DPAD_DOWN -> "▼"
      Button.DPAD_LEFT -> "◀"
      Button.DPAD_RIGHT -> "▶"
      Button.LT -> "LT"
      Button.RT -> "RT"
      Button.L3 -> "L3"
      Button.R3 -> "R3"
      Button.BLACK -> "BL"
      Button.WHITE -> "WH"
      Button.START -> "START"
      Button.SELECT -> "SELECT"
    }
  }

  override fun onTouchEvent(event: MotionEvent): Boolean {
    val pointerIndex = event.actionIndex
    val pointerId = event.getPointerId(pointerIndex)
    val x = event.getX(pointerIndex)
    val y = event.getY(pointerIndex)

    when (event.actionMasked) {
      MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
        handleTouchDown(x, y, pointerId)
      }
      MotionEvent.ACTION_MOVE -> {
        for (i in 0 until event.pointerCount) {
          handleTouchMove(
            event.getX(i),
            event.getY(i),
            event.getPointerId(i)
          )
        }
      }
      MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
        handleTouchUp(pointerId)
      }
      MotionEvent.ACTION_CANCEL -> {
        handleCancel()
      }
    }

    invalidate()
    return true
  }

  private fun handleTouchDown(x: Float, y: Float, pointerId: Int) {

    sticks.forEach { (stick, state) ->
      if (state.activePointerId == -1 && isPointInCircle(x, y, state.center, state.radius * 1.3f)) {
        state.activePointerId = pointerId
        updateStickPosition(stick, state, x, y)
        return
      }
    }

    buttons.forEach { (button, state) ->
      val hit = when (state.shape) {
        ButtonState.Shape.CIRCLE -> isPointInCircle(x, y, state.center, state.radius * 1.3f)
        ButtonState.Shape.RECT -> isPointInRect(x, y, state.center, state.radius * 1.3f, state.radius * 0.9f)
      }
      if (state.activePointerId == -1 && hit) {
        state.isPressed = true
        state.activePointerId = pointerId
        controllerListener?.onButtonPressed(button)
        return
      }
    }
  }

  private fun handleTouchMove(x: Float, y: Float, pointerId: Int) {
    sticks.forEach { (stick, state) ->
      if (state.activePointerId == pointerId) {
        updateStickPosition(stick, state, x, y)
      }
    }
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
    if (changedView === this && visibility != View.VISIBLE) {
        resetAllInputs()
    }
}

  private fun handleTouchUp(pointerId: Int) {
    sticks.forEach { (stick, state) ->
      if (state.activePointerId == pointerId) {
        state.activePointerId = -1
        state.currentPos = PointF(0f, 0f)
        controllerListener?.onStickMoved(stick, 0f, 0f)
        if (state.isPressed) {
          state.isPressed = false
          controllerListener?.onStickReleased(stick)
        }
      }
    }

    buttons.forEach { (button, state) ->
      if (state.activePointerId == pointerId) {
        state.isPressed = false
        state.activePointerId = -1
        controllerListener?.onButtonReleased(button)
      }
    }
  }

  private fun handleCancel() {
    sticks.forEach { (stick, state) ->
      if (state.activePointerId != -1) {
        state.activePointerId = -1
        state.currentPos = PointF(0f, 0f)
        controllerListener?.onStickMoved(stick, 0f, 0f)
        if (state.isPressed) {
          state.isPressed = false
          controllerListener?.onStickReleased(stick)
        }
      }
    }

    buttons.forEach { (button, state) ->
      if (state.isPressed) {
        state.isPressed = false
        state.activePointerId = -1
        controllerListener?.onButtonReleased(button)
      }
    }
  }

  private fun updateStickPosition(stick: Stick, state: StickState, x: Float, y: Float) {
    val dx = x - state.center.x
    val dy = y - state.center.y
    val distance = sqrt(dx.pow(2) + dy.pow(2))

    if (distance > state.radius) {
      state.currentPos.x = (dx / distance).coerceIn(-1f, 1f)
      state.currentPos.y = (dy / distance).coerceIn(-1f, 1f)
    } else {
      state.currentPos.x = (dx / state.radius).coerceIn(-1f, 1f)
      state.currentPos.y = (dy / state.radius).coerceIn(-1f, 1f)
    }

    val magnitude = sqrt(state.currentPos.x.pow(2) + state.currentPos.y.pow(2))
    if (magnitude < state.deadZone) {
      state.currentPos.x = 0f
      state.currentPos.y = 0f
    }

    controllerListener?.onStickMoved(stick, state.currentPos.x, state.currentPos.y)

    if (magnitude > 0.9f && !state.isPressed) {
      state.isPressed = true
      controllerListener?.onStickPressed(stick)
    } else if (magnitude < 0.5f && state.isPressed) {
      state.isPressed = false
      controllerListener?.onStickReleased(stick)
    }
  }

  private fun isPointInCircle(x: Float, y: Float, center: PointF, radius: Float): Boolean {
    val dx = x - center.x
    val dy = y - center.y
    return sqrt(dx.pow(2) + dy.pow(2)) <= radius
  }

  private fun isPointInRect(x: Float, y: Float, center: PointF, halfW: Float, halfH: Float): Boolean {
    return x >= center.x - halfW && x <= center.x + halfW &&
           y >= center.y - halfH && y <= center.y + halfH
  }

  fun setVisibility(visible: Boolean) {
    visibility = if (visible) View.VISIBLE else View.GONE
  }
}

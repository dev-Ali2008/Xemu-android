package Ali.Xanite

import android.view.MotionEvent
import kotlin.math.abs
import kotlin.math.max

class SwipeUpGestureRecognizer(
  private val minDistancePx: () -> Float,
  private val touchSlopPx: () -> Float,
  private val canStartAt: (x: Float, y: Float) -> Boolean,
  private val onTriggered: () -> Unit,
) {
  private var activePointerId = INVALID_POINTER_ID
  private var startX = 0f
  private var startY = 0f
  private var tracking = false
  private var triggered = false

  fun onTouchEvent(event: MotionEvent): Boolean {
    if (triggered) {
      when (event.actionMasked) {
        MotionEvent.ACTION_UP,
        MotionEvent.ACTION_CANCEL -> reset()
        MotionEvent.ACTION_POINTER_UP -> {
          if (event.getPointerId(event.actionIndex) == activePointerId) {
            reset()
          }
        }
      }
      return true
    }

    when (event.actionMasked) {
      MotionEvent.ACTION_DOWN -> {
        reset()
        val x = event.getX(0)
        val y = event.getY(0)
        if (canStartAt(x, y)) {
          activePointerId = event.getPointerId(0)
          startX = x
          startY = y
          tracking = true
        }
      }

      MotionEvent.ACTION_POINTER_DOWN -> {
        reset()
      }

      MotionEvent.ACTION_MOVE -> {
        if (!tracking) {
          return false
        }

        val pointerIndex = event.findPointerIndex(activePointerId)
        if (pointerIndex < 0) {
          reset()
          return false
        }

        val x = event.getX(pointerIndex)
        val y = event.getY(pointerIndex)
        val dx = x - startX
        val dy = y - startY
        val upwardDistance = -dy
        val slop = touchSlopPx()

        if (dy > slop || abs(dx) > max(slop * 5f, upwardDistance * 0.9f + slop)) {
          reset()
          return false
        }

        if (upwardDistance >= minDistancePx() &&
          upwardDistance >= abs(dx) * 1.35f) {
          triggered = true
          tracking = false
          onTriggered()
          return true
        }
      }

      MotionEvent.ACTION_POINTER_UP -> {
        if (event.getPointerId(event.actionIndex) == activePointerId) {
          reset()
        }
      }

      MotionEvent.ACTION_UP,
      MotionEvent.ACTION_CANCEL -> reset()
    }

    return false
  }

  fun reset() {
    activePointerId = INVALID_POINTER_ID
    startX = 0f
    startY = 0f
    tracking = false
    triggered = false
  }

  private companion object {
    const val INVALID_POINTER_ID = -1
  }
}

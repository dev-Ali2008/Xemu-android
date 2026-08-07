package Ali.Xanite

import android.content.Context
import android.os.Bundle
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.SoundEffectConstants
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import kotlin.math.abs

abstract class BaseActivity : AppCompatActivity() {
  private var feedbackTarget: View? = null
  private var feedbackTargetSoundEnabled = true
  private var feedbackDownX = 0f
  private var feedbackDownY = 0f

  override fun attachBaseContext(newBase: Context) {
    super.attachBaseContext(LocaleHelper.applyLocale(newBase))
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    applyPreferredUiOrientation()
    window.decorView.isSoundEffectsEnabled = true
  }

  /**
   * Normal Android views only make a sound when the device-wide touch-sound
   * option is enabled, and they provide no consistent tactile response. Keep
   * one restrained system click plus a short tap haptic across every app menu.
   */
  override fun dispatchTouchEvent(event: MotionEvent): Boolean {
    when (event.actionMasked) {
      MotionEvent.ACTION_DOWN -> {
        restoreFeedbackTarget()
        feedbackDownX = event.rawX
        feedbackDownY = event.rawY
        feedbackTarget = findClickableView(window.decorView, event.rawX, event.rawY)
        feedbackTarget?.let { target ->
          feedbackTargetSoundEnabled = target.isSoundEffectsEnabled
          // Suppress View.performClick's automatic sound so the explicit click
          // below cannot be doubled on devices with touch sounds enabled.
          target.isSoundEffectsEnabled = false
        }
      }
      MotionEvent.ACTION_MOVE -> {
        val slop = ViewConfiguration.get(this).scaledTouchSlop
        if (abs(event.rawX - feedbackDownX) > slop || abs(event.rawY - feedbackDownY) > slop) {
          restoreFeedbackTarget()
        }
      }
    }

    val handled = super.dispatchTouchEvent(event)

    when (event.actionMasked) {
      MotionEvent.ACTION_UP -> {
        val target = feedbackTarget
        val shouldRespond = target != null && target.isEnabled &&
          isPointInside(target, event.rawX, event.rawY)
        restoreFeedbackTarget()
        if (shouldRespond) {
          window.decorView.playSoundEffect(SoundEffectConstants.CLICK)
          target?.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        }
      }
      MotionEvent.ACTION_CANCEL -> restoreFeedbackTarget()
    }
    return handled
  }

  override fun onResume() {
    super.onResume()
    // Re-read the preference when returning from SettingsActivity. Android
    // recreates activities that need a different portrait/landscape layout.
    applyPreferredUiOrientation()
  }

  protected fun applyPreferredUiOrientation() {
    val preferred = OrientationPreferences.getUiRequestedOrientation(this)
    if (requestedOrientation != preferred) {
      requestedOrientation = preferred
    }
  }

  private fun restoreFeedbackTarget() {
    feedbackTarget?.isSoundEffectsEnabled = feedbackTargetSoundEnabled
    feedbackTarget = null
    feedbackTargetSoundEnabled = true
  }

  private fun findClickableView(view: View, rawX: Float, rawY: Float): View? {
    if (view.visibility != View.VISIBLE || !isPointInside(view, rawX, rawY)) return null
    if (view is ViewGroup) {
      for (index in view.childCount - 1 downTo 0) {
        findClickableView(view.getChildAt(index), rawX, rawY)?.let { return it }
      }
    }
    return view.takeIf { it.isClickable && it.isEnabled }
  }

  private fun isPointInside(view: View, rawX: Float, rawY: Float): Boolean {
    val location = IntArray(2)
    view.getLocationOnScreen(location)
    return rawX >= location[0] && rawX < location[0] + view.width &&
      rawY >= location[1] && rawY < location[1] + view.height
  }
}

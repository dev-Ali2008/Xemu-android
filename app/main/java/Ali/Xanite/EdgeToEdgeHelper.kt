package Ali.Xanite

import android.app.Activity
import android.view.View
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat

internal object EdgeToEdgeHelper {
  fun enable(activity: Activity) {
    WindowCompat.setDecorFitsSystemWindows(activity.window, false)
  }

  fun applySystemBarPadding(view: View) {
    val initialLeft = view.paddingLeft
    val initialTop = view.paddingTop
    val initialRight = view.paddingRight
    val initialBottom = view.paddingBottom

    ViewCompat.setOnApplyWindowInsetsListener(view) { target, insets ->
      val bars = insets.getInsets(
        WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
      )
      target.setPadding(
        initialLeft + bars.left,
        initialTop + bars.top,
        initialRight + bars.right,
        initialBottom + bars.bottom
      )
      insets
    }
    ViewCompat.requestApplyInsets(view)
  }
}

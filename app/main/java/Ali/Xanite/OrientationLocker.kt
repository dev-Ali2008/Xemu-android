package Ali.Xanite

import android.app.Activity
import android.content.pm.ActivityInfo

class OrientationLocker(private val activity: Activity, private val landscapeOnly: Boolean = false) {
  fun enable() {
    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
  }

  fun disable() {
  }
}

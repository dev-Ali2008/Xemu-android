package Ali.Xanite

import android.os.Bundle
import android.text.method.LinkMovementMethod
import android.view.ViewGroup
import android.widget.ScrollView
import android.widget.TextView

class PrivacyPolicyActivity : BaseActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    title = getString(R.string.privacy_policy_title)

    val body = TextView(this).apply {
      text = getString(R.string.privacy_policy_body)
      textSize = 16f
      setTextColor(0xFFFFF6EB.toInt())
      setPadding(48, 48, 48, 64)
      movementMethod = LinkMovementMethod.getInstance()
    }
    val scroll = ScrollView(this).apply {
      setBackgroundColor(0xFF0D0B08.toInt())
      // Keep the text scrolling under the padded edges rather than being
      // boxed in by them.
      clipToPadding = false
      addView(
        body,
        ViewGroup.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.WRAP_CONTENT,
        ),
      )
    }
    setContentView(scroll)

    // Unlike every other screen this one does not go immersive, so with
    // targetSdk 36 (edge-to-edge is enforced from Android 15) the first lines
    // of the policy rendered under the status bar and the last under the
    // gesture bar. Landscape is where it bit hardest, since the little
    // vertical space there is was being eaten at both ends.
    EdgeToEdgeHelper.enable(this)
    EdgeToEdgeHelper.applySystemBarPadding(scroll)
  }
}

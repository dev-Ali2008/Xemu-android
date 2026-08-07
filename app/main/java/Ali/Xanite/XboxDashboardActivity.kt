package Ali.Xanite

import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.view.animation.DecelerateInterpolator
import android.view.animation.ScaleAnimation
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import com.google.android.material.button.MaterialButton

class XboxDashboardActivity : BaseActivity() {

    private lateinit var menuGameLibrary: FrameLayout
    private lateinit var menuXboxLive: FrameLayout
    private lateinit var menuSettings: FrameLayout
    private lateinit var orientationButton: MaterialButton

    private val menuItems get() = listOf(menuGameLibrary, menuXboxLive, menuSettings)
    private val menuLabels get() = menuItems.map { it.getChildAt(1) as? TextView }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        EdgeToEdgeHelper.enable(this)
        setContentView(R.layout.activity_xbox_dashboard)
        enableFullScreen()
        adaptLayoutForOrientation()

        menuGameLibrary = findViewById(R.id.menu_game_library)
        menuXboxLive    = findViewById(R.id.menu_xbox_live)
        menuSettings    = findViewById(R.id.menu_settings)
        orientationButton = findViewById(R.id.btn_dashboard_orientation)
        updateOrientationButton()

        orientationButton.setOnClickListener {
            val next = when (OrientationPreferences.getUiOrientation(this)) {
                OrientationPreferences.UiOrientation.FOLLOW_DEVICE ->
                    OrientationPreferences.UiOrientation.PORTRAIT
                OrientationPreferences.UiOrientation.PORTRAIT,
                OrientationPreferences.UiOrientation.REVERSE_PORTRAIT ->
                    OrientationPreferences.UiOrientation.LANDSCAPE
                OrientationPreferences.UiOrientation.LANDSCAPE,
                OrientationPreferences.UiOrientation.REVERSE_LANDSCAPE ->
                    OrientationPreferences.UiOrientation.FOLLOW_DEVICE
            }
            OrientationPreferences.setUiOrientation(this, next)
            updateOrientationButton()
            requestedOrientation = next.requestedOrientation
        }

        menuItems.forEachIndexed { i, item ->
            item.alpha = 0f
            item.translationX = 150f
            item.animate()
                .alpha(1f)
                .translationX(0f)
                .setStartDelay((i * 100).toLong())
                .setDuration(350)
                .setInterpolator(DecelerateInterpolator())
                .start()
        }

        startLogoPulse()

        menuGameLibrary.setOnClickListener {
            startActivity(Intent(this, GameLibraryActivity::class.java))
            overridePendingTransition(R.anim.slide_in_right, R.anim.fade_out)
        }

        menuXboxLive.setOnClickListener {
            // Insignia setup is the useful destination here: it is the service
            // that actually works on original Xbox. System Link is reachable
            // from a link inside it.
            startActivity(Intent(this, InsigniaSetupActivity::class.java))
            overridePendingTransition(R.anim.slide_in_right, R.anim.fade_out)
        }

        menuSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
            overridePendingTransition(R.anim.slide_in_right, R.anim.fade_out)
        }
    }

    override fun finish() {
        super.finish()
        overridePendingTransition(R.anim.fade_in, R.anim.slide_out_right)
    }

    override fun onResume() {
        super.onResume()
        if (::orientationButton.isInitialized) {
            updateOrientationButton()
        }
    }

    private fun updateOrientationButton() {
        val (labelRes, iconRes) = when (OrientationPreferences.getUiOrientation(this)) {
            OrientationPreferences.UiOrientation.FOLLOW_DEVICE ->
                R.string.dashboard_orientation_auto to R.drawable.ic_orientation_auto
            OrientationPreferences.UiOrientation.PORTRAIT,
            OrientationPreferences.UiOrientation.REVERSE_PORTRAIT ->
                R.string.dashboard_orientation_portrait to R.drawable.ic_orientation_portrait
            OrientationPreferences.UiOrientation.LANDSCAPE,
            OrientationPreferences.UiOrientation.REVERSE_LANDSCAPE ->
                R.string.dashboard_orientation_landscape to R.drawable.ic_orientation_landscape
        }
        val label = getString(labelRes)
        orientationButton.setText(labelRes)
        orientationButton.setIconResource(iconRes)
        orientationButton.contentDescription =
            getString(R.string.dashboard_orientation_description, label)
    }

    private fun startLogoPulse() {
        val logo = findViewById<ImageView>(R.id.iv_xbox_logo)
        val pulse = ScaleAnimation(
            1f, 1.06f,
            1f, 1.06f,
            ScaleAnimation.RELATIVE_TO_SELF, 0.5f,
            ScaleAnimation.RELATIVE_TO_SELF, 0.5f
        ).apply {
            duration = 1800
            repeatMode = ScaleAnimation.REVERSE
            repeatCount = ScaleAnimation.INFINITE
            interpolator = DecelerateInterpolator()
        }
        logo.startAnimation(pulse)
    }

    private fun adaptLayoutForOrientation() {
        if (resources.configuration.orientation != Configuration.ORIENTATION_PORTRAIT) {
            return
        }

        val content = findViewById<LinearLayout>(R.id.dashboard_content)
        val logoContainer = findViewById<LinearLayout>(R.id.dashboard_logo_container)
        val menuContainer = findViewById<LinearLayout>(R.id.dashboard_menu_container)
        val logo = findViewById<ImageView>(R.id.iv_xbox_logo)
        val density = resources.displayMetrics.density
        fun dp(value: Int) = (value * density).toInt()

        content.orientation = LinearLayout.VERTICAL
        content.gravity = Gravity.CENTER
        content.setPadding(dp(24), dp(20), dp(24), dp(68))
        logoContainer.layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            0,
            1f,
        )
        menuContainer.layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
        )
        logo.layoutParams = LinearLayout.LayoutParams(dp(164), dp(164))
    }
}

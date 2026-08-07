package Ali.Xanite

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.Toast
import android.view.ViewGroup
import android.view.animation.DecelerateInterpolator
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentActivity
import androidx.viewpager2.adapter.FragmentStateAdapter
import androidx.viewpager2.widget.ViewPager2
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class SetupWizardActivity : BaseActivity() {

    private lateinit var pager: ViewPager2
    private lateinit var btnBack: MaterialButton
    private lateinit var btnContinue: MaterialButton
    private lateinit var orientationToggle: MaterialButtonToggleGroup
    private lateinit var dots: Array<View>

    // Track readiness from SharedPreferences on resume
    val mcpxReady  get() = hasUri("mcpxUri")  || hasPath("mcpxPath")
    val flashReady get() = hasUri("flashUri") || hasPath("flashPath")
    val hddReady   get() = hasUri("hddUri")   || hasPath("hddPath")
    val discReady  get() = hasUri("gamesFolderUri")

    companion object {
        const val TOTAL_STEPS = 4
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // If setup already complete, skip straight to dashboard
        if (getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)
                .getBoolean("setup_complete", false)) {
            startActivity(Intent(this, XboxDashboardActivity::class.java))
            finish()
            return
        }

        // NOTE: previously this screen force-locked SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        // via OrientationLocker(this).enable(), which silently ignored
        // OrientationPreferences entirely (that dead code path is left alone for the
        // other screens that still call it — this Activity now manages its own
        // orientation directly since it's the one place the user can change it live).
        // Applied before setContentView() so the window doesn't briefly inflate at
        // the wrong orientation before flipping to the saved preference.
        applyOrientation(OrientationPreferences.getUiOrientation(this), persist = false)
        setContentView(R.layout.activity_setup_wizard)
        enableFullScreen()

        pager             = findViewById(R.id.setup_pager)
        btnBack           = findViewById(R.id.btn_setup_back)
        btnContinue       = findViewById(R.id.btn_setup_continue)
        orientationToggle = findViewById(R.id.setup_orientation_toggle)
        val btnLanguage = findViewById<MaterialButton>(R.id.btn_setup_language)

        btnLanguage.text = LocaleHelper.getCurrentLocaleCode(this).uppercase()
        btnLanguage.setOnClickListener {
          val codes = LocaleHelper.supportedLocales.map { it.code }
          val labels = LocaleHelper.supportedLocales.map { it.displayName }
          val currentCode = LocaleHelper.getCurrentLocaleCode(this)
          val checkedItem = codes.indexOf(currentCode).coerceAtLeast(0)

          MaterialAlertDialogBuilder(this)
            .setTitle(R.string.settings_app_language)
            .setSingleChoiceItems(labels.toTypedArray(), checkedItem) { dialog, which ->
              val selectedCode = codes[which]
              val code = LocaleHelper.getCurrentLocaleCode(this)
              if (selectedCode != code) {
                LocaleHelper.setLocale(this, selectedCode)
                recreate()
              }
              dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
        }

        dots = arrayOf(
            findViewById(R.id.step_dot_1),
            findViewById(R.id.step_dot_2),
            findViewById(R.id.step_dot_3),
            findViewById(R.id.step_dot_4),
        )

        pager.adapter = SetupPagerAdapter(this)
        pager.isUserInputEnabled = false
        pager.offscreenPageLimit = 1
        pager.setPageTransformer(ZoomOutPageTransformer())

        updateIndicators(0)
        updateNavButtons(0)
        syncOrientationToggle(OrientationPreferences.getUiOrientation(this))

        btnBack.setOnClickListener {
            val cur = pager.currentItem
            if (cur > 0) pager.currentItem = cur - 1
        }

        btnContinue.setOnClickListener {
            val cur = pager.currentItem
            if (cur < TOTAL_STEPS - 1) {
                pager.currentItem = cur + 1
            } else {
                // آخر خطوة — احفظ setup_complete وافتح المكتبة
                prefs().edit().putBoolean("setup_complete", true).apply()
                startActivity(Intent(this, XboxDashboardActivity::class.java))
                finish()
            }
        }

        pager.registerOnPageChangeCallback(object : ViewPager2.OnPageChangeCallback() {
            override fun onPageSelected(position: Int) {
                updateIndicators(position)
                updateNavButtons(position)
            }
        })

        orientationToggle.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (!isChecked) return@addOnButtonCheckedListener
            val orientation = if (checkedId == R.id.btn_orientation_horizontal) {
                OrientationPreferences.UiOrientation.LANDSCAPE
            } else {
                OrientationPreferences.UiOrientation.PORTRAIT
            }
            applyOrientation(orientation, persist = true)
        }
    }

    /**
     * Applies [orientation] to this Activity immediately and, when [persist] is
     * true, saves it as the app-wide UI orientation preference so other screens
     * pick it up too. SetupWizardActivity declares
     * android:configChanges="orientation|screenSize|screenLayout|smallestScreenSize"
     * in the manifest, so setting requestedOrientation here reflows the existing
     * ConstraintLayout in place — it does NOT destroy/recreate the Activity, so
     * the ViewPager2's current step and every file/folder already picked in the
     * visible fragment stay exactly as they were.
     */
    private fun applyOrientation(orientation: OrientationPreferences.UiOrientation, persist: Boolean) {
        if (persist) {
            prefs().edit()
                .putString(OrientationPreferences.PREF_UI_ORIENTATION, orientation.prefValue)
                .apply()
        }
        requestedOrientation = orientation.requestedOrientation
    }

    private fun syncOrientationToggle(orientation: OrientationPreferences.UiOrientation) {
        val checkedButtonId = when (orientation) {
            OrientationPreferences.UiOrientation.LANDSCAPE,
            OrientationPreferences.UiOrientation.REVERSE_LANDSCAPE -> R.id.btn_orientation_horizontal
            else -> R.id.btn_orientation_vertical
        }
        orientationToggle.check(checkedButtonId)
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    fun prefs() = getSharedPreferences("xaniteog_prefs", MODE_PRIVATE)

    /** URI محفوظ + permission موجود */
    private fun hasUri(key: String): Boolean {
        val uriStr = prefs().getString(key, null) ?: return false
        val uri = Uri.parse(uriStr)
        return contentResolver.persistedUriPermissions.any {
            it.uri == uri && it.isReadPermission
        }
    }

    /** ملف محلي موجود على الـ storage */
    private fun hasPath(key: String): Boolean {
        val path = prefs().getString(key, null) ?: return false
        return java.io.File(path).exists()
    }

    /** احفظ URI فوراً مع permission — يُستدعى من الـ Fragments */
    fun saveUri(key: String, uri: Uri) {
        try {
            contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        } catch (_: SecurityException) {}
        prefs().edit().putString(key, uri.toString()).apply()
        updateNavButtons(pager.currentItem)
    }

    fun refreshContinue() = updateNavButtons(pager.currentItem)

    // ── UI ───────────────────────────────────────────────────────────────────

    private fun updateIndicators(step: Int) {
        dots.forEachIndexed { i, dot ->
            dot.setBackgroundResource(
                when {
                    i < step  -> R.drawable.setup_wizard_indicator_complete
                    i == step -> R.drawable.setup_wizard_indicator_active
                    else      -> R.drawable.setup_wizard_indicator_inactive
                }
            )
        }
    }

    private fun updateNavButtons(step: Int) {
        btnBack.visibility = if (step == 0) View.GONE else View.VISIBLE
        btnContinue.text   = if (step == TOTAL_STEPS - 1)
            getString(R.string.setup_finish) else getString(R.string.setup_hub_continue)

        btnContinue.isEnabled = when (step) {
            1    -> mcpxReady && flashReady && hddReady
            2    -> discReady
            else -> true
        }
    }

    // ── Fragments ─────────────────────────────────────────────────────────────

    class WelcomeFragment : Fragment() {
        override fun onCreateView(i: LayoutInflater, c: ViewGroup?, s: Bundle?): View =
            i.inflate(R.layout.fragment_setup_step1_welcome, c, false)

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            view.findViewById<MaterialButton>(R.id.btn_setup_privacy_policy).setOnClickListener {
                startActivity(Intent(requireContext(), PrivacyPolicyActivity::class.java))
            }
        }
    }

    class GamesFolderFragment : Fragment() {
        private val pickDir = registerForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.OpenDocumentTree()
        ) { uri ->
            if (uri == null) return@registerForActivityResult
            val act = requireActivity() as SetupWizardActivity
            act.saveUri("gamesFolderUri", uri)
            view?.findViewById<TextView>(R.id.disc_path_text)?.text =
                uri.lastPathSegment ?: uri.toString()
            view?.findViewById<TextView>(R.id.setup_status_disc)?.apply {
                text = getString(R.string.setup_card_status_set)
                setTextColor(resources.getColor(R.color.xaniteog_green, null))
            }
        }

        override fun onCreateView(i: LayoutInflater, c: ViewGroup?, s: Bundle?): View =
            i.inflate(R.layout.fragment_setup_step2_games, c, false)

        override fun onViewCreated(view: View, s: Bundle?) {
            // استرجع القيمة المحفوظة إن وجدت
            val saved = (requireActivity() as SetupWizardActivity)
                .prefs().getString("gamesFolderUri", null)
            if (saved != null) {
                view.findViewById<TextView>(R.id.disc_path_text)?.text =
                    Uri.parse(saved).lastPathSegment ?: saved
                view.findViewById<TextView>(R.id.setup_status_disc)?.apply {
                    text = getString(R.string.setup_card_status_set)
                    setTextColor(resources.getColor(R.color.xaniteog_green, null))
                }
            }
            view.findViewById<MaterialButton>(R.id.btn_pick_disc).setOnClickListener {
                pickDir.launch(null)
            }
        }
    }

    class BiosFragment : Fragment() {
        private val pickMcpx = registerForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.OpenDocument()
        ) { uri -> handlePick(uri, "mcpxUri", R.id.mcpx_path_text, R.id.setup_status_mcpx) }

        private val pickFlash = registerForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.OpenDocument()
        ) { uri -> handlePick(uri, "flashUri", R.id.flash_path_text, R.id.setup_status_flash) }

        private val pickHdd = registerForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.OpenDocument()
        ) { uri -> handlePick(uri, "hddUri", R.id.hdd_path_text, R.id.setup_status_hdd) }

        private fun handlePick(uri: Uri?, prefKey: String, pathId: Int, statusId: Int) {
            if (uri == null) return
            val act = requireActivity() as SetupWizardActivity
            act.saveUri(prefKey, uri)
            view?.findViewById<TextView>(pathId)?.text =
                uri.lastPathSegment ?: uri.toString()
            view?.findViewById<TextView>(statusId)?.apply {
                text = getString(R.string.setup_card_status_set)
                setTextColor(resources.getColor(R.color.xaniteog_green, null))
            }
        }

        override fun onCreateView(i: LayoutInflater, c: ViewGroup?, s: Bundle?): View =
            i.inflate(R.layout.fragment_setup_step3_bios, c, false)

        override fun onViewCreated(view: View, s: Bundle?) {
            val prefs = (requireActivity() as SetupWizardActivity).prefs()
            // استرجع القيم المحفوظة
            listOf(
                Triple("mcpxUri",  R.id.mcpx_path_text,  R.id.setup_status_mcpx),
                Triple("flashUri", R.id.flash_path_text, R.id.setup_status_flash),
                Triple("hddUri",   R.id.hdd_path_text,   R.id.setup_status_hdd),
            ).forEach { (key, pathId, statusId) ->
                val saved = prefs.getString(key, null)
                if (saved != null) {
                    view.findViewById<TextView>(pathId)?.text =
                        Uri.parse(saved).lastPathSegment ?: saved
                    view.findViewById<TextView>(statusId)?.apply {
                        text = getString(R.string.setup_card_status_set)
                        setTextColor(resources.getColor(R.color.xaniteog_green, null))
                    }
                }
            }
            view.findViewById<MaterialButton>(R.id.btn_pick_mcpx).setOnClickListener {
                pickMcpx.launch(arrayOf("*/*"))
            }
            view.findViewById<MaterialButton>(R.id.btn_pick_flash).setOnClickListener {
                pickFlash.launch(arrayOf("*/*"))
            }
            view.findViewById<MaterialButton>(R.id.btn_pick_hdd).setOnClickListener {
                pickHdd.launch(arrayOf("*/*"))
            }
        }
    }

    class DriverFragment : Fragment() {
        private val pickDriver = registerForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.OpenDocument()
        ) { uri ->
            if (uri == null) return@registerForActivityResult
            val act = requireActivity() as SetupWizardActivity
            act.saveUri("gpuDriverUri", uri)
            // Picking the zip is not the same as installing it. Storing the URI
            // alone left the wizard showing the file name while Settings still
            // reported the stock driver, because nothing ever unpacked it.
            installDriver(uri)
        }

        override fun onCreateView(i: LayoutInflater, c: ViewGroup?, s: Bundle?): View =
            i.inflate(R.layout.fragment_setup_step4_driver, c, false)

        override fun onViewCreated(view: View, s: Bundle?) {
            GpuDriverHelper.init(requireContext())
            view.findViewById<MaterialButton>(R.id.btn_install_driver).setOnClickListener {
                pickDriver.launch(arrayOf("application/zip"))
            }
            refreshDriverStatus()
        }

        private fun installDriver(uri: Uri) {
            val context = requireContext().applicationContext
            Thread {
                val success = GpuDriverHelper.installDriverFromUri(context, uri)
                val activity = activity ?: return@Thread
                activity.runOnUiThread {
                    if (!isAdded) return@runOnUiThread
                    if (!success) {
                        Toast.makeText(
                            context,
                            R.string.settings_gpu_driver_install_failed,
                            Toast.LENGTH_LONG,
                        ).show()
                    }
                    // Read the name back from the helper rather than the file
                    // name, so the label can never claim a driver is active
                    // when the install failed.
                    refreshDriverStatus()
                }
            }.start()
        }

        private fun refreshDriverStatus() {
            val label = view?.findViewById<TextView>(R.id.tv_active_driver) ?: return
            val name = GpuDriverHelper.getInstalledDriverName()
            label.text = if (name != null) {
                getString(R.string.settings_gpu_driver_active, name)
            } else {
                getString(R.string.settings_gpu_driver_system)
            }
        }
    }

    override fun finish() {
        super.finish()
        overridePendingTransition(R.anim.fade_in, R.anim.fade_out)
    }

    override fun startActivity(intent: Intent) {
        super.startActivity(intent)
        overridePendingTransition(R.anim.slide_in_right, R.anim.fade_out)
    }

    // ── Page Transformer ────────────────────────────────────────────────────────
    // Drives the fade + slide-up transition between steps. Runs on every
    // ViewPager2 frame (including programmatic pager.currentItem = n changes
    // triggered by the Back/Continue buttons, since ViewPager2 smooth-scrolls
    // those by default) so the effect is a single continuous animation with
    // no separate/competing Animator to fall out of sync with it.
    private inner class ZoomOutPageTransformer : ViewPager2.PageTransformer {
        override fun transformPage(view: View, position: Float) {
            val pageWidth = view.width
            val pageHeight = view.height

            when {
                position < -1 -> {
                    view.alpha = 0f
                }
                position <= 1 -> {
                    val scaleFactor = 0.85f.coerceAtLeast(1f - kotlin.math.abs(position) * 0.15f)
                    val vertMargin = pageHeight * (1f - scaleFactor) / 2f
                    val horzMargin = pageWidth * (1f - scaleFactor) / 2f
                    view.translationX = if (position < 0) {
                        horzMargin - vertMargin / 2f
                    } else {
                        -horzMargin + vertMargin / 2f
                    }
                    // Subtle slide-up: settles to 0 exactly at the current page
                    // (position == 0) and eases in from ~6% of page height below
                    // as a page enters/leaves, giving the "slide up while it
                    // fades in" feel on top of the existing scale/fade.
                    view.translationY = pageHeight * 0.06f * kotlin.math.abs(position)
                    view.scaleX = scaleFactor
                    view.scaleY = scaleFactor
                    view.alpha = 0.5f.coerceAtLeast(1f - kotlin.math.abs(position) * 0.5f)
                }
                else -> {
                    view.alpha = 0f
                }
            }
        }
    }

    // ── Adapter ───────────────────────────────────────────────────────────────

    private inner class SetupPagerAdapter(fa: FragmentActivity) : FragmentStateAdapter(fa) {
        override fun getItemCount() = TOTAL_STEPS
        override fun createFragment(position: Int): Fragment = when (position) {
            0 -> WelcomeFragment()
            1 -> BiosFragment()
            2 -> GamesFolderFragment()
            3 -> DriverFragment()
            else -> WelcomeFragment()
        }
    }
}

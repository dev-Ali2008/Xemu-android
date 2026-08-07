package Ali.Xanite

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import com.google.android.material.button.MaterialButton
import java.io.File

/**
 * Beginner-friendly Insignia onboarding.
 *
 * The pieces this drives (XboxInsigniaHelper, XboxEepromEditor.applyXboxLiveDns
 * and ~25 settings_insignia_* strings) already existed but were orphaned - the
 * old Settings UI was removed with a note saying it had "moved to
 * XboxLiveActivity", and it never arrived. This is that screen.
 *
 * Each step detects its own completion where it can, so returning here shows
 * progress rather than asking again, and once everything is done the wizard is
 * replaced by a short confirmation.
 */
class InsigniaSetupActivity : BaseActivity() {

  private lateinit var prefs: android.content.SharedPreferences
  private lateinit var stepsContainer: LinearLayout
  private lateinit var setupPanel: View
  private lateinit var donePanel: View

  private val pickSetupDisc =
    registerForActivityResult(
      androidx.activity.result.contract.ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
      if (uri != null) {
        runCatching {
          contentResolver.takePersistableUriPermission(
            uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
          )
        }
        prefs.edit().putString(KEY_SETUP_DISC, uri.toString()).apply()
        render()
      }
    }

  companion object {
    private const val PREFS = "xaniteog_prefs"
    private const val KEY_DASH_SKIPPED = "insignia_dashboard_skipped"
    private const val KEY_HAS_CODE = "insignia_has_code"
    private const val KEY_SIGNED_UP = "insignia_signed_up"
    private const val KEY_SETUP_DISC = "insigniaSetupUri"
    private const val KEY_NETWORK = "setting_network_enable"
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_insignia_setup)
    EdgeToEdgeHelper.enable(this)
    EdgeToEdgeHelper.applySystemBarPadding(findViewById(R.id.insignia_scroll))
    enableFullScreen()

    prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    stepsContainer = findViewById(R.id.insignia_steps)
    setupPanel = findViewById(R.id.insignia_setup_panel)
    donePanel = findViewById(R.id.insignia_done_panel)

    findViewById<View>(R.id.btn_insignia_back).setOnClickListener { finish() }
    findViewById<Button>(R.id.btn_insignia_guide).setOnClickListener {
      openLink(getString(R.string.insignia_url_guide))
    }
    findViewById<Button>(R.id.btn_insignia_clear).setOnClickListener { confirmClear() }
    findViewById<Button>(R.id.btn_insignia_redo).setOnClickListener { confirmClear() }

    render()
  }

  override fun onResume() {
    super.onResume()
    // Networking can be toggled in Settings, and the EEPROM only appears after
    // a game has been launched, so re-check on every return.
    render()
  }

  // ── State ──

  /**
   * Insignia requires the retail MS dashboard on C: before a console can be
   * registered (see https://insignia.live/guide/connect). Detect it where we
   * can, but allow skipping - inspectDashboard() needs native tools and a
   * local HDD image, neither of which is guaranteed.
   */
  private fun dashboardReady(): Boolean {
    if (prefs.getBoolean(KEY_DASH_SKIPPED, false)) return true
    val hdd = prefs.getString("hddPath", null)?.let { File(it) }?.takeIf { it.isFile }
      ?: return false
    return runCatching {
      XboxInsigniaHelper.inspectDashboard(hdd).looksRetailDashboardInstalled
    }.getOrDefault(false)
  }

  private fun networkingOn() = prefs.getBoolean(KEY_NETWORK, false)

  private fun eepromFile(): File? {
    val roots = listOfNotNull(getExternalFilesDir(null), filesDir)
      .map { File(it, "XaniteOG") } + listOfNotNull(getExternalFilesDir(null), filesDir)
    return roots.map { File(it, "eeprom.bin") }.firstOrNull { it.isFile }
  }

  private fun dnsApplied(): Boolean {
    val file = eepromFile() ?: return false
    return XboxEepromEditor.readXboxLiveDns(file) == XboxInsigniaHelper.PRIMARY_DNS
  }

  private fun consoleRegistered() = !prefs.getString(KEY_SETUP_DISC, null).isNullOrEmpty()

  // ── Rendering ──

  private fun render() {
    // Three steps, not six. Networking and DNS are both automatic with no
    // decision to make, so they are one tap; the dashboard check was internal
    // detail a beginner cannot act on and is now folded into the same tap.
    val steps = listOf(
      Step(
        title = getString(R.string.insignia_step1_title),
        body = getText(R.string.insignia_step1_body),
        done = networkingOn() && dnsApplied(),
        primary = getString(R.string.insignia_step1_action) to { prepareConsole() },
      ),
      // Stays actionable once a file is picked - registering is what links the
      // console, so hiding the button after picking would strand the user one
      // step short.
      Step(
        title = getString(R.string.insignia_step2_title),
        body = getText(R.string.insignia_step2_body),
        done = consoleRegistered(),
        alwaysShowActions = true,
        primary = (
          if (consoleRegistered()) getString(R.string.insignia_step2_boot_action)
          else getString(R.string.insignia_step2_action)
          ) to {
          if (consoleRegistered()) bootSetupAssistant()
          else openLink(getString(R.string.insignia_url_setup_download))
        },
        secondary = (
          if (consoleRegistered()) getString(R.string.insignia_step2_change_action)
          else getString(R.string.insignia_step2_pick)
          ) to { pickDisc() },
      ),
      // Sign-up happens inside the retail dashboard with a subscription code
      // emailed from insignia.live - there is no web sign-up form - so booting
      // the dashboard has to be offered right here.
      Step(
        title = getString(R.string.insignia_step3_title),
        body = getText(R.string.insignia_step3_body),
        done = prefs.getBoolean(KEY_SIGNED_UP, false),
        alwaysShowActions = true,
        primary = getString(R.string.insignia_step3_action) to {
          openLink(getString(R.string.insignia_url_site))
        },
        secondary = getString(R.string.insignia_step3_boot_action) to { bootDashboard() },
        tertiary = getString(R.string.insignia_step3_done_action) to {
          prefs.edit().putBoolean(KEY_SIGNED_UP, true).apply()
          render()
        },
      ),
    )

    val allDone = steps.all { it.done }
    donePanel.visibility = if (allDone) View.VISIBLE else View.GONE
    setupPanel.visibility = if (allDone) View.GONE else View.VISIBLE

    if (allDone) {
      val tag = getSharedPreferences("xbox_live_prefs", Context.MODE_PRIVATE)
        .getString("xbox_live_gamertag", null)
      findViewById<TextView>(R.id.insignia_gamertag_line).apply {
        if (!tag.isNullOrBlank()) {
          text = tag
          visibility = View.VISIBLE
        } else {
          visibility = View.GONE
        }
      }
      return
    }

    stepsContainer.removeAllViews()
    steps.forEach { stepsContainer.addView(buildStepView(it)) }
  }

  private data class Step(
    val title: String,
    /** CharSequence so <b> in strings.xml survives - it marks the exact file
     *  name and card number, which are the bits people get wrong. */
    val body: CharSequence,
    val done: Boolean,
    val primary: Pair<String, () -> Unit>,
    val secondary: Pair<String, () -> Unit>? = null,
    val tertiary: Pair<String, () -> Unit>? = null,
    /** Keep buttons visible even when complete (used for "boot" actions). */
    val alwaysShowActions: Boolean = false,
  )

  private fun buildStepView(step: Step): View {
    val d = resources.displayMetrics.density
    fun dp(v: Int) = (v * d).toInt()

    return LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setBackgroundResource(R.drawable.xbox_menu_item_bg)
      setPadding(dp(16), dp(14), dp(16), dp(14))
      layoutParams = LinearLayout.LayoutParams(
        LinearLayout.LayoutParams.MATCH_PARENT,
        LinearLayout.LayoutParams.WRAP_CONTENT,
      ).apply { bottomMargin = dp(12) }

      addView(LinearLayout(this@InsigniaSetupActivity).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        addView(TextView(this@InsigniaSetupActivity).apply {
          text = step.title
          setTextColor(androidx.core.content.ContextCompat.getColor(this@InsigniaSetupActivity, R.color.xanite_accent))
          textSize = 15f
          setTypeface(null, android.graphics.Typeface.BOLD)
          layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
        })
        if (step.done) {
          addView(TextView(this@InsigniaSetupActivity).apply {
            text = "✓ " + getString(R.string.insignia_step_done)
            setTextColor(0xFF7CFF6B.toInt())
            textSize = 13f
            setTypeface(null, android.graphics.Typeface.BOLD)
          })
        }
      })

      addView(TextView(this@InsigniaSetupActivity).apply {
        text = step.body
        setTextColor(0xCCFFF6EB.toInt())
        textSize = 13f
        setLineSpacing(0f, 1.25f)
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(6) }
      })

      // A completed step keeps its buttons hidden - nothing left to do, and it
      // keeps the list short once most of setup is behind you.
      if (!step.done || step.alwaysShowActions) {
        addView(MaterialButton(
          this@InsigniaSetupActivity, null,
          com.google.android.material.R.attr.materialButtonOutlinedStyle
        ).apply {
          text = step.primary.first
          setOnClickListener { step.primary.second() }
          layoutParams = LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(10) }
        })

        listOfNotNull(step.secondary, step.tertiary).forEach { (label, action) ->
          addView(MaterialButton(
            this@InsigniaSetupActivity, null,
            com.google.android.material.R.attr.materialButtonOutlinedStyle
          ).apply {
            text = label
            setOnClickListener { action() }
            layoutParams = LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(6) }
          })
        }
      }
    }
  }

  // ── Actions ──

  /**
   * Step 1 in one tap: networking and DNS are both automatic and have no
   * decision attached, so asking twice was pure friction. The EEPROM only
   * exists after a game has been launched, so say so plainly if it is missing
   * rather than failing silently.
   */
  private fun prepareConsole() {
    prefs.edit().putBoolean(KEY_NETWORK, true).apply()

    val file = eepromFile()
    if (file == null) {
      toast(getString(R.string.insignia_prepare_partial))
      render()
      return
    }
    try {
      XboxEepromEditor.applyXboxLiveDns(file, XboxInsigniaHelper.primaryDnsBytes())
      toast(getString(R.string.insignia_step1_result))
    } catch (t: Throwable) {
      toast(getString(R.string.insignia_dns_failed, t.message ?: t.javaClass.simpleName))
    }
    render()
  }

  private fun applyDns() {
    val file = eepromFile()
    if (file == null) {
      toast(getString(R.string.insignia_dns_no_eeprom))
      return
    }
    try {
      val changed = XboxEepromEditor.applyXboxLiveDns(
        file,
        XboxInsigniaHelper.primaryDnsBytes(),
      )
      toast(
        getString(
          if (changed) R.string.insignia_dns_applied else R.string.insignia_dns_already
        )
      )
      render()
    } catch (t: Throwable) {
      toast(getString(R.string.insignia_dns_failed, t.message ?: t.javaClass.simpleName))
    }
  }

  private fun pickDisc() {
    runCatching { pickSetupDisc.launch(arrayOf("*/*")) }
      .onFailure { toast(getString(R.string.insignia_boot_failed)) }
  }

  /**
   * Boots the picked Setup Assistant image through the same path the library
   * uses, so per-game overrides and the synchronous pref flush behave
   * identically. Registering here is what creates the Insignia account.
   */
  private fun bootSetupAssistant() {
    val uriString = prefs.getString(KEY_SETUP_DISC, null)
    if (uriString.isNullOrEmpty()) {
      pickDisc()
      return
    }
    try {
      GameLauncher.launch(this, Uri.parse(uriString), "insignia-setup-assistant")
      finish()
    } catch (_: Exception) {
      toast(getString(R.string.insignia_boot_failed))
    }
  }

  /** Boots with no disc so the console lands on the retail dashboard. */
  private fun bootDashboard() {
    try {
      GameLauncher.launchDashboard(this)
      finish()
    } catch (_: Exception) {
      toast(getString(R.string.insignia_dashboard_boot_failed))
    }
  }

  private fun confirmClear() {
    com.google.android.material.dialog.MaterialAlertDialogBuilder(
      this, R.style.ThemeOverlay_Xaniteog_RoundedDialog
    )
      .setTitle(R.string.insignia_clear_title)
      .setMessage(R.string.insignia_clear_message)
      .setNegativeButton(android.R.string.cancel, null)
      .setPositiveButton(R.string.insignia_clear_confirm) { _, _ ->
        // Only clears this screen's own progress flags. Deliberately leaves
        // setting_network_enable and the EEPROM DNS alone - those are real
        // console state, not checkboxes, and silently reverting them would
        // break a working setup.
        prefs.edit()
          .remove(KEY_DASH_SKIPPED)
          .remove(KEY_HAS_CODE)
          .remove(KEY_SIGNED_UP)
          .remove(KEY_SETUP_DISC)
          .apply()
        toast(getString(R.string.insignia_cleared))
        render()
      }
      .show()
  }

  private fun openLink(url: String) {
    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
      .addCategory(Intent.CATEGORY_BROWSABLE)
    try {
      startActivity(intent)
    } catch (_: Exception) {
      toast(getString(R.string.insignia_no_browser))
    }
  }

  private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
}

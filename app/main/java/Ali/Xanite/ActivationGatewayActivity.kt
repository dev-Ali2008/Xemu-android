package Ali.Xanite

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.browser.customtabs.CustomTabsIntent

/**
 * Non-bypassable Gold activation gate.
 *
 * There is no navigation out of this screen except a successful activation or
 * exiting the app: back is swallowed, the task is excluded from recents, and
 * nothing here starts another activity. Note this is only the *presentation*
 * of the block — the actual authority is the native verifier, which is
 * re-consulted on every launch.
 */
class ActivationGatewayActivity : Activity() {

  companion object {
    private const val STATE_PENDING = "pending_state"

    fun intent(context: Context): Intent =
      Intent(context, ActivationGatewayActivity::class.java)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
  }

  private lateinit var activateButton: Button
  private lateinit var exitButton: Button
  private lateinit var getGoldButton: Button
  private lateinit var progress: ProgressBar
  private lateinit var message: TextView

  /** Outstanding OAuth state, awaited across the browser round trip. */
  private var pendingState: String? = null
  private var busy = false

  override fun attachBaseContext(newBase: Context) {
    super.attachBaseContext(LocaleHelper.applyLocale(newBase))
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.activity_activation_gateway)

    pendingState = savedInstanceState?.getString(STATE_PENDING)

    activateButton = findViewById(R.id.activation_activate)
    exitButton = findViewById(R.id.activation_exit)
    progress = findViewById(R.id.activation_progress)
    message = findViewById(R.id.activation_message)

    findViewById<TextView>(R.id.activation_slots).text =
      getString(R.string.activation_slots_value, BuildConfig.MAX_DEVICE_SLOTS)

    // Debug builds only: the derived device id is otherwise unobservable from
    // outside the app, which makes activation problems very hard to diagnose.
    if (BuildConfig.DEBUG) {
      android.util.Log.d("LicenseNative", "device hwid=${LicenseGate.hwid(this)}")
    }

    getGoldButton = findViewById(R.id.activation_get_gold)

    activateButton.setOnClickListener { beginActivation() }
    exitButton.setOnClickListener { finishAffinity() }
    getGoldButton.setOnClickListener { openCampaignPage() }

    if (!ActivationClient.isConfigured()) {
      // Fail closed rather than pretending activation is possible. Distinct
      // from a failed attempt: retrying will never help here.
      activateButton.isEnabled = false
      activateButton.alpha = 0.4f
      showError(getString(R.string.activation_error_not_configured))
    }

    handleRedirect(intent)
  }

  override fun onNewIntent(intent: Intent?) {
    super.onNewIntent(intent)
    if (intent != null) setIntent(intent)
    handleRedirect(intent)
  }

  override fun onSaveInstanceState(outState: Bundle) {
    super.onSaveInstanceState(outState)
    outState.putString(STATE_PENDING, pendingState)
  }

  /** Hard block: back must not reach the dashboard. */
  @Deprecated("Deprecated in Java")
  override fun onBackPressed() {
    // Intentionally empty.
  }

  override fun onResume() {
    super.onResume()
    // Covers the case where the license landed via another path.
    if (LicenseGate.isActivated(this)) proceed()
  }

  private fun beginActivation() {
    if (busy) return
    val hwid = LicenseGate.hwid(this)
    if (hwid == null) {
      showError(getString(R.string.activation_error_generic))
      return
    }
    val state = ActivationClient.newState()
    pendingState = state
    val uri = ActivationClient.authorizeUri(state, hwid)
    try {
      CustomTabsIntent.Builder()
        .setShowTitle(true)
        .build()
        .launchUrl(this, uri)
    } catch (t: Throwable) {
      // No browser able to handle the intent.
      try {
        startActivity(Intent(Intent.ACTION_VIEW, uri))
      } catch (t2: Throwable) {
        showError(getString(R.string.activation_error_network))
      }
    }
  }

  /** Consumes `xanite://activate?state=…&status=…`. */
  private fun handleRedirect(intent: Intent?) {
    val data: Uri = intent?.data ?: return
    if (data.scheme != ActivationClient.REDIRECT_SCHEME ||
      data.host != ActivationClient.REDIRECT_HOST
    ) {
      return
    }
    // Clear it so a configuration change cannot replay the same redirect.
    setIntent(Intent(this, ActivationGatewayActivity::class.java))

    val returnedState = data.getQueryParameter("state")
    val status = data.getQueryParameter("status")
    val expected = pendingState

    if (expected == null || returnedState == null || returnedState != expected) {
      // Mismatched state: either a stale tab or a forged redirect.
      showError(getString(R.string.activation_error_generic))
      return
    }
    pendingState = null

    if (status != "ok") {
      showDenied(data.getQueryParameter("reason") ?: status ?: "generic")
      return
    }
    claim(expected)
  }

  private fun claim(state: String) {
    setBusy(true)
    ActivationClient.claim(this, state) { result ->
      runOnUiThread {
        setBusy(false)
        when (result) {
          is ActivationClient.Result.Success -> proceed()
          is ActivationClient.Result.Denied -> showDenied(result.reason)
          is ActivationClient.Result.Failed ->
            showError(
              if (result.cause == "network") getString(R.string.activation_error_network)
              else getString(R.string.activation_error_generic)
            )
        }
      }
    }
  }

  private fun reasonToMessage(reason: String): String = when (reason) {
    "not_purchased" -> getString(R.string.activation_error_not_purchased)
    "no_email" -> getString(R.string.activation_error_no_email)
    "slots_full" -> getString(R.string.activation_error_slots_full, BuildConfig.MAX_DEVICE_SLOTS)
    "cancelled", "access_denied" -> getString(R.string.activation_error_cancelled)
    "network" -> getString(R.string.activation_error_network)
    else -> getString(R.string.activation_error_generic)
  }

  private fun proceed() {
    startActivity(
      Intent(this, LauncherActivity::class.java)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
    )
    finish()
  }

  private fun setBusy(value: Boolean) {
    busy = value
    progress.visibility = if (value) View.VISIBLE else View.GONE
    if (value) getGoldButton.visibility = View.GONE
    activateButton.isEnabled = !value && ActivationClient.isConfigured()
    if (value) message.visibility = View.GONE
  }

  private fun showError(text: String) {
    message.text = text
    message.visibility = View.VISIBLE
    progress.visibility = View.GONE
  }

  /** Offers the purchase page only when the failure was "not purchased". */
  private fun showDenied(reason: String) {
    showError(reasonToMessage(reason))
    getGoldButton.visibility =
      if (reason == "not_purchased" && BuildConfig.PATREON_CAMPAIGN_URL.isNotBlank()) {
        View.VISIBLE
      } else {
        View.GONE
      }
  }

  private fun openCampaignPage() {
    val uri = Uri.parse(BuildConfig.PATREON_CAMPAIGN_URL)
    try {
      CustomTabsIntent.Builder().setShowTitle(true).build().launchUrl(this, uri)
    } catch (t: Throwable) {
      try {
        startActivity(Intent(Intent.ACTION_VIEW, uri))
      } catch (t2: Throwable) {
        showError(getString(R.string.activation_error_network))
      }
    }
  }
}

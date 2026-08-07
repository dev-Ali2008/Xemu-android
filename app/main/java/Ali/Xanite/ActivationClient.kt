package Ali.Xanite

import android.content.Context
import android.net.Uri
import android.util.Base64
import android.util.Log
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.SecureRandom
import java.util.concurrent.Executors
import org.json.JSONObject

/**
 * Talks to the Xanite activation backend.
 *
 * The Patreon OAuth round trip is driven by the *server*, not the app: the
 * client secret must never ship in an APK, and Patreon only accepts registered
 * https redirect URIs. The app opens `/oauth/start`, the backend bounces
 * through Patreon and finally redirects back to `xanite://activate`, at which
 * point the app claims the signed license with a one-time state token.
 */
object ActivationClient {
  private const val TAG = "ActivationClient"
  private const val TIMEOUT_MS = 20_000

  const val REDIRECT_SCHEME = "xanite"
  const val REDIRECT_HOST = "activate"

  private val io = Executors.newSingleThreadExecutor { r ->
    Thread(r, "activation-io").apply { isDaemon = true }
  }

  sealed class Result {
    object Success : Result()
    /** Backend reachable, but this account/device cannot be activated. */
    data class Denied(val reason: String) : Result()
    data class Failed(val cause: String) : Result()
  }

  fun isConfigured(): Boolean = BuildConfig.ACTIVATION_BACKEND_URL.isNotBlank()

  fun newState(): String {
    val bytes = ByteArray(24)
    SecureRandom().nextBytes(bytes)
    return Base64.encodeToString(bytes, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
  }

  /** Browser entry point for the OAuth hand-off. */
  fun authorizeUri(state: String, hwid: String): Uri =
    Uri.parse(BuildConfig.ACTIVATION_BACKEND_URL.trimEnd('/') + "/oauth/start")
      .buildUpon()
      .appendQueryParameter("state", state)
      .appendQueryParameter("hwid", hwid)
      .build()

  /**
   * Exchanges the one-time [state] for the signed license and stores it.
   * Runs off the main thread; [onResult] is posted back by the caller.
   */
  fun claim(context: Context, state: String, onResult: (Result) -> Unit) {
    val appContext = context.applicationContext
    io.execute {
      val hwid = LicenseGate.hwid(appContext)
      if (hwid == null) {
        onResult(Result.Failed("hwid"))
        return@execute
      }
      val body = JSONObject().put("state", state).put("hwid", hwid)
      onResult(postForLicense(appContext, "/api/activate/claim", body))
    }
  }

  /** Re-signs the local license, picking up current slot counts. */
  fun refresh(context: Context, onResult: (Result) -> Unit) {
    val appContext = context.applicationContext
    io.execute {
      val license = readLicenseBase64(appContext)
      if (license == null) {
        onResult(Result.Failed("no_license"))
        return@execute
      }
      onResult(postForLicense(appContext, "/api/refresh", JSONObject().put("license", license)))
    }
  }

  /**
   * Releases this device's slot. The signed license itself is the credential,
   * so no second OAuth round trip is needed.
   */
  fun unbind(context: Context, onResult: (Result) -> Unit) {
    val appContext = context.applicationContext
    io.execute {
      val license = readLicenseBase64(appContext)
      if (license == null) {
        // Nothing bound locally; clearing is still the right end state.
        LicenseGate.clear(appContext)
        onResult(Result.Success)
        return@execute
      }
      val response = post(appContext, "/api/unbind", JSONObject().put("license", license))
      when (response) {
        is Response.Ok -> {
          LicenseGate.clear(appContext)
          onResult(Result.Success)
        }
        is Response.Denied -> onResult(Result.Denied(response.reason))
        is Response.Error -> onResult(Result.Failed(response.cause))
      }
    }
  }

  // -------------------------------------------------------------------------

  private fun readLicenseBase64(context: Context): String? = runCatching {
    val file = LicenseGate.licenseFile(context)
    if (!file.isFile) return null
    Base64.encodeToString(file.readBytes(), Base64.NO_WRAP)
  }.getOrNull()

  private fun postForLicense(context: Context, path: String, body: JSONObject): Result =
    when (val response = post(context, path, body)) {
      is Response.Ok -> {
        val encoded = response.json.optString("license", "")
        if (encoded.isEmpty()) {
          Result.Failed("empty_license")
        } else {
          val blob = runCatching { Base64.decode(encoded, Base64.DEFAULT) }.getOrNull()
          when {
            blob == null -> Result.Failed("bad_license_encoding")
            !LicenseGate.write(context, blob) -> Result.Failed("write_failed")
            // Native is the authority: a blob the core will not accept is a
            // failure even though the server returned 200.
            !LicenseGate.isActivated(context) -> {
              LicenseGate.clear(context)
              Result.Failed("rejected_by_core")
            }
            else -> Result.Success
          }
        }
      }
      is Response.Denied -> Result.Denied(response.reason)
      is Response.Error -> Result.Failed(response.cause)
    }

  private sealed class Response {
    data class Ok(val json: JSONObject) : Response()
    data class Denied(val reason: String) : Response()
    data class Error(val cause: String) : Response()
  }

  private fun post(context: Context, path: String, body: JSONObject): Response {
    if (!isConfigured()) return Response.Error("not_configured")
    val url = BuildConfig.ACTIVATION_BACKEND_URL.trimEnd('/') + path
    var conn: HttpURLConnection? = null
    return try {
      conn = (URL(url).openConnection() as HttpURLConnection).apply {
        requestMethod = "POST"
        connectTimeout = TIMEOUT_MS
        readTimeout = TIMEOUT_MS
        doOutput = true
        setRequestProperty("Content-Type", "application/json")
        setRequestProperty("Accept", "application/json")
      }
      conn.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
      val code = conn.responseCode
      val text = (if (code in 200..299) conn.inputStream else conn.errorStream)
        ?.bufferedReader()?.use { it.readText() }.orEmpty()
      when {
        code in 200..299 -> Response.Ok(if (text.isBlank()) JSONObject() else JSONObject(text))
        // 4xx carries an actionable reason (not_purchased, slots_full, ...).
        code in 400..499 -> Response.Denied(
          runCatching { JSONObject(text).optString("reason", "denied") }.getOrDefault("denied")
        )
        else -> Response.Error("http_$code")
      }
    } catch (e: IOException) {
      Log.w(TAG, "activation request failed", e)
      Response.Error("network")
    } catch (t: Throwable) {
      Log.w(TAG, "activation request failed", t)
      Response.Error("unexpected")
    } finally {
      conn?.disconnect()
    }
  }
}

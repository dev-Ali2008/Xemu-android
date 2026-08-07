package Ali.Xanite

import android.content.Context
import android.content.res.Configuration
import android.os.Build
import java.util.Locale

object LocaleHelper {
  private const val PREFS_NAME = "xaniteog_prefs"
  private const val PREF_LOCALE = "app_locale"

  val supportedLocales = listOf(
    LocaleOption("en", "English"),
    LocaleOption("ru", "Русский"),
    LocaleOption("es", "Español"),
    LocaleOption("zh", "中文"),
    LocaleOption("ar", "العربية"),
    LocaleOption("pt-rBR", "Português (Brasil)"),
    LocaleOption("de", "Deutsch"),
    LocaleOption("fr", "Français"),
    LocaleOption("ja", "日本語"),
  )

  data class LocaleOption(
    val code: String,
    val displayName: String,
  )

  fun getCurrentLocaleCode(context: Context): String {
    return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      .getString(PREF_LOCALE, "en") ?: "en"
  }

  fun setLocale(context: Context, localeCode: String): Context {
    context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
      .edit()
      .putString(PREF_LOCALE, localeCode)
      .apply()
    return applyLocale(context, localeCode)
  }

  fun applyLocale(context: Context, localeCode: String? = null): Context {
    val code = localeCode ?: getCurrentLocaleCode(context)
    val locale = if (code.contains("-r")) {
      val parts = code.split("-r")
      Locale(parts[0], parts[1])
    } else if (code == "zh") {
      Locale("zh", "CN")
    } else if (code == "ar") {
      Locale("ar")
    } else {
      Locale(code)
    }

    Locale.setDefault(locale)

    return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      val config = Configuration(context.resources.configuration)
      config.setLocale(locale)
      context.createConfigurationContext(config)
    } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
      val config = Configuration(context.resources.configuration)
      config.setLocale(locale)
      context.resources.updateConfiguration(config, context.resources.displayMetrics)
      context
    } else {
      val config = Configuration(context.resources.configuration)
      config.locale = locale
      context.resources.updateConfiguration(config, context.resources.displayMetrics)
      context
    }
  }

  fun getLocaleLabel(code: String): String {
    return supportedLocales.find { it.code == code }?.displayName ?: "English"
  }
}

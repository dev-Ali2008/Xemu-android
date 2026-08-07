package Ali.Xanite

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import android.util.AttributeSet
import android.view.Gravity
import android.text.TextUtils
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView

class GameBladeView @JvmOverloads constructor(
  context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {

  private val bladePath = Path()
  private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
  private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
    style = Paint.Style.STROKE
    strokeWidth = 1.5f
  }

  private val chamfer: Float
  private var _selected = false

  var bladeSelected: Boolean
    get() = _selected
    set(v) {
      _selected = v
      updateAppearance()
      invalidate()
    }

  private var _gameTitle = ""
  var gameTitle: String
    get() = _gameTitle
    set(v) { _gameTitle = v; titleView.text = v }

  private var _subtitle = ""
  var subtitle: String
    get() = _subtitle
    set(v) { _subtitle = v; subtitleView.text = v; subtitleView.visibility = if (v.isNotEmpty()) VISIBLE else GONE }

  private val titleView: TextView
  private val subtitleView: TextView
  val coverImage: ImageView

  /**
   * Menu style: no cover art and no size subtitle, so the title reclaims the
   * margins reserved for them. Without this the Y menu (a 280dp panel) left
   * only ~90dp for text and every entry rendered truncated - "Set Cust...",
   * "Delete G...". Also allows two lines for longer labels.
   */
  fun useMenuLabelStyle() {
    coverImage.visibility = GONE
    subtitleView.visibility = GONE
    val d = resources.displayMetrics.density
    titleView.layoutParams = LayoutParams(
      LayoutParams.MATCH_PARENT,
      LayoutParams.WRAP_CONTENT,
      Gravity.START or Gravity.CENTER_VERTICAL
    ).apply {
      marginStart = (4 * d).toInt()
      marginEnd = (4 * d).toInt()
    }
    titleView.maxLines = 2
    titleView.requestLayout()
  }

  init {
    chamfer = resources.displayMetrics.density * 10f
    setPadding(
      (16 * resources.displayMetrics.density).toInt(),
      (8 * resources.displayMetrics.density).toInt(),
      (16 * resources.displayMetrics.density).toInt(),
      (8 * resources.displayMetrics.density).toInt()
    )

    minimumHeight = (74 * resources.displayMetrics.density).toInt()

    coverImage = ImageView(context).apply {
      layoutParams = LayoutParams(
        (48 * resources.displayMetrics.density).toInt(),
        (48 * resources.displayMetrics.density).toInt(),
        Gravity.START or Gravity.CENTER_VERTICAL
      )
      scaleType = ImageView.ScaleType.CENTER_CROP
    }

    titleView = TextView(context).apply {
      layoutParams = LayoutParams(
        LayoutParams.MATCH_PARENT,
        LayoutParams.WRAP_CONTENT,
        Gravity.START or Gravity.CENTER_VERTICAL
      ).apply {
        marginStart = (58 * resources.displayMetrics.density).toInt()
        marginEnd = (100 * resources.displayMetrics.density).toInt()
      }
      textSize = 14f
      setTypeface(null, android.graphics.Typeface.BOLD)
      setTextColor(Color.parseColor("#FFF6EB"))
      maxLines = 1
      ellipsize = TextUtils.TruncateAt.END
    }

    subtitleView = TextView(context).apply {
      layoutParams = LayoutParams(
        (94 * resources.displayMetrics.density).toInt(),
        LayoutParams.WRAP_CONTENT,
        Gravity.END or Gravity.CENTER_VERTICAL
      )
      textSize = 11f
      setTextColor(Color.parseColor("#BFA898"))
      maxLines = 1
      ellipsize = TextUtils.TruncateAt.END
      visibility = GONE
    }

    addView(coverImage)
    addView(titleView)
    addView(subtitleView)

    setWillNotDraw(false)
    updateAppearance()
  }

  private fun updateAppearance() {
    if (_selected) {
      fillPaint.shader = LinearGradient(
        0f, 0f, width.toFloat(), 0f,
        intArrayOf(
          androidx.core.content.ContextCompat.getColor(context, R.color.xanite_accent),
          Color.parseColor("#A66B00")
        ),
        null,
        Shader.TileMode.CLAMP
      )
      strokePaint.color = Color.parseColor("#FFF0A0")
      titleView.setTextColor(Color.parseColor("#120D00"))
      subtitleView.setTextColor(Color.parseColor("#2A1A00"))
    } else {
      fillPaint.shader = null
      fillPaint.color = Color.parseColor("#241D0B")
      strokePaint.color = androidx.core.content.ContextCompat.getColor(context, R.color.xanite_accent_55)
      titleView.setTextColor(Color.parseColor("#FFF6EB"))
      subtitleView.setTextColor(Color.parseColor("#BFA898"))
    }
  }

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    buildBladePath(w.toFloat(), h.toFloat())
  }

  private fun buildBladePath(w: Float, h: Float) {
    val c = chamfer
    bladePath.rewind()
    bladePath.moveTo(c, 0f)
    bladePath.lineTo(w - c, 0f)
    bladePath.lineTo(w, c)
    bladePath.lineTo(w, h - c)
    bladePath.lineTo(w - c, h)
    bladePath.lineTo(c, h)
    bladePath.lineTo(0f, h - c)
    bladePath.lineTo(0f, c)
    bladePath.close()
  }

  override fun onDraw(canvas: Canvas) {
    canvas.drawPath(bladePath, fillPaint)
    canvas.drawPath(bladePath, strokePaint)
    super.onDraw(canvas)
  }

  override fun setSelected(selected: Boolean) {
    super.setSelected(selected)
    bladeSelected = selected
  }
}

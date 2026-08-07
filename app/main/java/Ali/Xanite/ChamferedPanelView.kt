package Ali.Xanite

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.Shader
import android.util.AttributeSet
import android.view.animation.DecelerateInterpolator
import android.widget.FrameLayout

class ChamferedPanelView @JvmOverloads constructor(
  context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {

  private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
  private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
  private val path = Path()
  private var _chamferSize = 0f

  var chamferSize: Float
    get() = _chamferSize
    set(v) { _chamferSize = v; invalidatePath() }

  var fillColor: Int = Color.parseColor("#1A170D")
    set(v) { field = v; fillPaint.color = v; invalidate() }

  var strokeColor: Int = androidx.core.content.ContextCompat.getColor(context, R.color.xanite_accent_33)
    set(v) { field = v; strokePaint.color = v; invalidate() }

  var strokeWidth: Float = 1.5f
    set(v) { field = v; strokePaint.strokeWidth = v; invalidate() }

  var panelSelected: Boolean = false
    set(v) {
      field = v
      if (v) startGlowPulse() else stopGlowPulse()
      invalidate()
    }

  private var glowAlpha = 0f
  private var glowAnimator: ValueAnimator? = null

  init {
    setWillNotDraw(false)
    val ta = context.obtainStyledAttributes(attrs, R.styleable.ChamferedPanelView, defStyleAttr, 0)
    fillColor = ta.getColor(R.styleable.ChamferedPanelView_fillColor, Color.parseColor("#1A170D"))
    strokeColor = ta.getColor(R.styleable.ChamferedPanelView_strokeColor, androidx.core.content.ContextCompat.getColor(context, R.color.xanite_accent_33))
    _chamferSize = ta.getDimension(R.styleable.ChamferedPanelView_chamferSize, resources.displayMetrics.density * 12f)
    ta.recycle()
    fillPaint.color = fillColor
    strokePaint.color = strokeColor
    strokePaint.strokeWidth = strokeWidth
  }

  private fun invalidatePath() {
    path.rewind()
    buildChamferedPath(path, width.toFloat(), height.toFloat(), _chamferSize)
    invalidate()
  }

  private fun buildChamferedPath(p: Path, w: Float, h: Float, c: Float) {
    p.moveTo(c, 0f)
    p.lineTo(w - c, 0f)
    p.lineTo(w, c)
    p.lineTo(w, h - c)
    p.lineTo(w - c, h)
    p.lineTo(c, h)
    p.lineTo(0f, h - c)
    p.lineTo(0f, c)
    p.close()
  }

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    invalidatePath()
  }

  override fun dispatchDraw(canvas: Canvas) {
    canvas.save()
    canvas.clipPath(path)
    super.dispatchDraw(canvas)
    canvas.restore()
  }

  override fun onDraw(canvas: Canvas) {
    canvas.drawPath(path, fillPaint)
    canvas.drawPath(path, strokePaint)

    if (panelSelected && glowAlpha > 0.01f) {
      val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        shader = RadialGradient(
          width / 2f, height / 2f, maxOf(width, height) * 0.6f,
          intArrayOf(
            Color.argb((glowAlpha * 0.3f * 255).toInt(), 0x39, 0xFF, 0x14),
            Color.TRANSPARENT
          ),
          floatArrayOf(0f, 1f),
          Shader.TileMode.CLAMP
        )
        style = Paint.Style.FILL
      }
      canvas.drawPath(path, glowPaint)
    }
  }

  private fun startGlowPulse() {
    stopGlowPulse()
    glowAnimator = ValueAnimator.ofFloat(0.3f, 1.0f, 0.3f).apply {
      duration = 1800
      repeatCount = ValueAnimator.INFINITE
      interpolator = DecelerateInterpolator()
      addUpdateListener { a ->
        glowAlpha = a.animatedFraction
        invalidate()
      }
      start()
    }
  }

  private fun stopGlowPulse() {
    glowAnimator?.cancel()
    glowAnimator = null
    glowAlpha = 0f
    invalidate()
  }

  override fun onDetachedFromWindow() {
    super.onDetachedFromWindow()
    stopGlowPulse()
  }
}

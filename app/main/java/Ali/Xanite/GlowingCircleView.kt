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
import android.widget.ImageView
import androidx.appcompat.widget.AppCompatImageView

class GlowingCircleView @JvmOverloads constructor(
  context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : AppCompatImageView(context, attrs, defStyleAttr) {

  private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
  private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
  private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
  private val imageClipPath = Path()

  var ringColor: Int = Color.parseColor("#39FF14")
    set(v) { field = v; ringPaint.color = v; invalidate() }

  var ringWidth: Float = 3f
    set(v) { field = v; ringPaint.strokeWidth = v; invalidate() }

  var fillColor: Int = Color.parseColor("#1A170D")
    set(v) { field = v; fillPaint.color = v; invalidate() }

  var glowColor: Int = Color.parseColor("#39FF14")
    set(v) { field = v; invalidate() }

  private var pulsePhase = 0f
  private var animator: ValueAnimator? = null

  init {
    val ta = context.obtainStyledAttributes(attrs, R.styleable.GlowingCircleView, defStyleAttr, 0)
    ringColor = ta.getColor(R.styleable.GlowingCircleView_ringColor, Color.parseColor("#39FF14"))
    fillColor = ta.getColor(R.styleable.GlowingCircleView_fillColor, Color.parseColor("#1A170D"))
    glowColor = ta.getColor(R.styleable.GlowingCircleView_glowColor, Color.parseColor("#39FF14"))
    ringWidth = ta.getDimension(R.styleable.GlowingCircleView_ringWidth, 3f)
    ta.recycle()
    ringPaint.color = ringColor
    ringPaint.strokeWidth = ringWidth
    fillPaint.color = fillColor
    scaleType = ImageView.ScaleType.CENTER_CROP
    setWillNotDraw(false)
    startPulse()
  }

  override fun onDraw(canvas: Canvas) {
    val cx = width / 2f
    val cy = height / 2f
    val radius = minOf(cx, cy) - ringWidth

    val glowRadius = radius + 12f + pulsePhase * 8f
    glowPaint.shader = RadialGradient(
      cx, cy, glowRadius,
      intArrayOf(
        Color.argb((pulsePhase * 100).toInt(), Color.red(glowColor), Color.green(glowColor), Color.blue(glowColor)),
        Color.TRANSPARENT
      ),
      floatArrayOf(0.6f, 1f),
      Shader.TileMode.CLAMP
    )
    canvas.drawCircle(cx, cy, glowRadius, glowPaint)

    canvas.drawCircle(cx, cy, radius, fillPaint)

    // Keep cover art inside the glowing circle. This view used to only paint
    // the ring, which left the selected-game artwork permanently blank.
    val imageRadius = (radius - ringWidth - 2f).coerceAtLeast(0f)
    if (drawable != null && imageRadius > 0f) {
      val checkpoint = canvas.save()
      imageClipPath.rewind()
      imageClipPath.addCircle(cx, cy, imageRadius, Path.Direction.CW)
      canvas.clipPath(imageClipPath)
      super.onDraw(canvas)
      canvas.restoreToCount(checkpoint)
    }

    val ringRadius = radius - 2f
    canvas.drawCircle(cx, cy, ringRadius, ringPaint)
  }

  private fun startPulse() {
    animator = ValueAnimator.ofFloat(0f, 1f, 0f).apply {
      duration = 2000
      repeatCount = ValueAnimator.INFINITE
      interpolator = DecelerateInterpolator()
      addUpdateListener { a ->
        pulsePhase = a.animatedFraction
        invalidate()
      }
      start()
    }
  }

  override fun onDetachedFromWindow() {
    super.onDetachedFromWindow()
    animator?.cancel()
  }
}

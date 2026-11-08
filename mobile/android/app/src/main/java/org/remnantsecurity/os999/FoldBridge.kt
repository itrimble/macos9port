package org.remnantsecurity.os999

import android.util.DisplayMetrics
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.core.util.Consumer
import androidx.window.java.layout.WindowInfoTrackerCallbackAdapter
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import androidx.window.layout.WindowLayoutInfo
import androidx.window.layout.WindowMetricsCalculator

/**
 * Jetpack WindowManager -> OS999. Each layout change reports the hinge
 * (state, orientation, bounds in window pixels) and which screen we're on;
 * the native shell turns that into Cover, Unfolded, Laptop or Book.
 * Numbers match MobileFoldState / MobileFoldAxis in mobile/include/mobile.h.
 */
class FoldBridge(private val activity: OS999Activity) {
    // SDLActivity is a plain Activity (not a LifecycleOwner), so use the
    // callback adapter rather than the coroutine Flow.
    private val tracker = WindowInfoTrackerCallbackAdapter(WindowInfoTracker.getOrCreate(activity))
    private val listener: Consumer<WindowLayoutInfo> = Consumer { info ->
        val f = info.displayFeatures.filterIsInstance<FoldingFeature>().firstOrNull()
        Log.i(TAG, "layout: features=${info.displayFeatures.size} fold=${f?.state} ${f?.orientation} ${f?.bounds}")
        if (f == null) {
            nativeReportFold(STATE_NONE, AXIS_NONE, 0, 0, 0, 0)
        } else {
            val state = if (f.state == FoldingFeature.State.HALF_OPENED) STATE_HALF else STATE_FLAT
            val axis = if (f.orientation == FoldingFeature.Orientation.HORIZONTAL)
                AXIS_HORIZONTAL else AXIS_VERTICAL
            val b = f.bounds
            nativeReportFold(state, axis, b.left, b.top, b.right, b.bottom)
        }
        reportDisplay(f != null)
    }

    fun start() {
        reportDisplay()
        tracker.addWindowLayoutInfoListener(activity, ContextCompat.getMainExecutor(activity), listener)
    }

    fun stop() {
        tracker.removeWindowLayoutInfoListener(listener)
    }

    /** Density, and whether this looks like the big screen (a hinge is visible, or it is squarish). */
    fun reportDisplay(hasHinge: Boolean? = null) {
        val m = WindowMetricsCalculator.getOrCreate().computeCurrentWindowMetrics(activity).bounds
        val dm: DisplayMetrics = activity.resources.displayMetrics
        val ppi = (dm.xdpi + dm.ydpi) / 2f
        val lo = minOf(m.width(), m.height()).toFloat()
        val hi = maxOf(m.width(), m.height()).toFloat()
        val inner = when {
            hasHinge == true -> 1
            hi > 0 && lo / hi >= 0.70f -> 1
            else -> 0
        }
        // A tablet: a large screen (600dp+) on a device with no hinge sensor.
        // Unfolded foldables are just as large, but they have the sensor.
        val sw = activity.resources.configuration.smallestScreenWidthDp
        val hinge = activity.packageManager.hasSystemFeature("android.hardware.sensor.hinge_angle")
        val tablet = sw >= 600 && !hinge
        Log.i(TAG, "display: ${m.width()}x${m.height()} ppi=$ppi inner=$inner tablet=$tablet")
        nativeReportDisplay(if (tablet) 1 else inner, ppi)
        nativeReportTablet(tablet)
    }

    companion object {
        private const val TAG = "OS999Fold"
        const val STATE_NONE = 0
        const val STATE_FLAT = 1
        const val STATE_HALF = 2
        const val AXIS_NONE = 0
        const val AXIS_HORIZONTAL = 1
        const val AXIS_VERTICAL = 2

        @JvmStatic external fun nativeReportFold(state: Int, axis: Int, l: Int, t: Int, r: Int, b: Int)
        @JvmStatic external fun nativeReportDisplay(inner: Int, ppi: Float)
        @JvmStatic external fun nativeReportInsets(l: Int, t: Int, r: Int, b: Int)
        @JvmStatic external fun nativeReportHardwareKeyboard(attached: Boolean)
        @JvmStatic external fun nativeReportTablet(tablet: Boolean)
        @JvmStatic external fun nativeReportMagnify(factor: Float)
        @JvmStatic external fun nativeReportScroll(lines: Float)
    }
}

package org.remnantsecurity.os999

import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.window.OnBackInvokedDispatcher
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.libsdl.app.SDLActivity

/**
 * OS999 on Android: SDL runs the desktop; this activity tells it how the
 * phone is folded, where the system bars are, and renders its haptics.
 */
class OS999Activity : SDLActivity() {
    private lateinit var fold: FoldBridge

    override fun getLibraries(): Array<String> = arrayOf("SDL2", "main")

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen()
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        ViewCompat.setOnApplyWindowInsetsListener(window.decorView) { v, insets ->
            // Only the camera cut-out is truly unusable. The back-gesture
            // zones along the edges still show content, and the system bars
            // are hidden, so they don't shrink the desktop.
            val i = insets.getInsets(WindowInsetsCompat.Type.displayCutout())
            FoldBridge.nativeReportInsets(i.left, i.top, i.right, i.bottom)
            ViewCompat.onApplyWindowInsets(v, insets)
        }
        fold = FoldBridge(this)
        fold.start()
        // Back (edge swipe, nav button, mouse back) acts as Escape inside
        // OS999 instead of closing it: menus and dialogs close, the desktop
        // stays. Android 13+ (and predictive back, the default from 16)
        // routes back through this callback rather than onBackPressed.
        if (Build.VERSION.SDK_INT >= 33) {
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT) { sendEscape() }
        }
        reportKeyboard(resources.configuration)
    }

    @Deprecated("Android 12 and earlier; 13+ uses the OnBackInvokedCallback above")
    override fun onBackPressed() {
        sendEscape()
    }

    private fun sendEscape() {
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE)
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE)
    }

    // Touchpads on Android 14+ (laptop-style keyboard covers, Bluetooth
    // trackpads): the system recognises pinches and two-finger swipes itself
    // and sends them as a fake finger on a mouse source. SDL would see that
    // finger as the pointer jumping about, so OS999 takes them here and turns
    // them into a zoom and a scroll instead.
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean =
        touchpadGesture(ev) || super.dispatchTouchEvent(ev)

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
        touchpadGesture(ev) || super.dispatchGenericMotionEvent(ev)

    private fun touchpadGesture(ev: MotionEvent): Boolean {
        if (Build.VERSION.SDK_INT < 34) return false
        when (ev.classification) {
            MotionEvent.CLASSIFICATION_PINCH -> {
                if (ev.actionMasked == MotionEvent.ACTION_MOVE)
                    FoldBridge.nativeReportMagnify(ev.getAxisValue(MotionEvent.AXIS_GESTURE_PINCH_SCALE_FACTOR))
            }
            MotionEvent.CLASSIFICATION_TWO_FINGER_SWIPE -> {
                if (ev.actionMasked == MotionEvent.ACTION_MOVE) {
                    // Pixels the content should scroll; positive is down. One
                    // OS999 wheel line is about 24 dp of finger travel.
                    val line = 24f * resources.displayMetrics.density
                    FoldBridge.nativeReportScroll(-ev.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_Y_DISTANCE) / line)
                }
            }
            else -> return false
        }
        return true
    }

    override fun onDestroy() {
        fold.stop()
        super.onDestroy()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        reportKeyboard(newConfig)
        fold.reportDisplay()
    }

    private fun reportKeyboard(c: Configuration) {
        FoldBridge.nativeReportHardwareKeyboard(
            c.keyboard == Configuration.KEYBOARD_QWERTY &&
                c.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO)
    }

    /**
     * Called from native code with the keyboard's haptic vocabulary
     * (keyboard/docs/HAPTICS.md): KEY, KEY_WIDE, MODIFIER, SELECTION, TAP,
     * CLICK, RELEASE, FORCE, MODE = 0..8.
     */
    @Suppress("unused")
    fun performOs999Haptic(type: Int, strength: Float, sharpness: Float, durationMs: Int) {
        val vibrator = if (Build.VERSION.SDK_INT >= 31)
            (getSystemService(VibratorManager::class.java)).defaultVibrator
        else @Suppress("DEPRECATION") getSystemService(Vibrator::class.java)
        if (vibrator == null || !vibrator.hasVibrator()) {
            window.decorView.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            return
        }
        if (Build.VERSION.SDK_INT >= 30) {
            val primitive = when (type) {
                0, 3 -> VibrationEffect.Composition.PRIMITIVE_TICK
                7 -> VibrationEffect.Composition.PRIMITIVE_THUD
                8 -> VibrationEffect.Composition.PRIMITIVE_LOW_TICK
                else -> VibrationEffect.Composition.PRIMITIVE_CLICK
            }
            if (vibrator.areAllPrimitivesSupported(primitive)) {
                val scale = if (type == 6) strength * 0.6f else strength
                vibrator.vibrate(VibrationEffect.startComposition()
                    .addPrimitive(primitive, scale.coerceIn(0f, 1f)).compose())
                return
            }
        }
        val amplitude = (strength * 255).toInt().coerceIn(1, 255)
        vibrator.vibrate(VibrationEffect.createOneShot(durationMs.coerceAtLeast(8).toLong(), amplitude))
    }
}

package Ali.Xanite

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.KeyEvent
import org.libsdl.app.SDLControllerManager

class ControllerInputBridge : OnScreenController.ControllerListener {

    private data class ButtonDispatchState(
        var isDown: Boolean = false,
        var pressedAtMs: Long = 0L,
        var pendingRelease: Runnable? = null,
    )

    companion object {
        const val VIRTUAL_DEVICE_ID = -2
        private const val MIN_TAP_HOLD_MS = 50L

        const val AXIS_LEFT_X        = 0
        const val AXIS_LEFT_Y        = 1
        const val AXIS_RIGHT_X       = 2
        const val AXIS_RIGHT_Y       = 3
        const val AXIS_LEFT_TRIGGER  = 4
        const val AXIS_RIGHT_TRIGGER = 5
    }

    private val mainHandler = Handler(Looper.getMainLooper())
    private val buttonStates = mutableMapOf<OnScreenController.Button, ButtonDispatchState>()

    override fun onButtonPressed(button: OnScreenController.Button) {
        val state = buttonStates.getOrPut(button) { ButtonDispatchState() }
        state.pendingRelease?.let(mainHandler::removeCallbacks)
        state.pendingRelease = null
        state.pressedAtMs = SystemClock.uptimeMillis()
        if (state.isDown) return
        state.isDown = true

        try {
            dispatchButtonState(button, pressed = true)
        } catch (e: Exception) {
            android.util.Log.e("ControllerBridge", "Error on button press [${button.name}]: ${e.message}")
        }
    }

    override fun onButtonReleased(button: OnScreenController.Button) {
        val state = buttonStates.getOrPut(button) { ButtonDispatchState() }
        state.pendingRelease?.let(mainHandler::removeCallbacks)
        state.pendingRelease = null
        if (!state.isDown) return

        val elapsed = SystemClock.uptimeMillis() - state.pressedAtMs
        val remaining = MIN_TAP_HOLD_MS - elapsed

        if (remaining > 0L) {
            val releaseTask = Runnable {
                state.pendingRelease = null
                if (!state.isDown) return@Runnable
                state.isDown = false
                try {
                    dispatchButtonState(button, pressed = false)
                } catch (e: Exception) {
                    android.util.Log.e("ControllerBridge", "Delayed release error [${button.name}]: ${e.message}")
                }
            }
            state.pendingRelease = releaseTask
            mainHandler.postDelayed(releaseTask, remaining)
            return
        }

        state.isDown = false
        try {
            dispatchButtonState(button, pressed = false)
        } catch (e: Exception) {
            android.util.Log.e("ControllerBridge", "Error on button release [${button.name}]: ${e.message}")
        }
    }

    override fun onStickMoved(stick: OnScreenController.Stick, x: Float, y: Float) {
        try {
            when (stick) {
                OnScreenController.Stick.LEFT -> {
                    SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_LEFT_X, x)
                    SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_LEFT_Y, y)
                }
                OnScreenController.Stick.RIGHT -> {
                    SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_RIGHT_X, x)
                    SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_RIGHT_Y, y)
                }
            }
        } catch (e: Exception) {
            android.util.Log.e("ControllerBridge", "Error on stick move [${stick.name}]: ${e.message}")
        }
    }

    fun reset() {
        buttonStates.forEach { (button, state) ->
            state.pendingRelease?.let(mainHandler::removeCallbacks)
            state.pendingRelease = null
            if (!state.isDown) return@forEach
            state.isDown = false
            try {
                dispatchButtonState(button, pressed = false)
            } catch (e: Exception) {
                android.util.Log.e("ControllerBridge", "Error resetting ${button.name}: ${e.message}")
            }
        }
    }

    private fun dispatchButtonState(button: OnScreenController.Button, pressed: Boolean) {
        when (button) {
            OnScreenController.Button.LT,
            OnScreenController.Button.RT -> setTriggerState(button, pressed)
            else -> {
                val keyCode = getKeyCodeForButton(button)
                if (keyCode != KeyEvent.KEYCODE_UNKNOWN) {
                    if (pressed) SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, keyCode)
                    else         SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, keyCode)
                }
            }
        }
    }

    private fun setTriggerState(button: OnScreenController.Button, pressed: Boolean) {
        val axis = when (button) {
            OnScreenController.Button.LT -> AXIS_LEFT_TRIGGER
            OnScreenController.Button.RT -> AXIS_RIGHT_TRIGGER
            else -> return
        }
        val keyCode = getKeyCodeForButton(button)

        val axisValue = if (pressed) 1.0f else -1.0f

        if (keyCode != KeyEvent.KEYCODE_UNKNOWN) {
            try {
                if (pressed) SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, keyCode)
                else         SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, keyCode)
            } catch (e: Exception) {
                android.util.Log.e("ControllerBridge", "SDL pad event failed for ${button.name}: ${e.message}")
            }
        }
        try {
            SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, axis, axisValue)
        } catch (e: Exception) {
            android.util.Log.e("ControllerBridge", "SDL joy event failed for ${button.name}: ${e.message}")
        }
    }

    private fun getKeyCodeForButton(button: OnScreenController.Button): Int {
        return when (button) {
            OnScreenController.Button.CROSS    -> KeyEvent.KEYCODE_BUTTON_A
            OnScreenController.Button.CIRCLE   -> KeyEvent.KEYCODE_BUTTON_B
            OnScreenController.Button.SQUARE   -> KeyEvent.KEYCODE_BUTTON_X
            OnScreenController.Button.TRIANGLE -> KeyEvent.KEYCODE_BUTTON_Y

            OnScreenController.Button.DPAD_UP    -> KeyEvent.KEYCODE_DPAD_UP
            OnScreenController.Button.DPAD_DOWN  -> KeyEvent.KEYCODE_DPAD_DOWN
            OnScreenController.Button.DPAD_LEFT  -> KeyEvent.KEYCODE_DPAD_LEFT
            OnScreenController.Button.DPAD_RIGHT -> KeyEvent.KEYCODE_DPAD_RIGHT

            OnScreenController.Button.BLACK -> KeyEvent.KEYCODE_BUTTON_R1
            OnScreenController.Button.WHITE -> KeyEvent.KEYCODE_BUTTON_L1

            OnScreenController.Button.LT -> KeyEvent.KEYCODE_BUTTON_L2
            OnScreenController.Button.RT -> KeyEvent.KEYCODE_BUTTON_R2

            OnScreenController.Button.L3 -> KeyEvent.KEYCODE_BUTTON_THUMBL
            OnScreenController.Button.R3 -> KeyEvent.KEYCODE_BUTTON_THUMBR

            OnScreenController.Button.START  -> KeyEvent.KEYCODE_BUTTON_START
            OnScreenController.Button.SELECT -> KeyEvent.KEYCODE_BUTTON_SELECT
            OnScreenController.Button.MENU   -> KeyEvent.KEYCODE_UNKNOWN
        }
    }
}

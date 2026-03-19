package Ali.Xanite

import android.view.KeyEvent
import org.libsdl.app.SDLControllerManager

class ControllerInputBridge : OnScreenController.ControllerListener {

  companion object {

    const val VIRTUAL_DEVICE_ID = -2

    const val AXIS_LEFT_X = 0
    const val AXIS_LEFT_Y = 1
    const val AXIS_RIGHT_X = 2
    const val AXIS_RIGHT_Y = 3
    const val AXIS_LEFT_TRIGGER = 4
    const val AXIS_RIGHT_TRIGGER = 5
  }
  
  override fun onButtonPressed(button: OnScreenController.Button) {
    try {
      when (button) {

        OnScreenController.Button.LT ->
          SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_LEFT_TRIGGER, 1.0f)
        OnScreenController.Button.RT ->
          SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_RIGHT_TRIGGER, 1.0f)

        OnScreenController.Button.L3 ->
          SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBL)
        OnScreenController.Button.R3 ->
          SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBR)


        else -> {
          val keyCode = getKeyCodeForButton(button)
          SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, keyCode)
        }
      }
    } catch (e: Exception) {
      android.util.Log.e("ControllerBridge", "Error on button press: ${e.message}")
    }
  }

  override fun onButtonReleased(button: OnScreenController.Button) {
    try {
      when (button) {

        OnScreenController.Button.LT ->
          SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_LEFT_TRIGGER, 0.0f)
        OnScreenController.Button.RT ->
          SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, AXIS_RIGHT_TRIGGER, 0.0f)

        OnScreenController.Button.L3 ->
          SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBL)
        OnScreenController.Button.R3 ->
          SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBR)

        else -> {
          val keyCode = getKeyCodeForButton(button)
          SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, keyCode)
        }
      }
    } catch (e: Exception) {
      android.util.Log.e("ControllerBridge", "Error on button release: ${e.message}")
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
      android.util.Log.e("ControllerBridge", "Error on stick move: ${e.message}")
    }
  }

  override fun onStickPressed(stick: OnScreenController.Stick) {
    try {

      when (stick) {
        OnScreenController.Stick.LEFT -> 
          SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBL)
        OnScreenController.Stick.RIGHT -> 
          SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBR)
      }
    } catch (e: Exception) {
      android.util.Log.e("ControllerBridge", "Error on stick press: ${e.message}")
    }
  }

  override fun onStickReleased(stick: OnScreenController.Stick) {
    try {
      when (stick) {
        OnScreenController.Stick.LEFT -> 
          SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBL)
        OnScreenController.Stick.RIGHT -> 
          SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, KeyEvent.KEYCODE_BUTTON_THUMBR)
      }
    } catch (e: Exception) {
      android.util.Log.e("ControllerBridge", "Error on stick release: ${e.message}")
    }
  }

  private fun getKeyCodeForButton(button: OnScreenController.Button): Int {
    return when (button) {

      OnScreenController.Button.CROSS -> KeyEvent.KEYCODE_BUTTON_A      
      OnScreenController.Button.CIRCLE -> KeyEvent.KEYCODE_BUTTON_B     
      OnScreenController.Button.SQUARE -> KeyEvent.KEYCODE_BUTTON_X     
      OnScreenController.Button.TRIANGLE -> KeyEvent.KEYCODE_BUTTON_Y   

      OnScreenController.Button.DPAD_UP -> KeyEvent.KEYCODE_DPAD_UP
      OnScreenController.Button.DPAD_DOWN -> KeyEvent.KEYCODE_DPAD_DOWN
      OnScreenController.Button.DPAD_LEFT -> KeyEvent.KEYCODE_DPAD_LEFT
      OnScreenController.Button.DPAD_RIGHT -> KeyEvent.KEYCODE_DPAD_RIGHT

      OnScreenController.Button.START -> KeyEvent.KEYCODE_BUTTON_START
      OnScreenController.Button.SELECT -> KeyEvent.KEYCODE_BUTTON_SELECT
      
      OnScreenController.Button.BLACK -> KeyEvent.KEYCODE_BUTTON_R1     
      OnScreenController.Button.WHITE -> KeyEvent.KEYCODE_BUTTON_L1     

      OnScreenController.Button.LT, OnScreenController.Button.RT,
      OnScreenController.Button.L3, OnScreenController.Button.R3 -> 
        KeyEvent.KEYCODE_UNKNOWN
    }
  }
}

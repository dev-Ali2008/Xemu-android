#ifndef XANITE_INPUT_MANAGER_H
#define XANITE_INPUT_MANAGER_H

#include <unordered_map>
#include <vector>
#include <cstdint>

namespace xanite {

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Touch input
    void OnTouchEvent(int pointer_id, float x, float y, bool is_down);
    
    // Key input
    void OnKeyEvent(int key_code, bool is_down);
    
    // Controller input (external gamepads)
    void OnControllerEvent(int controller_id, int button, float value);
    
    // Motion input (accelerometer/gyro)
    void OnMotionEvent(float x, float y, float z);
    
    // Frame processing
    void ProcessFrame();
    
    // State queries
    bool IsButtonPressed(int button) const;
    float GetAnalogValue(int axis) const;
    void GetTouchState(int pointer_id, float& x, float& y, bool& is_down) const;

    // Configuration
    void SetTouchSensitivity(float sensitivity) { touch_sensitivity_ = sensitivity; }
    void SetAnalogDeadzone(float deadzone) { analog_deadzone_ = deadzone; }

private:
    struct TouchPoint {
        float x = 0.0f;
        float y = 0.0f;
        bool is_down = false;
        uint64_t timestamp = 0;
    };
    
    struct ControllerState {
        float left_trigger = 0.0f;
        float right_trigger = 0.0f;
        float left_stick_x = 0.0f;
        float left_stick_y = 0.0f;
        float right_stick_x = 0.0f;
        float right_stick_y = 0.0f;
        uint32_t buttons = 0;
    };
    
    // Touch input state
    std::unordered_map<int, TouchPoint> touch_points_;
    int max_touch_points_ = 10;
    
    // Controller state
    ControllerState controller_state_;
    
    // Key state
    std::unordered_map<int, bool> key_states_;
    
    // Motion state
    float accelerometer_[3] = {0.0f, 0.0f, 0.0f};
    float gyroscope_[3] = {0.0f, 0.0f, 0.0f};
    
    // Configuration
    float analog_deadzone_ = 0.15f;
    float trigger_deadzone_ = 0.1f;
    float touch_sensitivity_ = 1.0f;
    
    // Virtual gamepad mapping
    struct VirtualButtonZone {
        float x, y, width, height;
        int button_mask;
    };
    std::vector<VirtualButtonZone> virtual_buttons_;
    
    // Utility methods
    void UpdateVirtualGamepad();
    void ProcessGestures();
    float ApplyDeadzone(float value, float deadzone);
    void SendToXeniaInputSystem();
    void InitializeVirtualLayout();
    
    // Gesture recognition
    bool DetectSwipeGesture(const TouchPoint& start, const TouchPoint& current);
    bool DetectTapGesture(const TouchPoint& point);
    bool DetectPinchGesture();
    
    // Virtual button handling
    int GetVirtualButtonAt(float x, float y);
};

} // namespace xanite

#endif // XANITE_INPUT_MANAGER_H
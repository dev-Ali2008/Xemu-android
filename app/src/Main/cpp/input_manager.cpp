#include "input_manager.h"
#include <cmath>
#include <chrono>
#include "xenia/base/logging.h"

namespace xanite {

InputManager::InputManager() {
    XELOGI("InputManager initialized");
    
    // Initialize default controller state
    controller_state_ = ControllerState();
    
    // Pre-allocate touch points
    for (int i = 0; i < max_touch_points_; ++i) {
        touch_points_[i] = TouchPoint();
    }
    
    // Initialize virtual button layout
    InitializeVirtualLayout();
}

InputManager::~InputManager() {
    XELOGI("InputManager shutdown");
}

void InputManager::InitializeVirtualLayout() {
    // Define virtual button zones for touch screen
    // These are example positions - adjust based on your UI layout
    
    // A button (right side)
    virtual_buttons_.push_back({0.8f, 0.6f, 0.1f, 0.1f, 0x1000}); // A button
    
    // B button (right side)
    virtual_buttons_.push_back({0.7f, 0.5f, 0.1f, 0.1f, 0x2000}); // B button
    
    // X button (right side)
    virtual_buttons_.push_back({0.7f, 0.7f, 0.1f, 0.1f, 0x4000}); // X button
    
    // Y button (right side)
    virtual_buttons_.push_back({0.6f, 0.6f, 0.1f, 0.1f, 0x8000}); // Y button
    
    // Start button (center top)
    virtual_buttons_.push_back({0.5f, 0.1f, 0.08f, 0.08f, 0x0010}); // Start
    
    // Select button (center top)
    virtual_buttons_.push_back({0.4f, 0.1f, 0.08f, 0.08f, 0x0020}); // Select
    
    // Left shoulder (top left)
    virtual_buttons_.push_back({0.1f, 0.1f, 0.15f, 0.08f, 0x0100}); // LB
    
    // Right shoulder (top right)
    virtual_buttons_.push_back({0.75f, 0.1f, 0.15f, 0.08f, 0x0200}); // RB
}

void InputManager::OnTouchEvent(int pointer_id, float x, float y, bool is_down) {
    if (pointer_id < 0 || pointer_id >= max_touch_points_) {
        XELOGW("Invalid touch pointer ID: %d", pointer_id);
        return;
    }
    
    auto& point = touch_points_[pointer_id];
    point.x = x;
    point.y = y;
    point.is_down = is_down;
    point.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    XELOGD("Touch event: pointer=%d, x=%.2f, y=%.2f, down=%d", pointer_id, x, y, is_down);
    
    // Process virtual buttons
    if (is_down) {
        int button_mask = GetVirtualButtonAt(x, y);
        if (button_mask != 0) {
            controller_state_.buttons |= button_mask;
            XELOGD("Virtual button pressed: 0x%04X", button_mask);
        }
        
        // Process gestures
        if (DetectTapGesture(point)) {
            XELOGD("Tap gesture detected at (%.2f, %.2f)", x, y);
        }
    } else {
        // Clear all virtual buttons for this pointer
        // In a real implementation, you'd track which button was pressed by which pointer
        // This is a simplified version
        int button_mask = GetVirtualButtonAt(x, y);
        if (button_mask != 0) {
            controller_state_.buttons &= ~button_mask;
            XELOGD("Virtual button released: 0x%04X", button_mask);
        }
    }
}

void InputManager::OnKeyEvent(int key_code, bool is_down) {
    key_states_[key_code] = is_down;
    
    // Map Android key codes to Xbox controller buttons
    switch (key_code) {
        case 96: // KEYCODE_BUTTON_A
            if (is_down) controller_state_.buttons |= 0x1000;
            else controller_state_.buttons &= ~0x1000;
            break;
        case 97: // KEYCODE_BUTTON_B
            if (is_down) controller_state_.buttons |= 0x2000;
            else controller_state_.buttons &= ~0x2000;
            break;
        case 99: // KEYCODE_BUTTON_X
            if (is_down) controller_state_.buttons |= 0x4000;
            else controller_state_.buttons &= ~0x4000;
            break;
        case 100: // KEYCODE_BUTTON_Y
            if (is_down) controller_state_.buttons |= 0x8000;
            else controller_state_.buttons &= ~0x8000;
            break;
        case 102: // KEYCODE_BUTTON_L1
            if (is_down) controller_state_.buttons |= 0x0100;
            else controller_state_.buttons &= ~0x0100;
            break;
        case 103: // KEYCODE_BUTTON_R1
            if (is_down) controller_state_.buttons |= 0x0200;
            else controller_state_.buttons &= ~0x0200;
            break;
        case 106: // KEYCODE_BUTTON_START
            if (is_down) controller_state_.buttons |= 0x0010;
            else controller_state_.buttons &= ~0x0010;
            break;
        case 107: // KEYCODE_BUTTON_SELECT
            if (is_down) controller_state_.buttons |= 0x0020;
            else controller_state_.buttons &= ~0x0020;
            break;
        case 19: // KEYCODE_DPAD_UP
            if (is_down) controller_state_.buttons |= 0x0001;
            else controller_state_.buttons &= ~0x0001;
            break;
        case 20: // KEYCODE_DPAD_DOWN
            if (is_down) controller_state_.buttons |= 0x0002;
            else controller_state_.buttons &= ~0x0002;
            break;
        case 21: // KEYCODE_DPAD_LEFT
            if (is_down) controller_state_.buttons |= 0x0004;
            else controller_state_.buttons &= ~0x0004;
            break;
        case 22: // KEYCODE_DPAD_RIGHT
            if (is_down) controller_state_.buttons |= 0x0008;
            else controller_state_.buttons &= ~0x0008;
            break;
        default:
            XELOGD("Key event: code=%d, down=%d", key_code, is_down);
            break;
    }
}

void InputManager::OnControllerEvent(int controller_id, int button, float value) {
    // Handle external gamepad input
    switch (button) {
        case 0: // Left stick X
            controller_state_.left_stick_x = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 1: // Left stick Y
            controller_state_.left_stick_y = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 2: // Right stick X
            controller_state_.right_stick_x = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 3: // Right stick Y
            controller_state_.right_stick_y = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 4: // Left trigger
            controller_state_.left_trigger = ApplyDeadzone(value, trigger_deadzone_);
            break;
        case 5: // Right trigger
            controller_state_.right_trigger = ApplyDeadzone(value, trigger_deadzone_);
            break;
        default:
            // Handle button presses
            if (value > 0.5f) {
                controller_state_.buttons |= (1 << button);
            } else {
                controller_state_.buttons &= ~(1 << button);
            }
            break;
    }
    
    XELOGD("Controller event: id=%d, button=%d, value=%.2f", controller_id, button, value);
}

void InputManager::OnMotionEvent(float x, float y, float z) {
    // Update motion sensors
    accelerometer_[0] = x;
    accelerometer_[1] = y;
    accelerometer_[2] = z;
    
    // Could be used for tilt controls or motion gestures
    XELOGD("Motion event: x=%.2f, y=%.2f, z=%.2f", x, y, z);
}

void InputManager::ProcessFrame() {
    // Update virtual gamepad from touch input
    UpdateVirtualGamepad();
    
    // Process continuous gestures
    ProcessGestures();
    
    // Send input state to Xenia
    SendToXeniaInputSystem();
}

bool InputManager::IsButtonPressed(int button) const {
    return (controller_state_.buttons & button) != 0;
}

float InputManager::GetAnalogValue(int axis) const {
    switch (axis) {
        case 0: return controller_state_.left_stick_x;
        case 1: return controller_state_.left_stick_y;
        case 2: return controller_state_.right_stick_x;
        case 3: return controller_state_.right_stick_y;
        case 4: return controller_state_.left_trigger;
        case 5: return controller_state_.right_trigger;
        default: return 0.0f;
    }
}

void InputManager::GetTouchState(int pointer_id, float& x, float& y, bool& is_down) const {
    auto it = touch_points_.find(pointer_id);
    if (it != touch_points_.end()) {
        x = it->second.x;
        y = it->second.y;
        is_down = it->second.is_down;
    } else {
        x = 0.0f;
        y = 0.0f;
        is_down = false;
    }
}

void InputManager::UpdateVirtualGamepad() {
    // Convert touch input to virtual analog sticks
    // This is a simplified version - you'd want more sophisticated stick handling
    
    // Reset analog sticks
    controller_state_.left_stick_x = 0.0f;
    controller_state_.left_stick_y = 0.0f;
    controller_state_.right_stick_x = 0.0f;
    controller_state_.right_stick_y = 0.0f;
    
    // Look for touch points in analog stick zones
    for (const auto& pair : touch_points_) {
        if (!pair.second.is_down) continue;
        
        float x = pair.second.x;
        float y = pair.second.y;
        
        // Left analog stick zone (bottom left)
        if (x < 0.3f && y > 0.6f) {
            // Convert touch position to stick values
            controller_state_.left_stick_x = (x - 0.15f) / 0.15f;
            controller_state_.left_stick_y = (y - 0.75f) / 0.15f;
            
            // Apply deadzone
            controller_state_.left_stick_x = ApplyDeadzone(controller_state_.left_stick_x, analog_deadzone_);
            controller_state_.left_stick_y = ApplyDeadzone(controller_state_.left_stick_y, analog_deadzone_);
        }
        
        // Right analog stick zone (bottom right)
        if (x > 0.7f && y > 0.6f) {
            // Convert touch position to stick values
            controller_state_.right_stick_x = (x - 0.85f) / 0.15f;
            controller_state_.right_stick_y = (y - 0.75f) / 0.15f;
            
            // Apply deadzone
            controller_state_.right_stick_x = ApplyDeadzone(controller_state_.right_stick_x, analog_deadzone_);
            controller_state_.right_stick_y = ApplyDeadzone(controller_state_.right_stick_y, analog_deadzone_);
        }
    }
}

void InputManager::ProcessGestures() {
    // Process multi-touch gestures
    int active_touches = 0;
    for (const auto& pair : touch_points_) {
        if (pair.second.is_down) {
            active_touches++;
        }
    }
    
    // Two-finger gestures
    if (active_touches >= 2) {
        // Look for pinch gesture
        if (DetectPinchGesture()) {
            XELOGD("Pinch gesture detected");
            // Could be used for zoom or other actions
        }
    }
    
    // Single finger gestures
    for (const auto& pair : touch_points_) {
        if (pair.second.is_down) {
            // Could detect swipe, long press, etc.
            // DetectSwipeGesture(...);
        }
    }
}

float InputManager::ApplyDeadzone(float value, float deadzone) {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }
    
    // Apply circular deadzone for sticks, linear for triggers
    if (deadzone == analog_deadzone_) {
        // Circular deadzone for analog sticks
        float magnitude = std::sqrt(value * value);
        if (magnitude < deadzone) {
            return 0.0f;
        }
        
        // Scale to compensate for deadzone
        float scale = (magnitude - deadzone) / (1.0f - deadzone);
        return (value / magnitude) * scale;
    } else {
        // Linear deadzone for triggers
        return (value - deadzone) / (1.0f - deadzone);
    }
}

void InputManager::SendToXeniaInputSystem() {
    // This function would send the current input state to Xenia's HID system
    // For now, we just log the state for debugging
    
    XELOGD("Input State: L(%.2f,%.2f) R(%.2f,%.2f) LT:%.2f RT:%.2f Buttons:%04X",
           controller_state_.left_stick_x, controller_state_.left_stick_y,
           controller_state_.right_stick_x, controller_state_.right_stick_y,
           controller_state_.left_trigger, controller_state_.right_trigger,
           controller_state_.buttons);
    
    // TODO: Integrate with Xenia's hid::InputSystem
    // This would involve:
    // 1. Getting the emulator instance
    // 2. Getting the input system from the emulator
    // 3. Creating XInput state packets
    // 4. Sending them to the input system
}

bool InputManager::DetectSwipeGesture(const TouchPoint& start, const TouchPoint& current) {
    float dx = current.x - start.x;
    float dy = current.y - start.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // Minimum swipe distance threshold
    const float SWIPE_THRESHOLD = 50.0f;
    
    if (distance > SWIPE_THRESHOLD) {
        // Determine swipe direction
        float angle = std::atan2(dy, dx) * 180.0f / 3.14159f;
        
        XELOGD("Swipe detected: distance=%.2f, angle=%.2f", distance, angle);
        return true;
    }
    
    return false;
}

bool InputManager::DetectTapGesture(const TouchPoint& point) {
    // Simple tap detection - in real implementation, you'd check timing
    // For now, just consider any quick touch as a tap
    return true;
}

bool InputManager::DetectPinchGesture() {
    // Simple pinch detection - look for two moving touch points
    int touch_count = 0;
    TouchPoint points[2];
    
    for (const auto& pair : touch_points_) {
        if (pair.second.is_down && touch_count < 2) {
            points[touch_count++] = pair.second;
        }
    }
    
    if (touch_count == 2) {
        // Calculate distance between two points
        float dx = points[0].x - points[1].x;
        float dy = points[0].y - points[1].y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        // You'd compare with previous distance to detect pinch/zoom
        // For now, just return true if two points are active
        return true;
    }
    
    return false;
}

int InputManager::GetVirtualButtonAt(float x, float y) {
    for (const auto& zone : virtual_buttons_) {
        if (x >= zone.x && x <= zone.x + zone.width &&
            y >= zone.y && y <= zone.y + zone.height) {
            return zone.button_mask;
        }
    }
    return 0;
}

} // namespace xanite
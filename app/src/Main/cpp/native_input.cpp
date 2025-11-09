#include <cmath>
#include <unordered_map>
#include <vector>
#include "xenia/base/logging.h"

namespace xanite {

class NativeInput {
public:
    NativeInput();
    ~NativeInput();
   
    void OnTouchEvent(int pointer_id, float x, float y, bool is_down);
        
    void OnKeyEvent(int key_code, bool is_down);
    
    void OnControllerEvent(int controller_id, int button, float value);
      
    void OnMotionEvent(float x, float y, float z);
    
    void ProcessFrame();
    
    bool IsButtonPressed(int button) const;
    float GetAnalogValue(int axis) const;
    void GetTouchState(int pointer_id, float& x, float& y, bool& is_down) const;

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
    
    
    std::unordered_map<int, TouchPoint> touch_points_;
    int max_touch_points_ = 10;
    
    ControllerState controller_state_;
       
    std::unordered_map<int, bool> key_states_;
       
    float accelerometer_[3] = {0.0f, 0.0f, 0.0f};
    float gyroscope_[3] = {0.0f, 0.0f, 0.0f};
       
    float analog_deadzone_ = 0.15f;
    float trigger_deadzone_ = 0.1f;
    float touch_sensitivity_ = 1.0f;
        
    void UpdateVirtualGamepad();
    void ProcessGestures();
    float ApplyDeadzone(float value, float deadzone);
    void SendToXeniaInputSystem();
        
    bool DetectSwipeGesture(const TouchPoint& start, const TouchPoint& current);
    bool DetectTapGesture(const TouchPoint& point);
    bool DetectPinchGesture();
};

NativeInput::NativeInput() {
    XELOGI("NativeInput initialized");
       
    controller_state_ = ControllerState();   
    
    for (int i = 0; i < max_touch_points_; ++i) {
        touch_points_[i] = TouchPoint();
    }
}

NativeInput::~NativeInput() {
    XELOGI("NativeInput shutdown");
}

void NativeInput::OnTouchEvent(int pointer_id, float x, float y, bool is_down) {
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
    
    if (is_down) {
        if (DetectTapGesture(point)) {
            
            XELOGD("Tap gesture detected at (%.2f, %.2f)", x, y);
        }
    }
}

void NativeInput::OnKeyEvent(int key_code, bool is_down) {
    key_states_[key_code] = is_down;
      
    switch (key_code) {
        case 96: 
            if (is_down) controller_state_.buttons |= 0x1000;
            else controller_state_.buttons &= ~0x1000;
            break;
        case 97: 
            if (is_down) controller_state_.buttons |= 0x2000;
            else controller_state_.buttons &= ~0x2000;
            break;
        case 99: 
            if (is_down) controller_state_.buttons |= 0x4000;
            else controller_state_.buttons &= ~0x4000;
            break;
        case 100: 
            if (is_down) controller_state_.buttons |= 0x8000;
            else controller_state_.buttons &= ~0x8000;
            break;
        case 102: 
            if (is_down) controller_state_.buttons |= 0x0100;
            else controller_state_.buttons &= ~0x0100;
            break;
        case 103: 
            if (is_down) controller_state_.buttons |= 0x0200;
            else controller_state_.buttons &= ~0x0200;
            break;
        case 106: 
            if (is_down) controller_state_.buttons |= 0x0010;
            else controller_state_.buttons &= ~0x0010;
            break;
        case 107: 
            if (is_down) controller_state_.buttons |= 0x0020;
            else controller_state_.buttons &= ~0x0020;
            break;
        default:
            
            break;
    }
}

void NativeInput::OnControllerEvent(int controller_id, int button, float value) {
    
    switch (button) {
        case 0: 
            controller_state_.left_stick_x = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 1: 
            controller_state_.left_stick_y = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 2: 
            controller_state_.right_stick_x = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 3: 
            controller_state_.right_stick_y = ApplyDeadzone(value, analog_deadzone_);
            break;
        case 4: 
            controller_state_.left_trigger = ApplyDeadzone(value, trigger_deadzone_);
            break;
        case 5: 
            controller_state_.right_trigger = ApplyDeadzone(value, trigger_deadzone_);
            break;
        default:
            
            if (value > 0.5f) {
                controller_state_.buttons |= (1 << button);
            } else {
                controller_state_.buttons &= ~(1 << button);
            }
            break;
    }
}

void NativeInput::OnMotionEvent(float x, float y, float z) {
    
    accelerometer_[0] = x;
    accelerometer_[1] = y;
    accelerometer_[2] = z;
    
}

void NativeInput::ProcessFrame() {
    
    UpdateVirtualGamepad();
      
    ProcessGestures();
       
    SendToXeniaInputSystem();
     
}

bool NativeInput::IsButtonPressed(int button) const {
    return (controller_state_.buttons & button) != 0;
}

float NativeInput::GetAnalogValue(int axis) const {
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

void NativeInput::GetTouchState(int pointer_id, float& x, float& y, bool& is_down) const {
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

void NativeInput::UpdateVirtualGamepad() {
  //  控制 
    
    int left_thumb_pointer = 0;
    int right_thumb_pointer = 1;
    
    auto left_it = touch_points_.find(left_thumb_pointer);
    auto right_it = touch_points_.find(right_thumb_pointer);
    
    
    if (left_it == touch_points_.end() || !left_it->second.is_down) {
        controller_state_.left_stick_x = 0.0f;
        controller_state_.left_stick_y = 0.0f;
    }
    
    if (right_it == touch_points_.end() || !right_it->second.is_down) {
        controller_state_.right_stick_x = 0.0f;
        controller_state_.right_stick_y = 0.0f;
    }
    // 添加了更多配料 . . . 
}

void NativeInput::ProcessGestures() {
    
    int active_touches = 0;
    for (const auto& pair : touch_points_) {
        if (pair.second.is_down) {
            active_touches++;
        }
    }
    
    
    if (active_touches >= 2) {
        
        if (DetectPinchGesture()) {
            
            XELOGD("Pinch gesture detected");
        }
    }
    
    
    for (const auto& pair : touch_points_) {
        if (pair.second.is_down) {
            
            
        }
    }
}

float NativeInput::ApplyDeadzone(float value, float deadzone) {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }
 
    if (deadzone == analog_deadzone_) {
        
        float magnitude = std::abs(value);
        if (magnitude < deadzone) {
            return 0.0f;
        }
        
        float scale = (magnitude - deadzone) / (1.0f - deadzone);
        return (value / magnitude) * scale;
    } else {
        
        return (value - deadzone) / (1.0f - deadzone);
    }
}

void NativeInput::SendToXeniaInputSystem() {
    // 别忘了在这里添加一些内容。
}

bool NativeInput::DetectSwipeGesture(const TouchPoint& start, const TouchPoint& current) {
    float dx = current.x - start.x;
    float dy = current.y - start.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    
    const float SWIPE_THRESHOLD = 50.0f;
    
    if (distance > SWIPE_THRESHOLD) {
        
        float angle = std::atan2(dy, dx) * 180.0f / 3.14159f;
        
        
        return true;
    }
    
    return false;
}

bool NativeInput::DetectTapGesture(const TouchPoint& point) {
    
    return true;
}

bool NativeInput::DetectPinchGesture() {
    
    int touch_count = 0;
    TouchPoint points[2];
    
    for (const auto& pair : touch_points_) {
        if (pair.second.is_down && touch_count < 2) {
            points[touch_count++] = pair.second;
        }
    }
    
    if (touch_count == 2) {
        
        float dx = points[0].x - points[1].x;
        float dy = points[0].y - points[1].y;
        float distance = std::sqrt(dx * dx + dy * dy);  
        
        return true;
    }
    
    return false;
}

} 

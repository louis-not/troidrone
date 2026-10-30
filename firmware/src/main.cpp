#include <Arduino.h>
#include <mutex>
#include "pins.h"
#include "flap_engine.h"
#include "flight_fsm.h"
#include "servo_ease.h"
#include "servo_out.h"
#include "net.h"

#ifndef FW_VERSION
#define FW_VERSION "0.1.0"
#endif

// Core subsystems
static FlightFsm g_fsm;
static FlapEngine g_flap_engine;
static ServoEase g_servo_ease;
static ServoOut g_servo_out;
static NetManager g_net;

// Synchronization & shared telemetry state
static std::mutex g_state_mutex;
static float g_current_freq_hz = 0.0f;
static FlightState g_last_logged_state = FlightState::DISARMED;

// FreeRTOS control task handle
static TaskHandle_t g_control_task_handle = nullptr;

static void control_task(void* param) {
    (void)param;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(10);  // 10 ms = 100 Hz
    const float dt_s = 0.010f;

    while (true) {
        xTaskDelayUntil(&last_wake_time, period_ticks);

        uint32_t now = millis();
        FlightState current_state;
        float effective_thr = 0.0f;

        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_fsm.step(now);
            current_state = g_fsm.state();
            effective_thr = g_fsm.effective_thr();
        }

        // Log state transition if state changed
        if (current_state != g_last_logged_state) {
            Serial.print("[FSM] State: ");
            Serial.print(FlightFsm::to_string(g_last_logged_state));
            Serial.print(" -> ");
            Serial.println(FlightFsm::to_string(current_state));
            g_last_logged_state = current_state;
        }

        // Flap engine computation
        bool is_armed = (current_state == FlightState::ARMED);
        FlapOutput flap = g_flap_engine.update(dt_s, is_armed, effective_thr);

        // Store frequency for telemetry
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_current_freq_hz = flap.freq_hz;
        }

        // Servo slew / ramp lifecycle easing
        float out_left = 90.0f;
        float out_right = 90.0f;
        bool pwm_active = false;
        g_servo_ease.update(dt_s, current_state, flap.left_deg, flap.right_deg,
                            out_left, out_right, pwm_active);

        // Hardware LEDC PWM update
        g_servo_out.update(out_left, out_right, pwm_active);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);

    // 1. Initialize servo output (pins configured as OUTPUT and driven LOW)
    g_servo_out.begin();

    // 2. Initialize network subsystems (LittleFS, Wi-Fi AP, mDNS, WebServer, WebSocket)
    g_net.begin(g_fsm, g_current_freq_hz, g_state_mutex);

    // 3. Log boot banner
    Serial.println();
    Serial.println("==================================================");
    Serial.println("         TROIDRONE ESP32-C3 FIRMWARE              ");
    Serial.print  ("  Version:         "); Serial.println(FW_VERSION);
    Serial.print  ("  Left Servo Pin:  GPIO"); Serial.println(g_servo_out.left_pin());
    Serial.print  ("  Right Servo Pin: GPIO"); Serial.println(g_servo_out.right_pin());
    Serial.print  ("  Wi-Fi AP SSID:   "); Serial.println(g_net.ssid());
    Serial.println("  AP IP:           192.168.4.1");
    Serial.println("  Initial State:   DISARMED");
    Serial.println("==================================================");

    // 4. Create dedicated 100 Hz FreeRTOS task (priority 2, higher than loopTask)
    BaseType_t ret = xTaskCreate(
        control_task,
        "ctrl_100hz",
        4096,
        nullptr,
        2,
        &g_control_task_handle
    );

    if (ret != pdPASS) {
        Serial.println("[RTOS] ERROR: Failed to create 100 Hz control task!");
    } else {
        Serial.println("[RTOS] 100 Hz control task running");
    }
}

void loop() {
    g_net.update();
    vTaskDelay(pdMS_TO_TICKS(10));
}

#include <Arduino.h>
#include <mutex>
#include "pins.h"
#include "flap_engine.h"
#include "flight_fsm.h"
#include "servo_ease.h"
#include "servo_out.h"
#include "battery_adc.h"
#include "battery.h"
#include "leds.h"
#include "net.h"

#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

// Core subsystems
static FlightFsm g_fsm;
static FlapEngine g_flap_engine;
static ServoEase g_servo_ease;
static ServoOut g_servo_out;
static BatteryMonitor g_battery;
static BatteryAdc g_battery_adc;
static LedDriver g_leds;
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
    uint32_t step_count = 0;

    while (true) {
        xTaskDelayUntil(&last_wake_time, period_ticks);

        uint32_t now = millis();
        step_count++;

        FlightState current_state;
        float effective_thr = 0.0f;
        float effective_str = 0.0f;
        BatteryState batt_state;

        // Battery ADC sampling at 50 Hz (every 2 ticks = 20 ms)
        if ((step_count % 2) == 0) {
            uint32_t adc_mv = g_battery_adc.read_adc_mv();
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_battery.update_adc_mv(adc_mv, 0.020f);
            if (g_battery.is_cutoff_latched()) {
                g_fsm.on_battery_cutoff();
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_fsm.step(now);
            current_state = g_fsm.state();
            effective_thr = g_fsm.effective_thr();
            effective_str = (current_state == FlightState::ARMED) ? g_fsm.last_steering() : 0.0f;
            batt_state = g_battery.state();
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
        FlapOutput flap = g_flap_engine.update(dt_s, is_armed, effective_thr, effective_str);

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

        // Update indicator LEDs
        g_leds.update(now, current_state, batt_state);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);

    // 1. Initialize servo output (pins configured as OUTPUT and driven LOW)
    g_servo_out.begin();

    // 2. Initialize battery ADC
    g_battery_adc.begin();

    // 3. Initialize LED indicators
    g_leds.begin();

    // 4. Initialize network subsystems (LittleFS, Wi-Fi AP, mDNS, WebServer, WebSocket)
    g_net.begin(g_fsm, g_battery, g_current_freq_hz, g_state_mutex);

    // 5. Log boot banner
    Serial.println();
    Serial.println("==================================================");
    Serial.println("         TROIDRONE ESP32-S3 FIRMWARE              ");
    Serial.print  ("  Version:         "); Serial.println(FW_VERSION);
    Serial.print  ("  Left Servo Pin:  GPIO"); Serial.println(g_servo_out.left_pin());
    Serial.print  ("  Right Servo Pin: GPIO"); Serial.println(g_servo_out.right_pin());
    Serial.print  ("  Battery ADC Pin: GPIO"); Serial.println(PIN_VBAT_ADC);
    Serial.print  ("  Status LED Pin:  GPIO"); Serial.println(PIN_LED_STATUS);
    Serial.print  ("  LowBatt LED Pin: GPIO"); Serial.println(PIN_LED_LOWBATT);
    Serial.print  ("  Wi-Fi AP SSID:   "); Serial.println(g_net.ssid());
    Serial.println("  AP IP:           192.168.4.1");
    Serial.println("  Initial State:   DISARMED");
    Serial.println("==================================================");

    // 6. Create dedicated 100 Hz FreeRTOS task (priority 2, higher than loopTask)
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

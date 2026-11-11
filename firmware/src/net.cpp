#include "net.h"
#include <cstring>
#include "link.h"
#include "protocol_codec.h"

#if defined(ARDUINO)
    #include <Arduino.h>
    #include <esp_mac.h>
    #include <WiFi.h>
    #include <ESPmDNS.h>
    #include <LittleFS.h>
    #include <AsyncTCP.h>
    #include <ESPAsyncWebServer.h>
#endif

#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

#ifndef WIFI_SSID_PREFIX
#define WIFI_SSID_PREFIX "troidrone"
#endif

#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD "troidrone-dev"
#endif

struct NetManager::Impl {
#if defined(ARDUINO)
    AsyncWebServer server{80};
    AsyncWebSocket ws{"/ws"};
#endif
    WsControlSource ws_control_source;
    uint32_t active_client_id = 0;
    char ssid[32] = {0};

    FlightFsm* fsm = nullptr;
    BatteryMonitor* battery = nullptr;
    const float* current_freq_hz = nullptr;
    std::mutex* state_mutex = nullptr;
    uint32_t last_telemetry_ms = 0;

#if defined(ARDUINO)
    void on_ws_event(AsyncWebSocket* server, AsyncWebSocketClient* client,
                     AwsEventType type, void* arg, uint8_t* data, size_t len);
#endif
    void send_telemetry(uint32_t now_ms);
};

#if defined(ARDUINO)
void NetManager::Impl::on_ws_event(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
                                   AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (client == nullptr) {
        return;
    }

    switch (type) {
        case WS_EVT_CONNECT: {
            uint32_t superseded_id = 0;
            if (state_mutex != nullptr && fsm != nullptr) {
                std::lock_guard<std::mutex> lock(*state_mutex);
                if (active_client_id != 0 && active_client_id != client->id()) {
                    superseded_id = active_client_id;
                    fsm->on_link_closed();
                }
                active_client_id = client->id();
                ws_control_source.reset();
            }

            if (superseded_id != 0) {
                ws.close(superseded_id, 4001, "superseded");
                Serial.print("[WS] Client #");
                Serial.print(superseded_id);
                Serial.print(" superseded by #");
                Serial.println(client->id());
            }

            char hello_buf[64];
            size_t hello_len = serialize_hello(hello_buf, sizeof(hello_buf), FW_VERSION);
            if (hello_len > 0) {
                client->text(hello_buf, hello_len);
            }
            Serial.print("[WS] Client #");
            Serial.print(client->id());
            Serial.println(" connected");
            break;
        }

        case WS_EVT_DISCONNECT: {
            uint32_t cid = client->id();
            bool was_active = false;
            if (state_mutex != nullptr && fsm != nullptr) {
                std::lock_guard<std::mutex> lock(*state_mutex);
                if (active_client_id != 0 && cid == active_client_id) {
                    was_active = true;
                    fsm->on_link_closed();
                    active_client_id = 0;
                    ws_control_source.reset();
                }
            }
            if (was_active) {
                Serial.print("[WS] Active client #");
                Serial.print(cid);
                Serial.println(" disconnected");
            }
            break;
        }

        case WS_EVT_DATA: {
            AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
            if (info != nullptr && info->opcode == WS_TEXT) {
                if (info->final && info->index == 0 && info->len == len) {
                    uint32_t cid = client->id();
                    const char* payload = reinterpret_cast<const char*>(data);
                    FrameType ft = identify_frame(payload, len);

                    if (ft == FrameType::CTL) {
                        CtlFrame ctl;
                        if (parse_ctl(payload, len, ctl)) {
                            if (state_mutex != nullptr && fsm != nullptr) {
                                std::lock_guard<std::mutex> lock(*state_mutex);
                                if (cid == active_client_id) {
                                    uint32_t now = millis();
                                    if (ws_control_source.accept_ctl(ctl.seq, ctl.thr, ctl.str, now)) {
                                        fsm->on_ctl(now, ctl.thr, ctl.str, ctl.seq);
                                    }
                                }
                            }
                        }
                    } else if (ft == FrameType::STOP) {
                        StopFrame stop_frame;
                        if (parse_stop(payload, len, stop_frame)) {
                            if (state_mutex != nullptr && fsm != nullptr) {
                                std::lock_guard<std::mutex> lock(*state_mutex);
                                if (cid == active_client_id) {
                                    uint32_t now = millis();
                                    if (ws_control_source.accept_stop(stop_frame.seq, now)) {
                                        fsm->on_stop(now, stop_frame.seq);
                                    }
                                }
                            }
                        }
                    } else if (ft == FrameType::ARM) {
                        ArmFrame arm;
                        if (parse_arm(payload, len, arm)) {
                            ArmResult result{false, ArmRefusal::NONE};
                            if (state_mutex != nullptr && fsm != nullptr) {
                                std::lock_guard<std::mutex> lock(*state_mutex);
                                if (cid == active_client_id) {
                                    uint32_t now = millis();
                                    bool batt_ok = (battery != nullptr) ? battery->is_ok() : true;
                                    result = fsm->on_arm_result(now, arm.v, batt_ok);
                                }
                            }
                            if (arm.v && !result.success) {
                                char err_buf[80];
                                size_t err_len = serialize_err(err_buf, sizeof(err_buf), "arm_refused", FlightFsm::to_refusal_str(result.reason));
                                if (err_len > 0) {
                                    client->text(err_buf, err_len);
                                }
                            }
                        }
                    }
                }
            }
            break;
        }

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}
#endif

void NetManager::Impl::send_telemetry(uint32_t now_ms) {
#if defined(ARDUINO)
    uint32_t client_id = 0;
    char st[16] = {0};
    float freq = 0.0f;
    float thr = 0.0f;
    float str = 0.0f;
    float vb = 0.0f;
    int vpct = 0;
    uint32_t age = 9999;

    if (state_mutex != nullptr && fsm != nullptr && current_freq_hz != nullptr) {
        std::lock_guard<std::mutex> lock(*state_mutex);
        client_id = active_client_id;
        if (client_id == 0) {
            return;
        }
        std::strncpy(st, fsm->state_str(), sizeof(st) - 1);
        freq = *current_freq_hz;
        thr = fsm->effective_thr();
        str = (fsm->state() == FlightState::ARMED) ? fsm->last_steering() : 0.0f;
        if (battery != nullptr) {
            vb = battery->voltage();
            vpct = battery->percentage();
        }
        age = fsm->age_ms(now_ms);
    } else {
        return;
    }

    char tel_buf[128];
    size_t len = serialize_tel(tel_buf, sizeof(tel_buf), st, freq, thr, str, vb, vpct, age);
    if (len > 0) {
        ws.text(client_id, tel_buf, len);
    }
#else
    (void)now_ms;
#endif
}

static NetManager::Impl s_impl;

NetManager::NetManager() : pimpl_(&s_impl) {}

NetManager::~NetManager() {}

uint32_t NetManager::active_client_id() const {
    return pimpl_->active_client_id;
}

const char* NetManager::ssid() const {
    return pimpl_->ssid;
}

bool NetManager::begin(FlightFsm& fsm, BatteryMonitor& battery, const float& current_freq_hz, std::mutex& state_mutex) {
    pimpl_->fsm = &fsm;
    pimpl_->battery = &battery;
    pimpl_->current_freq_hz = &current_freq_hz;
    pimpl_->state_mutex = &state_mutex;
    pimpl_->last_telemetry_ms = 0;

#if defined(ARDUINO)
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(pimpl_->ssid, sizeof(pimpl_->ssid), "%s-%02X%02X",
                  WIFI_SSID_PREFIX, mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    IPAddress ap_ip(192, 168, 4, 1);
    IPAddress ap_gateway(192, 168, 4, 1);
    IPAddress ap_subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ap_ip, ap_gateway, ap_subnet);

    bool ap_ok = WiFi.softAP(pimpl_->ssid, WIFI_AP_PASSWORD, 1, 0, 4);
    if (!ap_ok) {
        Serial.println("[NET] ERROR: Failed to start Wi-Fi SoftAP");
        return false;
    }

    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    if (MDNS.begin("troidrone")) {
        MDNS.addService("http", "tcp", 80);
    }

    if (!LittleFS.begin(false)) {
        Serial.println("[NET] WARNING: LittleFS mount failed (files will not be served)");
    } else {
        pimpl_->server.serveStatic("/", LittleFS, "/")
            .setDefaultFile("index.html")
            .setCacheControl("max-age=600");
    }

    pimpl_->ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                             AwsEventType t, void* a, uint8_t* d, size_t l) {
        pimpl_->on_ws_event(s, c, t, a, d, l);
    });
    pimpl_->server.addHandler(&pimpl_->ws);

    pimpl_->server.begin();
    return true;
#else
    std::strncpy(pimpl_->ssid, "troidrone-native", sizeof(pimpl_->ssid) - 1);
    return true;
#endif
}

void NetManager::update() {
#if defined(ARDUINO)
    uint32_t now = millis();
    pimpl_->ws.cleanupClients();
    if (now - pimpl_->last_telemetry_ms >= 200u) {
        pimpl_->last_telemetry_ms = now;
        pimpl_->send_telemetry(now);
    }
#endif
}

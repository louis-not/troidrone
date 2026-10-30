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
#define FW_VERSION "0.1.0"
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
                    } else if (ft == FrameType::ARM) {
                        ArmFrame arm;
                        if (parse_arm(payload, len, arm)) {
                            if (state_mutex != nullptr && fsm != nullptr) {
                                std::lock_guard<std::mutex> lock(*state_mutex);
                                if (cid == active_client_id) {
                                    uint32_t now = millis();
                                    fsm->on_arm(now, arm.v);
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
        age = fsm->age_ms(now_ms);
    } else {
        return;
    }

    char tel_buf[96];
    size_t len = serialize_tel(tel_buf, sizeof(tel_buf), st, freq, thr, age);
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

bool NetManager::begin(FlightFsm& fsm, const float& current_freq_hz, std::mutex& state_mutex) {
    pimpl_->fsm = &fsm;
    pimpl_->current_freq_hz = &current_freq_hz;
    pimpl_->state_mutex = &state_mutex;

#if defined(ARDUINO)
    // 1. Mount LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] ERROR: LittleFS mount failed");
    } else {
        Serial.println("[FS] LittleFS mounted");
    }

    // 2. Wi-Fi AP configuration
    WiFi.mode(WIFI_AP);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    static const char hex_digits[] = "0123456789ABCDEF";
    size_t plen = std::strlen(WIFI_SSID_PREFIX);
    if (plen + 6 < sizeof(pimpl_->ssid)) {
        std::memcpy(pimpl_->ssid, WIFI_SSID_PREFIX, plen);
        pimpl_->ssid[plen] = '-';
        pimpl_->ssid[plen + 1] = hex_digits[(mac[4] >> 4) & 0x0F];
        pimpl_->ssid[plen + 2] = hex_digits[mac[4] & 0x0F];
        pimpl_->ssid[plen + 3] = hex_digits[(mac[5] >> 4) & 0x0F];
        pimpl_->ssid[plen + 4] = hex_digits[mac[5] & 0x0F];
        pimpl_->ssid[plen + 5] = '\0';
    }

    IPAddress ap_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ap_ip, gateway, subnet);
    WiFi.softAP(pimpl_->ssid, WIFI_AP_PASSWORD);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // 3. mDNS responder: MDNS.begin("troidrone")
    if (MDNS.begin("troidrone")) {
        Serial.println("[MDNS] Responder started: http://troidrone.local");
    } else {
        Serial.println("[MDNS] ERROR: Failed to start responder");
    }

    // 4. WebSocket /ws setup
    pimpl_->ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c,
                          AwsEventType t, void* a, uint8_t* d, size_t l) {
        s_impl.on_ws_event(s, c, t, a, d, l);
    });
    pimpl_->server.addHandler(&pimpl_->ws);

    // 5. Static file handler (serve LittleFS with index.html default and gzip-first)
    pimpl_->server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setTryGzipFirst(true);

    // 6. 404 handler fallback
    pimpl_->server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404);
    });

    // 7. Start web server
    pimpl_->server.begin();
    Serial.print("[NET] AP SSID: ");
    Serial.print(pimpl_->ssid);
    Serial.println(", IP: 192.168.4.1");
#endif

    return true;
}

void NetManager::update() {
#if defined(ARDUINO)
    uint32_t now = millis();
    // 5 Hz telemetry loop (every 200 ms)
    if (now - pimpl_->last_telemetry_ms >= 200u) {
        pimpl_->last_telemetry_ms = now;
        pimpl_->send_telemetry(now);
    }
    pimpl_->ws.cleanupClients();
#endif
}

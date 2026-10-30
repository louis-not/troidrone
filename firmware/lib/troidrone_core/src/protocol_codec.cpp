#include "protocol_codec.h"
#include <algorithm>
#include <cmath>
#include <cstring>

FrameType identify_frame(const char* json_str, size_t len) {
    if (!json_str || len == 0) {
        return FrameType::UNKNOWN;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return FrameType::UNKNOWN;
    }
    if (!doc["t"].is<const char*>()) {
        return FrameType::UNKNOWN;
    }
    const char* t = doc["t"].as<const char*>();
    if (std::strcmp(t, "ctl") == 0) {
        return FrameType::CTL;
    }
    if (std::strcmp(t, "arm") == 0) {
        return FrameType::ARM;
    }
    if (std::strcmp(t, "hello") == 0) {
        return FrameType::HELLO;
    }
    if (std::strcmp(t, "tel") == 0) {
        return FrameType::TEL;
    }
    return FrameType::UNKNOWN;
}

bool parse_ctl(const char* json_str, size_t len, CtlFrame& out) {
    if (!json_str || len == 0) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    // "t" must be "ctl"
    if (!doc["t"].is<const char*>()) {
        return false;
    }
    if (std::strcmp(doc["t"].as<const char*>(), "ctl") != 0) {
        return false;
    }

    // "seq" must be unsigned 32-bit integer (reject bools or floats)
    if (!doc["seq"].is<uint32_t>() || doc["seq"].is<bool>()) {
        return false;
    }

    // "thr" must be finite float, clamped to [0.0f, 1.0f]
    if (!doc["thr"].is<float>() || doc["thr"].is<bool>()) {
        return false;
    }
    float raw_thr = doc["thr"].as<float>();
    if (!std::isfinite(raw_thr)) {
        return false;
    }

    // "str" optional float (default 0.0f), clamped to [-1.0f, 1.0f]
    float raw_str = 0.0f;
    if (!doc["str"].isNull()) {
        if (!doc["str"].is<float>() || doc["str"].is<bool>()) {
            return false;
        }
        raw_str = doc["str"].as<float>();
        if (!std::isfinite(raw_str)) {
            return false;
        }
    }

    out.seq = doc["seq"].as<uint32_t>();
    out.thr = std::clamp(raw_thr, 0.0f, 1.0f);
    out.str = std::clamp(raw_str, -1.0f, 1.0f);
    return true;
}

bool parse_arm(const char* json_str, size_t len, ArmFrame& out) {
    if (!json_str || len == 0) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    // "t" must be "arm"
    if (!doc["t"].is<const char*>()) {
        return false;
    }
    if (std::strcmp(doc["t"].as<const char*>(), "arm") != 0) {
        return false;
    }

    // "v" must be boolean
    if (!doc["v"].is<bool>()) {
        return false;
    }

    out.v = doc["v"].as<bool>();
    return true;
}

bool parse_hello(const char* json_str, size_t len, HelloFrame& out) {
    if (!json_str || len == 0) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    if (!doc["t"].is<const char*>() || std::strcmp(doc["t"].as<const char*>(), "hello") != 0) {
        return false;
    }
    if (!doc["proto"].is<int>() || doc["proto"].is<bool>()) {
        return false;
    }
    if (!doc["fw"].is<const char*>()) {
        return false;
    }

    out.proto = doc["proto"].as<int>();
    std::strncpy(out.fw, doc["fw"].as<const char*>(), sizeof(out.fw) - 1);
    out.fw[sizeof(out.fw) - 1] = '\0';
    return true;
}

bool parse_tel(const char* json_str, size_t len, TelFrame& out) {
    if (!json_str || len == 0) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    if (!doc["t"].is<const char*>() || std::strcmp(doc["t"].as<const char*>(), "tel") != 0) {
        return false;
    }
    if (!doc["st"].is<const char*>()) {
        return false;
    }
    if (!doc["f"].is<float>() || doc["f"].is<bool>()) {
        return false;
    }
    if (!doc["thr"].is<float>() || doc["thr"].is<bool>()) {
        return false;
    }
    if (!doc["age"].is<uint32_t>() || doc["age"].is<bool>()) {
        return false;
    }

    std::strncpy(out.st, doc["st"].as<const char*>(), sizeof(out.st) - 1);
    out.st[sizeof(out.st) - 1] = '\0';
    out.f = doc["f"].as<float>();
    out.thr = doc["thr"].as<float>();
    out.age = doc["age"].as<uint32_t>();
    return true;
}

size_t serialize_hello(char* buf, size_t buf_size, const char* fw_version) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    JsonDocument doc;
    doc["t"] = "hello";
    doc["proto"] = 1;
    doc["fw"] = (fw_version != nullptr) ? fw_version : "0.1.0";
    return serializeJson(doc, buf, buf_size);
}

size_t serialize_tel(char* buf, size_t buf_size, const char* st, float f, float thr, uint32_t age) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    JsonDocument doc;
    doc["t"] = "tel";
    doc["st"] = (st != nullptr) ? st : "";
    doc["f"] = f;
    doc["thr"] = thr;
    doc["age"] = age;
    return serializeJson(doc, buf, buf_size);
}

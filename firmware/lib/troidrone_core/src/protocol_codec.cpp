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
    if (std::strcmp(t, "stop") == 0) {
        return FrameType::STOP;
    }
    if (std::strcmp(t, "err") == 0) {
        return FrameType::ERR;
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

    if (!doc["t"].is<const char*>()) {
        return false;
    }
    if (std::strcmp(doc["t"].as<const char*>(), "ctl") != 0) {
        return false;
    }

    if (!doc["seq"].is<uint32_t>() || doc["seq"].is<bool>()) {
        return false;
    }

    if (!doc["thr"].is<float>() || doc["thr"].is<bool>()) {
        return false;
    }
    float raw_thr = doc["thr"].as<float>();
    if (!std::isfinite(raw_thr)) {
        return false;
    }

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

    if (!doc["t"].is<const char*>()) {
        return false;
    }
    if (std::strcmp(doc["t"].as<const char*>(), "arm") != 0) {
        return false;
    }

    if (!doc["v"].is<bool>()) {
        return false;
    }

    out.v = doc["v"].as<bool>();
    return true;
}

bool parse_stop(const char* json_str, size_t len, StopFrame& out) {
    if (!json_str || len == 0) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    if (!doc["t"].is<const char*>()) {
        return false;
    }
    if (std::strcmp(doc["t"].as<const char*>(), "stop") != 0) {
        return false;
    }

    if (!doc["seq"].is<uint32_t>() || doc["seq"].is<bool>()) {
        return false;
    }

    out.seq = doc["seq"].as<uint32_t>();
    return true;
}

bool parse_err(const char* json_str, size_t len, ErrFrame& out) {
    if (!json_str || len == 0) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    if (!doc["t"].is<const char*>()) {
        return false;
    }
    if (std::strcmp(doc["t"].as<const char*>(), "err") != 0) {
        return false;
    }

    if (!doc["code"].is<const char*>()) {
        return false;
    }
    std::strncpy(out.code, doc["code"].as<const char*>(), sizeof(out.code) - 1);
    out.code[sizeof(out.code) - 1] = '\0';

    if (doc["msg"].is<const char*>()) {
        std::strncpy(out.msg, doc["msg"].as<const char*>(), sizeof(out.msg) - 1);
        out.msg[sizeof(out.msg) - 1] = '\0';
    } else {
        out.msg[0] = '\0';
    }
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

    // Optional v2 fields
    out.str = 0.0f;
    if (doc["str"].is<float>() && !doc["str"].is<bool>()) {
        out.str = doc["str"].as<float>();
    }
    out.vb = 0.0f;
    if (doc["vb"].is<float>() && !doc["vb"].is<bool>()) {
        out.vb = doc["vb"].as<float>();
    }
    out.vpct = 0;
    if (doc["vpct"].is<int>() && !doc["vpct"].is<bool>()) {
        out.vpct = doc["vpct"].as<int>();
    }

    return true;
}

size_t serialize_hello(char* buf, size_t buf_size, const char* fw_version) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    JsonDocument doc;
    doc["t"] = "hello";
    doc["proto"] = 2;
    doc["fw"] = (fw_version != nullptr) ? fw_version : "0.2.0";
    return serializeJson(doc, buf, buf_size);
}

size_t serialize_tel(char* buf, size_t buf_size, const char* st, float f, float thr, float str, float vb, int vpct, uint32_t age) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    JsonDocument doc;
    doc["t"] = "tel";
    doc["st"] = (st != nullptr) ? st : "";
    doc["f"] = f;
    doc["thr"] = thr;
    doc["str"] = str;
    doc["vb"] = vb;
    doc["vpct"] = vpct;
    doc["age"] = age;
    return serializeJson(doc, buf, buf_size);
}

size_t serialize_tel(char* buf, size_t buf_size, const char* st, float f, float thr, uint32_t age) {
    return serialize_tel(buf, buf_size, st, f, thr, 0.0f, 0.0f, 0, age);
}

size_t serialize_err(char* buf, size_t buf_size, const char* code, const char* msg) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    JsonDocument doc;
    doc["t"] = "err";
    doc["code"] = (code != nullptr) ? code : "";
    doc["msg"] = (msg != nullptr) ? msg : "";
    return serializeJson(doc, buf, buf_size);
}

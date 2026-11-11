#pragma once
#ifndef TROIDRONE_PROTOCOL_CODEC_H_
#define TROIDRONE_PROTOCOL_CODEC_H_

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <ArduinoJson.h>

struct CtlFrame {
    uint32_t seq;
    float thr;
    float str;
};

struct ArmFrame {
    bool v;
};

struct StopFrame {
    uint32_t seq;
};

struct ErrFrame {
    char code[32];
    char msg[64];
};

struct HelloFrame {
    int proto;
    char fw[32];
};

struct TelFrame {
    char st[16];
    float f;
    float thr;
    float str;
    float vb;
    int vpct;
    uint32_t age;
};

enum class FrameType {
    UNKNOWN = 0,
    CTL,
    ARM,
    HELLO,
    TEL,
    STOP,
    ERR
};

FrameType identify_frame(const char* json_str, size_t len);
inline FrameType identify_frame(const char* json_str) {
    return json_str ? identify_frame(json_str, std::strlen(json_str)) : FrameType::UNKNOWN;
}

bool parse_ctl(const char* json_str, size_t len, CtlFrame& out);
inline bool parse_ctl(const char* json_str, CtlFrame& out) {
    return json_str ? parse_ctl(json_str, std::strlen(json_str), out) : false;
}

bool parse_arm(const char* json_str, size_t len, ArmFrame& out);
inline bool parse_arm(const char* json_str, ArmFrame& out) {
    return json_str ? parse_arm(json_str, std::strlen(json_str), out) : false;
}

bool parse_stop(const char* json_str, size_t len, StopFrame& out);
inline bool parse_stop(const char* json_str, StopFrame& out) {
    return json_str ? parse_stop(json_str, std::strlen(json_str), out) : false;
}

bool parse_err(const char* json_str, size_t len, ErrFrame& out);
inline bool parse_err(const char* json_str, ErrFrame& out) {
    return json_str ? parse_err(json_str, std::strlen(json_str), out) : false;
}

bool parse_hello(const char* json_str, size_t len, HelloFrame& out);
inline bool parse_hello(const char* json_str, HelloFrame& out) {
    return json_str ? parse_hello(json_str, std::strlen(json_str), out) : false;
}

bool parse_tel(const char* json_str, size_t len, TelFrame& out);
inline bool parse_tel(const char* json_str, TelFrame& out) {
    return json_str ? parse_tel(json_str, std::strlen(json_str), out) : false;
}

size_t serialize_hello(char* buf, size_t buf_size, const char* fw_version = "0.2.0");
size_t serialize_tel(char* buf, size_t buf_size, const char* st, float f, float thr, float str, float vb, int vpct, uint32_t age);
size_t serialize_tel(char* buf, size_t buf_size, const char* st, float f, float thr, uint32_t age);
size_t serialize_err(char* buf, size_t buf_size, const char* code, const char* msg);

namespace troidrone {
    using ::CtlFrame;
    using ::ArmFrame;
    using ::StopFrame;
    using ::ErrFrame;
    using ::HelloFrame;
    using ::TelFrame;
    using ::FrameType;
    using ::identify_frame;
    using ::parse_ctl;
    using ::parse_arm;
    using ::parse_stop;
    using ::parse_err;
    using ::parse_hello;
    using ::parse_tel;
    using ::serialize_hello;
    using ::serialize_tel;
    using ::serialize_err;
}

#endif  // TROIDRONE_PROTOCOL_CODEC_H_

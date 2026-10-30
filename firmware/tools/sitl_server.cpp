#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <cctype>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include "flap_engine.h"
#include "flight_fsm.h"
#include "servo_ease.h"
#include "servo_out.h"
#include "link.h"
#include "protocol_codec.h"

// ============================================================================
// 1. Self-contained SHA-1 & Base64 Implementations (RFC 6455 WebSocket)
// ============================================================================

namespace sha1 {

inline uint32_t rol(uint32_t value, size_t bits) {
    return (value << bits) | (value >> (32 - bits));
}

inline void transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t block[80];

    for (size_t i = 0; i < 16; ++i) {
        block[i] = (static_cast<uint32_t>(buffer[i * 4 + 0]) << 24)
                 | (static_cast<uint32_t>(buffer[i * 4 + 1]) << 16)
                 | (static_cast<uint32_t>(buffer[i * 4 + 2]) << 8)
                 | (static_cast<uint32_t>(buffer[i * 4 + 3]));
    }
    for (size_t i = 16; i < 80; ++i) {
        block[i] = rol(block[i - 3] ^ block[i - 8] ^ block[i - 14] ^ block[i - 16], 1);
    }

    for (size_t i = 0; i < 20; ++i) {
        uint32_t f = (b & c) | ((~b) & d);
        uint32_t k = 0x5A827999;
        uint32_t temp = rol(a, 5) + f + e + k + block[i];
        e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }
    for (size_t i = 20; i < 40; ++i) {
        uint32_t f = b ^ c ^ d;
        uint32_t k = 0x6ED9EBA1;
        uint32_t temp = rol(a, 5) + f + e + k + block[i];
        e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }
    for (size_t i = 40; i < 60; ++i) {
        uint32_t f = (b & c) | (b & d) | (c & d);
        uint32_t k = 0x8F1BBCDC;
        uint32_t temp = rol(a, 5) + f + e + k + block[i];
        e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }
    for (size_t i = 60; i < 80; ++i) {
        uint32_t f = b ^ c ^ d;
        uint32_t k = 0xCA62C1D6;
        uint32_t temp = rol(a, 5) + f + e + k + block[i];
        e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

inline void hash(const uint8_t* data, size_t len, uint8_t digest[20]) {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t total_bits = static_cast<uint64_t>(len) * 8;

    uint8_t buffer[64];
    size_t offset = 0;

    while (len >= 64) {
        transform(state, data + offset);
        offset += 64;
        len -= 64;
    }

    std::memcpy(buffer, data + offset, len);
    buffer[len] = 0x80;
    len++;

    if (len > 56) {
        std::memset(buffer + len, 0, 64 - len);
        transform(state, buffer);
        std::memset(buffer, 0, 56);
    } else {
        std::memset(buffer + len, 0, 56 - len);
    }

    for (int i = 7; i >= 0; --i) {
        buffer[56 + (7 - i)] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }
    transform(state, buffer);

    for (size_t i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
    }
}

} // namespace sha1

namespace base64 {

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(b64_table[(triple >> 18) & 0x3F]);
        out.push_back(b64_table[(triple >> 12) & 0x3F]);
        out.push_back(b64_table[(triple >> 6) & 0x3F]);
        out.push_back(b64_table[triple & 0x3F]);
    }

    size_t mod = len % 3;
    if (mod == 1) {
        out[out.size() - 1] = '=';
        out[out.size() - 2] = '=';
    } else if (mod == 2) {
        out[out.size() - 1] = '=';
    }

    return out;
}

} // namespace base64

// ============================================================================
// 2. Global Firmware Subsystems & Simulation State
// ============================================================================

static FlightFsm g_fsm;
static FlapEngine g_flap_engine;
static ServoEase g_servo_ease;
static ServoOut g_servo_out;
static WsControlSource g_ws_control_source;

static std::mutex g_state_mutex;
static std::mutex g_send_mutex;

static float g_current_freq_hz = 0.0f;
static bool g_drop_ctl = false;
static int g_active_client_fd = -1;

static std::atomic<bool> g_running{true};
static std::chrono::steady_clock::time_point g_start_time;

// ============================================================================
// 3. Socket I/O & WebSocket Framing Helpers
// ============================================================================

static bool send_all(int fd, const void* data, size_t len) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR)) {
                continue;
            }
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

static bool send_ws_text(int fd, const std::string& text) {
    std::vector<uint8_t> frame;
    size_t len = text.size();
    frame.push_back(0x81); // FIN=1, Opcode=1 (Text)
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), text.begin(), text.end());
    std::lock_guard<std::mutex> lock(g_send_mutex);
    return send_all(fd, frame.data(), frame.size());
}

static bool send_ws_close(int fd, uint16_t status_code, const std::string& reason) {
    std::vector<uint8_t> frame;
    size_t payload_len = 2 + reason.size();
    frame.push_back(0x88); // FIN=1, Opcode=8 (Close)
    if (payload_len < 126) {
        frame.push_back(static_cast<uint8_t>(payload_len));
    } else {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(payload_len & 0xFF));
    }
    frame.push_back(static_cast<uint8_t>((status_code >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(status_code & 0xFF));
    frame.insert(frame.end(), reason.begin(), reason.end());
    std::lock_guard<std::mutex> lock(g_send_mutex);
    return send_all(fd, frame.data(), frame.size());
}

static bool send_ws_pong(int fd, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(0x8A); // FIN=1, Opcode=0xA (Pong)
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    if (len > 0 && payload != nullptr) {
        frame.insert(frame.end(), payload, payload + len);
    }
    std::lock_guard<std::mutex> lock(g_send_mutex);
    return send_all(fd, frame.data(), frame.size());
}

struct WsFrame {
    bool fin = true;
    uint8_t opcode = 0;
    bool masked = false;
    std::vector<uint8_t> payload;
};

// Returns: 1 if frame parsed, 0 if incomplete (need more data), -1 on error
static int parse_ws_frame(std::vector<uint8_t>& buf, WsFrame& out) {
    if (buf.size() < 2) return 0;

    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];

    out.fin = (b0 & 0x80) != 0;
    out.opcode = b0 & 0x0F;
    out.masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;

    size_t header_len = 2;
    if (len == 126) {
        if (buf.size() < 4) return 0;
        len = (static_cast<uint64_t>(buf[2]) << 8) | buf[3];
        header_len = 4;
    } else if (len == 127) {
        if (buf.size() < 10) return 0;
        len = 0;
        for (int i = 0; i < 8; ++i) {
            len = (len << 8) | buf[2 + i];
        }
        header_len = 10;
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (out.masked) {
        if (buf.size() < header_len + 4) return 0;
        std::memcpy(mask, buf.data() + header_len, 4);
        header_len += 4;
    }

    if (buf.size() < header_len + len) return 0;

    out.payload.resize(len);
    const uint8_t* raw = buf.data() + header_len;
    if (out.masked) {
        for (size_t i = 0; i < len; ++i) {
            out.payload[i] = raw[i] ^ mask[i % 4];
        }
    } else {
        std::memcpy(out.payload.data(), raw, len);
    }

    buf.erase(buf.begin(), buf.begin() + header_len + len);
    return 1;
}

// ============================================================================
// 4. HTTP Helpers & MIME Types
// ============================================================================

static std::string get_mime_type(const std::string& path) {
    std::string p = path;
    if (p.size() >= 3 && p.rfind(".gz") == p.size() - 3) {
        p = p.substr(0, p.size() - 3);
    }
    size_t dot = p.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = p.substr(dot);
        for (char& c : ext) c = static_cast<char>(std::tolower(c));
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".js" || ext == ".mjs") return "application/javascript";
        if (ext == ".css") return "text/css";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".json") return "application/json";
        if (ext == ".png") return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".ico") return "image/x-icon";
        if (ext == ".woff") return "font/woff";
        if (ext == ".woff2") return "font/woff2";
        if (ext == ".txt") return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

static void send_404(int fd) {
    const char resp[] =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n"
        "Not Found";
    std::lock_guard<std::mutex> lock(g_send_mutex);
    send_all(fd, resp, sizeof(resp) - 1);
}

static void send_403(int fd) {
    const char resp[] =
        "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n"
        "Forbidden";
    std::lock_guard<std::mutex> lock(g_send_mutex);
    send_all(fd, resp, sizeof(resp) - 1);
}

static void serve_file_contents(int fd, const std::filesystem::path& path, bool is_gzip, const std::string& orig_name) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        send_404(fd);
        return;
    }
    std::vector<uint8_t> contents((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    std::string mime = get_mime_type(orig_name);
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: " + mime + "\r\n" +
        (is_gzip ? "Content-Encoding: gzip\r\n" : "") +
        "Content-Length: " + std::to_string(contents.size()) + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n";

    std::lock_guard<std::mutex> lock(g_send_mutex);
    send_all(fd, header.data(), header.size());
    if (!contents.empty()) {
        send_all(fd, contents.data(), contents.size());
    }
}

static void serve_static_file(int fd, const std::string& path_str, const std::string& data_dir) {
    std::filesystem::path base(data_dir);
    if (path_str == "/" || path_str.empty()) {
        std::filesystem::path p_gz = base / "index.html.gz";
        std::filesystem::path p_plain = base / "index.html";
        if (std::filesystem::exists(p_gz)) {
            serve_file_contents(fd, p_gz, true, "index.html");
            return;
        } else if (std::filesystem::exists(p_plain)) {
            serve_file_contents(fd, p_plain, false, "index.html");
            return;
        }
        send_404(fd);
        return;
    }

    std::string rel = path_str;
    while (!rel.empty() && rel.front() == '/') {
        rel.erase(rel.begin());
    }
    if (rel.find("..") != std::string::npos) {
        send_403(fd);
        return;
    }

    std::filesystem::path full_p = base / rel;
    std::filesystem::path full_gz = std::filesystem::path(full_p.string() + ".gz");

    if (rel.size() >= 3 && rel.rfind(".gz") == rel.size() - 3) {
        if (std::filesystem::exists(full_p)) {
            serve_file_contents(fd, full_p, true, rel);
            return;
        }
    } else {
        if (std::filesystem::exists(full_gz)) {
            serve_file_contents(fd, full_gz, true, rel);
            return;
        } else if (std::filesystem::exists(full_p)) {
            serve_file_contents(fd, full_p, false, rel);
            return;
        }
    }

    send_404(fd);
}

// ============================================================================
// 5. Client Connection State & Network Processing
// ============================================================================

enum class ClientState {
    HTTP,
    WEBSOCKET
};

struct ClientConnection {
    int fd = -1;
    ClientState state = ClientState::HTTP;
    std::vector<uint8_t> in_buf;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
};

static void disconnect_client(int fd, std::map<int, ClientConnection>& clients) {
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_active_client_fd == fd) {
            g_fsm.on_link_closed();
            g_ws_control_source.reset();
            g_active_client_fd = -1;
        }
    }
    close(fd);
    clients.erase(fd);
}

static void process_http(ClientConnection& client, const std::string& data_dir,
                         std::map<int, ClientConnection>& clients, bool& close_client) {
    // Find "\r\n\r\n"
    if (client.in_buf.size() < 4) return;
    size_t header_end = std::string::npos;
    for (size_t i = 0; i + 3 < client.in_buf.size(); ++i) {
        if (client.in_buf[i] == '\r' && client.in_buf[i+1] == '\n' &&
            client.in_buf[i+2] == '\r' && client.in_buf[i+3] == '\n') {
            header_end = i;
            break;
        }
    }

    if (header_end == std::string::npos) {
        if (client.in_buf.size() > 65536) {
            close_client = true;
        }
        return;
    }

    std::string header_str(client.in_buf.begin(), client.in_buf.begin() + header_end);
    std::istringstream stream(header_str);
    std::string req_line;
    if (!std::getline(stream, req_line)) {
        close_client = true;
        return;
    }
    if (!req_line.empty() && req_line.back() == '\r') req_line.pop_back();

    std::istringstream line_stream(req_line);
    HttpRequest req;
    line_stream >> req.method >> req.target;

    size_t qpos = req.target.find('?');
    if (qpos != std::string::npos) {
        req.path = req.target.substr(0, qpos);
        req.query = req.target.substr(qpos + 1);
    } else {
        req.path = req.target;
        req.query = "";
    }

    std::string header_line;
    while (std::getline(stream, header_line)) {
        if (!header_line.empty() && header_line.back() == '\r') header_line.pop_back();
        if (header_line.empty()) continue;
        size_t colon = header_line.find(':');
        if (colon != std::string::npos) {
            std::string key = header_line.substr(0, colon);
            std::string val = header_line.substr(colon + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
            for (char& c : key) c = static_cast<char>(std::tolower(c));
            req.headers[key] = val;
        }
    }

    size_t content_length = 0;
    auto it_cl = req.headers.find("content-length");
    if (it_cl != req.headers.end()) {
        try {
            content_length = std::stoul(it_cl->second);
        } catch (...) {
            content_length = 0;
        }
    }

    if (client.in_buf.size() < header_end + 4 + content_length) {
        return; // wait for full request body
    }

    if (content_length > 0) {
        req.body.assign(reinterpret_cast<const char*>(client.in_buf.data() + header_end + 4), content_length);
    }

    client.in_buf.erase(client.in_buf.begin(), client.in_buf.begin() + header_end + 4 + content_length);

    // 1. WebSocket Upgrade
    std::string upgrade_hdr = req.headers["upgrade"];
    for (char& c : upgrade_hdr) c = static_cast<char>(std::tolower(c));
    if (req.method == "GET" && req.path == "/ws" && upgrade_hdr.find("websocket") != std::string::npos) {
        std::string ws_key = req.headers["sec-websocket-key"];
        std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string accept_input = ws_key + magic;
        uint8_t digest[20];
        sha1::hash(reinterpret_cast<const uint8_t*>(accept_input.data()), accept_input.size(), digest);
        std::string accept_key = base64::encode(digest, 20);

        std::string handshake_resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

        {
            std::lock_guard<std::mutex> lock(g_send_mutex);
            send_all(client.fd, handshake_resp.data(), handshake_resp.size());
        }

        client.state = ClientState::WEBSOCKET;

        // Single active controller enforcement
        int superseded_fd = -1;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            if (g_active_client_fd != -1 && g_active_client_fd != client.fd) {
                superseded_fd = g_active_client_fd;
                g_fsm.on_link_closed();
                g_ws_control_source.reset();
            }
            g_active_client_fd = client.fd;
        }

        if (superseded_fd != -1) {
            send_ws_close(superseded_fd, 4001, "superseded");
            disconnect_client(superseded_fd, clients);
        }

        char hello_buf[64];
        size_t hlen = serialize_hello(hello_buf, sizeof(hello_buf), "0.1.0");
        if (hlen > 0) {
            send_ws_text(client.fd, std::string(hello_buf, hlen));
        }
        return;
    }

    // 2. GET /__sitl/state
    if (req.method == "GET" && req.path == "/__sitl/state") {
        std::string body;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            JsonDocument doc;
            doc["st"] = g_fsm.state_str();
            doc["pwm_active"] = g_servo_out.is_pwm_active();
            float left_deg = (g_servo_out.last_left_deg() >= 0.0f) ? g_servo_out.last_left_deg() : g_servo_ease.current_left();
            float right_deg = (g_servo_out.last_right_deg() >= 0.0f) ? g_servo_out.last_right_deg() : g_servo_ease.current_right();
            doc["left_deg"] = left_deg;
            doc["right_deg"] = right_deg;
            doc["left_us"] = ServoOut::angle_to_pulse_us(left_deg);
            doc["right_us"] = ServoOut::angle_to_pulse_us(right_deg);
            doc["freq_hz"] = g_current_freq_hz;
            doc["drop_ctl"] = g_drop_ctl;
            serializeJson(doc, body);
        }
        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n" + body;

        {
            std::lock_guard<std::mutex> lock(g_send_mutex);
            send_all(client.fd, resp.data(), resp.size());
        }
        close_client = true;
        return;
    }

    // 3. POST /__sitl/drop_ctl
    if (req.method == "POST" && req.path == "/__sitl/drop_ctl") {
        bool new_val = false;
        bool parsed = false;

        std::string query = req.query;
        for (char& c : query) c = static_cast<char>(std::tolower(c));
        if (!query.empty()) {
            if (query.find("v=1") != std::string::npos || query.find("v=true") != std::string::npos ||
                query.find("drop_ctl=1") != std::string::npos || query.find("drop_ctl=true") != std::string::npos) {
                new_val = true;
                parsed = true;
            } else if (query.find("v=0") != std::string::npos || query.find("v=false") != std::string::npos ||
                       query.find("drop_ctl=0") != std::string::npos || query.find("drop_ctl=false") != std::string::npos) {
                new_val = false;
                parsed = true;
            }
        }

        if (!parsed && !req.body.empty()) {
            JsonDocument bdoc;
            DeserializationError err = deserializeJson(bdoc, req.body);
            if (!err && bdoc.is<JsonObject>()) {
                if (bdoc["drop_ctl"].is<bool>()) {
                    new_val = bdoc["drop_ctl"].as<bool>();
                    parsed = true;
                } else if (bdoc["v"].is<bool>()) {
                    new_val = bdoc["v"].as<bool>();
                    parsed = true;
                } else if (bdoc["drop_ctl"].is<int>()) {
                    new_val = (bdoc["drop_ctl"].as<int>() != 0);
                    parsed = true;
                } else if (bdoc["v"].is<int>()) {
                    new_val = (bdoc["v"].as<int>() != 0);
                    parsed = true;
                }
            }
            if (!parsed) {
                std::string lbody = req.body;
                for (char& c : lbody) c = static_cast<char>(std::tolower(c));
                if (lbody.find("true") != std::string::npos || lbody.find("1") != std::string::npos) {
                    new_val = true;
                    parsed = true;
                } else if (lbody.find("false") != std::string::npos || lbody.find("0") != std::string::npos) {
                    new_val = false;
                    parsed = true;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_drop_ctl = new_val;
        }

        std::string resp_body = std::string("{\"drop_ctl\":") + (new_val ? "true" : "false") + "}";
        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(resp_body.size()) + "\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n" + resp_body;

        {
            std::lock_guard<std::mutex> lock(g_send_mutex);
            send_all(client.fd, resp.data(), resp.size());
        }
        close_client = true;
        return;
    }

    // 4. CORS preflight OPTIONS
    if (req.method == "OPTIONS") {
        std::string resp =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: *\r\n"
            "Connection: close\r\n\r\n";

        {
            std::lock_guard<std::mutex> lock(g_send_mutex);
            send_all(client.fd, resp.data(), resp.size());
        }
        close_client = true;
        return;
    }

    // 5. GET Static Files
    if (req.method == "GET") {
        serve_static_file(client.fd, req.path, data_dir);
        close_client = true;
        return;
    }

    // Unsupported method
    send_404(client.fd);
    close_client = true;
}

static void process_websocket(ClientConnection& client, bool& close_client) {
    while (true) {
        WsFrame frame;
        int res = parse_ws_frame(client.in_buf, frame);
        if (res == 0) {
            break; // need more data
        }
        if (res < 0) {
            close_client = true;
            break;
        }

        if (frame.opcode == 0x1) { // Text frame
            uint32_t now_ms = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_start_time
                ).count()
            );

            std::lock_guard<std::mutex> lock(g_state_mutex);
            if (client.fd == g_active_client_fd) {
                const char* payload_str = reinterpret_cast<const char*>(frame.payload.data());
                size_t payload_len = frame.payload.size();

                FrameType ft = identify_frame(payload_str, payload_len);

                if (g_drop_ctl && ft == FrameType::CTL) {
                    // Drop frame: do not feed to WsControlSource or FlightFsm
                } else if (ft == FrameType::CTL) {
                    CtlFrame ctl;
                    if (parse_ctl(payload_str, payload_len, ctl)) {
                        if (g_ws_control_source.accept_ctl(ctl.seq, ctl.thr, ctl.str, now_ms)) {
                            g_fsm.on_ctl(now_ms, ctl.thr, ctl.str, ctl.seq);
                        }
                    }
                } else if (ft == FrameType::ARM) {
                    ArmFrame arm;
                    if (parse_arm(payload_str, payload_len, arm)) {
                        g_fsm.on_arm(now_ms, arm.v);
                    }
                }
            }
        } else if (frame.opcode == 0x8) { // Close frame
            send_ws_close(client.fd, 1000, "normal closure");
            close_client = true;
            break;
        } else if (frame.opcode == 0x9) { // Ping
            send_ws_pong(client.fd, frame.payload.data(), frame.payload.size());
        } else if (frame.opcode == 0xA) { // Pong
            // ignore pong
        }
    }
}

static void process_client_data(ClientConnection& client, const std::string& data_dir,
                                std::map<int, ClientConnection>& clients, bool& close_client) {
    if (client.state == ClientState::HTTP) {
        process_http(client, data_dir, clients, close_client);
    }
    if (client.state == ClientState::WEBSOCKET && !close_client) {
        process_websocket(client, close_client);
    }
}

// ============================================================================
// 6. FreeRTOS / Loop Simulation Thread (100 Hz = 10 ms)
// ============================================================================

static void simulation_task() {
    auto next_wake = std::chrono::steady_clock::now();
    uint32_t tel_counter = 0;

    while (g_running) {
        next_wake += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next_wake);
        if (!g_running) break;

        auto now = std::chrono::steady_clock::now();
        if (next_wake < now) {
            next_wake = now;
        }

        uint32_t now_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count()
        );

        std::string tel_frame;
        int target_fd = -1;

        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_fsm.step(now_ms);

            FlightState st = g_fsm.state();
            bool is_armed = (st == FlightState::ARMED);
            float effective_thr = g_fsm.effective_thr();

            FlapOutput flap = g_flap_engine.update(0.010f, is_armed, effective_thr);
            g_current_freq_hz = flap.freq_hz;

            float out_left = 90.0f;
            float out_right = 90.0f;
            bool pwm_active = false;
            g_servo_ease.update(0.010f, st, flap.left_deg, flap.right_deg,
                                out_left, out_right, pwm_active);
            g_servo_out.update(out_left, out_right, pwm_active);

            tel_counter++;
            if (tel_counter >= 20) { // 20 * 10 ms = 200 ms = 5 Hz
                tel_counter = 0;
                if (g_active_client_fd != -1) {
                    char buf[128];
                    size_t n = serialize_tel(buf, sizeof(buf),
                                             g_fsm.state_str(),
                                             g_current_freq_hz,
                                             g_fsm.effective_thr(),
                                             g_fsm.age_ms(now_ms));
                    if (n > 0) {
                        tel_frame.assign(buf, n);
                        target_fd = g_active_client_fd;
                    }
                }
            }
        }

        if (target_fd != -1 && !tel_frame.empty()) {
            if (!send_ws_text(target_fd, tel_frame)) {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                if (g_active_client_fd == target_fd) {
                    g_fsm.on_link_closed();
                    g_ws_control_source.reset();
                    g_active_client_fd = -1;
                }
            }
        }
    }
}

// ============================================================================
// 7. Signal Handling & Main Server Loop
// ============================================================================

static void sig_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char** argv) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    int port = 0;
    std::string data_dir;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" || arg == "-p") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        } else if (arg.rfind("--port=", 0) == 0) {
            port = std::stoi(arg.substr(7));
        } else if (arg == "--data" || arg == "-d") {
            if (i + 1 < argc) {
                data_dir = argv[++i];
            }
        } else if (arg.rfind("--data=", 0) == 0) {
            data_dir = arg.substr(7);
        }
    }

    if (data_dir.empty()) {
        if (std::filesystem::exists("firmware/data")) {
            data_dir = "firmware/data";
        } else if (std::filesystem::exists("data")) {
            data_dir = "data";
        } else {
            data_dir = "firmware/data";
        }
    }

    // Initialize firmware servo layer
    g_servo_out.begin();
    g_start_time = std::chrono::steady_clock::now();

    // Create server TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int sflags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, sflags | O_NONBLOCK);

    sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 64) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    // Determine actual bound port
    sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    if (getsockname(server_fd, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len) < 0) {
        perror("getsockname");
        close(server_fd);
        return 1;
    }
    uint16_t actual_port = ntohs(bound_addr.sin_port);

    // Required announcement format
    std::cout << "SITL_PORT=" << actual_port << std::endl;
    std::fflush(stdout);

    // Launch dedicated 100 Hz simulation thread
    std::thread sim_thread(simulation_task);

    // Event loop for networking
    std::map<int, ClientConnection> clients;

    while (g_running) {
        std::vector<struct pollfd> pfds;
        pfds.reserve(clients.size() + 1);
        pfds.push_back({server_fd, POLLIN, 0});
        for (const auto& pair : clients) {
            pfds.push_back({pair.first, POLLIN, 0});
        }

        int ret = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 50);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!g_running) break;

        // Check listen socket for new clients
        if (pfds[0].revents & POLLIN) {
            while (true) {
                sockaddr_in caddr;
                socklen_t clen = sizeof(caddr);
                int cfd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&caddr), &clen);
                if (cfd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
                    break;
                }
                int cflags = fcntl(cfd, F_GETFL, 0);
                fcntl(cfd, F_SETFL, cflags | O_NONBLOCK);
                int nodelay = 1;
                setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                clients[cfd] = ClientConnection{cfd, ClientState::HTTP, {}};
            }
        }

        // Check connected clients
        std::vector<int> to_disconnect;
        for (size_t i = 1; i < pfds.size(); ++i) {
            int cfd = pfds[i].fd;
            auto it = clients.find(cfd);
            if (it == clients.end()) continue;

            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                to_disconnect.push_back(cfd);
                continue;
            }

            if (pfds[i].revents & POLLIN) {
                uint8_t temp[4096];
                bool disconnected = false;
                while (true) {
                    ssize_t n = recv(cfd, temp, sizeof(temp), 0);
                    if (n > 0) {
                        it->second.in_buf.insert(it->second.in_buf.end(), temp, temp + n);
                    } else if (n == 0) {
                        disconnected = true;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        disconnected = true;
                        break;
                    }
                }
                if (disconnected) {
                    to_disconnect.push_back(cfd);
                    continue;
                }

                bool close_client = false;
                process_client_data(it->second, data_dir, clients, close_client);
                if (close_client) {
                    to_disconnect.push_back(cfd);
                }
            }
        }

        for (int fd : to_disconnect) {
            if (clients.find(fd) != clients.end()) {
                disconnect_client(fd, clients);
            }
        }
    }

    g_running = false;
    if (sim_thread.joinable()) {
        sim_thread.join();
    }

    for (const auto& pair : clients) {
        close(pair.first);
    }
    clients.clear();
    close(server_fd);

    return 0;
}

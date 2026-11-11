#pragma once
#ifndef TROIDRONE_LINK_H_
#define TROIDRONE_LINK_H_

#include <cstdint>

inline bool is_seq_newer(uint32_t seq, uint32_t last_seq) {
    return static_cast<int32_t>(seq - last_seq) > 0;
}

class ControlSource {
public:
    virtual bool accept_ctl(uint32_t seq, float thr, float str, uint32_t now_ms) = 0;
    virtual bool accept_stop(uint32_t seq, uint32_t now_ms) = 0;
    virtual void reset() = 0;
    virtual ~ControlSource() = default;
};

class WsControlSource : public ControlSource {
public:
    WsControlSource() = default;
    ~WsControlSource() override = default;

    bool accept_ctl(uint32_t seq, float thr, float str, uint32_t now_ms) override;
    bool accept_stop(uint32_t seq, uint32_t now_ms) override;
    void reset() override;

    bool has_seq() const { return has_seq_; }
    uint32_t last_seq() const { return last_seq_; }

private:
    bool has_seq_ = false;
    uint32_t last_seq_ = 0;
};

namespace troidrone {
    using ::is_seq_newer;
    using ::ControlSource;
    using ::WsControlSource;
}

#endif  // TROIDRONE_LINK_H_

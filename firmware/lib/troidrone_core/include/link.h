#pragma once
#ifndef TROIDRONE_LINK_H_
#define TROIDRONE_LINK_H_

#include <cstdint>

class ControlSource {
public:
    virtual bool accept_ctl(uint32_t seq, float thr, float str, uint32_t now_ms) = 0;
    virtual void reset() = 0;
    virtual ~ControlSource() = default;
};

class WsControlSource : public ControlSource {
public:
    WsControlSource() = default;
    ~WsControlSource() override = default;

    bool accept_ctl(uint32_t seq, float thr, float str, uint32_t now_ms) override;
    void reset() override;

    bool has_seq() const { return has_seq_; }
    uint32_t last_seq() const { return last_seq_; }

private:
    bool has_seq_ = false;
    uint32_t last_seq_ = 0;
};

namespace troidrone {
    using ::ControlSource;
    using ::WsControlSource;
}

#endif  // TROIDRONE_LINK_H_

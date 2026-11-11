#include "link.h"

bool WsControlSource::accept_ctl(uint32_t seq, float thr, float str, uint32_t now_ms) {
    (void)thr;
    (void)str;
    (void)now_ms;

    if (!has_seq_) {
        has_seq_ = true;
        last_seq_ = seq;
        return true;
    }

    if (is_seq_newer(seq, last_seq_)) {
        last_seq_ = seq;
        return true;
    }

    return false;
}

bool WsControlSource::accept_stop(uint32_t seq, uint32_t now_ms) {
    (void)now_ms;

    if (!has_seq_) {
        has_seq_ = true;
        last_seq_ = seq;
        return true;
    }

    if (is_seq_newer(seq, last_seq_)) {
        last_seq_ = seq;
        return true;
    }

    return false;
}

void WsControlSource::reset() {
    has_seq_ = false;
    last_seq_ = 0;
}

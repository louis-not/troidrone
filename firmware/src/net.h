#pragma once
#ifndef TROIDRONE_NET_H_
#define TROIDRONE_NET_H_

#include <cstdint>
#include <mutex>
#include "flight_fsm.h"

class NetManager {
public:
    NetManager();
    ~NetManager();

    bool begin(FlightFsm& fsm, const float& current_freq_hz, std::mutex& state_mutex);
    void update();

    uint32_t active_client_id() const;
    const char* ssid() const;

public:
    struct Impl;
    Impl* pimpl_;
};

#endif  // TROIDRONE_NET_H_

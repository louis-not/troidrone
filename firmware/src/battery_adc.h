#pragma once
#ifndef TROIDRONE_BATTERY_ADC_H_
#define TROIDRONE_BATTERY_ADC_H_

#include <cstdint>
#include "pins.h"

class BatteryAdc {
public:
    void begin();
    uint32_t read_adc_mv();
};

#endif  // TROIDRONE_BATTERY_ADC_H_

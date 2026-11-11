#include "battery_adc.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

void BatteryAdc::begin() {
#if defined(ARDUINO)
    pinMode(PIN_VBAT_ADC, INPUT);
    analogSetAttenuation(ADC_11db);
#endif
}

uint32_t BatteryAdc::read_adc_mv() {
#if defined(ARDUINO)
    return analogReadMilliVolts(PIN_VBAT_ADC);
#else
    return 2000; // Simulated 2000 mV in native
#endif
}

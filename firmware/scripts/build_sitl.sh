#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

mkdir -p "${FIRMWARE_DIR}/.pio"

g++ -std=c++17 -Wall -Wextra -pthread -O2 \
  -I"${FIRMWARE_DIR}/include" \
  -I"${FIRMWARE_DIR}/lib/troidrone_core/include" \
  -I"${FIRMWARE_DIR}/src" \
  -I"${FIRMWARE_DIR}/.pio/libdeps/native/ArduinoJson/src" \
  "${FIRMWARE_DIR}/lib/troidrone_core/src/flap_engine.cpp" \
  "${FIRMWARE_DIR}/lib/troidrone_core/src/flight_fsm.cpp" \
  "${FIRMWARE_DIR}/lib/troidrone_core/src/protocol_codec.cpp" \
  "${FIRMWARE_DIR}/lib/troidrone_core/src/link.cpp" \
  "${FIRMWARE_DIR}/lib/troidrone_core/src/servo_ease.cpp" \
  "${FIRMWARE_DIR}/src/servo_out.cpp" \
  "${FIRMWARE_DIR}/tools/sitl_server.cpp" \
  -o "${FIRMWARE_DIR}/.pio/sitl_server"

echo "Build complete: ${FIRMWARE_DIR}/.pio/sitl_server"

// -- config.h --
#pragma once

// -- Load secrets from a separate, non-versioned file --
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found. Please create it from secrets.h.example and add your credentials."
#endif

// -- Display Settings --
// Define the I2C pins for the OLED display.
// Common pins for ESP32-C3 boards might be different.
// Please check your board's pinout diagram.
// For many generic ESP32-C3 boards, default I2C pins are:
const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;

// -- Update Interval --
// Time in milliseconds between data fetches from Nightscout. 30000ms = 30 seconds.
const unsigned long UPDATE_INTERVAL_MS = 20000;

// -- Time Settings --
// Time zone for Kyiv (EET/EEST, UTC+2 / UTC+3 DST) is used to convert the UTC timestamp from Nightscout
const char *TZ_INFO = "EET-2EEST,M3.5.0/3,M10.5.0/4";

#pragma once
#include <Arduino.h>

// #define DEBUG

static uint64_t swap64(uint64_t x) {
    uint64_t byte1 = x & 0xff00000000000000;
    uint64_t byte2 = x & 0x00ff000000000000;
    uint64_t byte3 = x & 0x0000ff0000000000;
    uint64_t byte4 = x & 0x000000ff00000000;
    uint64_t byte5 = x & 0x00000000ff000000;
    uint64_t byte6 = x & 0x0000000000ff0000;
    uint64_t byte7 = x & 0x000000000000ff00;
    uint64_t byte8 = x & 0x00000000000000ff;

    return (uint64_t)(byte1 >> 56 | byte2 >> 40 | byte3 >> 24 | byte4 >> 8 |
                      byte5 << 8 | byte6 << 24 | byte7 << 40 | byte8 << 56);
}

static inline uint64_t cast8u8_u64(const uint8_t* bytes) {
    return *(uint64_t*)bytes;
}

static String hex_8u8(const uint8_t* bytes) {
    char buffer[25];
    sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\0", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
    return String(buffer);
}


static String hex_8u8_plain(const uint8_t* bytes) {
    char buffer[25];
    sprintf(buffer, "%02X%02X%02X%02X%02X%02X%02X%02X\0", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
    return String(buffer);
}

template<typename... T> inline void log(T... args) {
    #ifdef DEBUG
    if (Serial) {
        Serial.print(args...);
    }
    #endif
}

template<typename... T> inline void logln(T... args) {
    #ifdef DEBUG
    if (Serial) {
        Serial.println(args...);
    }
    #endif
}

template<typename... T> inline void logf(T... args) {
    #ifdef DEBUG
    if (Serial) {
        Serial.printf(args...);
    }
    #endif
}
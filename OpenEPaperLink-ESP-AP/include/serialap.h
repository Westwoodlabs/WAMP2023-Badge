#pragma once
#include <Arduino.h>

extern struct espSetChannelPower curChannel;

#define AP_STATE_OFFLINE 0
#define AP_STATE_ONLINE 1
#define AP_STATE_FLASHING 2
#define AP_STATE_WAIT_RESET 3
#define AP_STATE_REQUIRED_POWER_CYCLE 4
#define AP_STATE_FAILED 5
#define AP_STATE_COMING_ONLINE 6

struct APInfoS {
    bool isOnline = false;
    uint8_t state = AP_STATE_OFFLINE;
    uint8_t type;
    uint16_t version = 0;
    uint8_t channel;
    uint8_t mac[8];
    uint8_t power;
    uint8_t pending;
    uint8_t nop;
};

extern struct APInfoS apInfo;

void APTask(void* parameter);

bool sendCancelPending(struct pendingData* pending);
bool sendDataAvail(struct pendingData* pending);
bool sendPing();
uint16_t sendBlock(const void* data, const uint16_t len);

void APEnterEarlyReset();

bool sendChannelPower(struct espSetChannelPower* scp);
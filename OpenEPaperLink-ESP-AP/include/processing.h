#include <Arduino.h>
#include "commstructs.h"

void processBlockRequest(struct espBlockRequest* br);
void processXferComplete(struct espXferComplete* xfc, bool local);
void processXferTimeout(struct espXferComplete* xfc, bool local);
void processDataReq(struct espAvailDataReq* adr, bool local);

void process_mqtt_sda(uint8_t* payload, uint32_t length);

struct TagSDA {
    uint8_t mac[8];
    AvailDataInfo sda;
} __packed;
#include "Arduino.h"
#pragma once
#define MQTT_BUFFER_SIZE 32 * 1024
void init_network();

void network_task(void* parameter);

void mqtt_send_message(const char* topic, const char* payload, uint32_t length);
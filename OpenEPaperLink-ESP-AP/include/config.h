#pragma once
#define CONFIG_LOCATION "/config.json"

typedef struct Config {
    String mqtt_broker;
    String mqtt_id;
    String mqtt_username;
    String mqtt_password;
} Config;

extern Config config;

void init_config();

void save_config();

#pragma once

#define EXAMPLE_ESP_WIFI_SSID      "WELTIN"
#define EXAMPLE_ESP_WIFI_PASS      "12345678"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5 // Set the Maximum retry to avoid station reconnecting to the AP unlimited when the AP is really inexistent.

void wifi_init_sta(void);

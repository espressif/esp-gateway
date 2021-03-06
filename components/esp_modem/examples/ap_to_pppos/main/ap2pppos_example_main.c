// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_modem.h"
#include "lwip/lwip_napt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

static const char *TAG = "ap-2-pppos";
static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int DISCONNECT_BIT = BIT1;

static void on_modem_event(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT) {
        ESP_LOGD(TAG, "IP event! %d", event_id);
        if (event_id == IP_EVENT_PPP_GOT_IP) {
            esp_netif_dns_info_t dns_info;

            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            esp_netif_t *netif = event->esp_netif;

            ESP_LOGI(TAG, "Modem Connect to PPP Server");
            ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
            ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
            ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
            esp_netif_get_dns_info(netif, 0, &dns_info);
            ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
            esp_netif_get_dns_info(netif, 1, &dns_info);
            ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
            ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
            xEventGroupSetBits(event_group, CONNECT_BIT);

            ESP_LOGI(TAG, "GOT ip event!!!");
        } else if (event_id == IP_EVENT_PPP_LOST_IP) {
            ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
            xEventGroupSetBits(event_group, DISCONNECT_BIT);
        } else if (event_id == IP_EVENT_GOT_IP6) {
            ESP_LOGI(TAG, "GOT IPv6 event!");
            ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
            ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
        }
    } else if (event_base == ESP_MODEM_EVENT) {
        ESP_LOGD(TAG, "Modem event! %d", event_id);

    }
}

static esp_err_t set_dhcps_dns(esp_netif_t *netif, uint32_t addr)
{
    esp_netif_dns_info_t dns;
    dns.ip.u_addr.ip4.addr = addr;
    dns.ip.type = IPADDR_TYPE_V4;
    dhcps_offer_t dhcps_dns_value = OFFER_DNS;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_dns_value, sizeof(dhcps_dns_value)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns));
    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

esp_modem_dce_t *sim7600_board_create(esp_modem_dce_config_t *config);

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    event_group = xEventGroupCreate();

    // init the DTE
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.event_task_stack_size = 4096;
    dte_config.rx_buffer_size = 16384;
    dte_config.tx_buffer_size = 2048;
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("internet23");
    dce_config.populate_command_list = true;
    esp_netif_config_t ppp_netif_config = ESP_NETIF_DEFAULT_PPP();


    // Initialize esp-modem units, DTE, DCE, ppp-netif
    esp_modem_dte_t *dte = esp_modem_dte_new(&dte_config);
    esp_modem_dce_t *dce = sim7600_board_create(&dce_config);
    esp_netif_t *ppp_netif = esp_netif_new(&ppp_netif_config);

    assert(ppp_netif);

    ESP_ERROR_CHECK(esp_modem_set_event_handler(dte, on_modem_event, ESP_EVENT_ANY_ID, dte));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_modem_event, dte));

    ESP_ERROR_CHECK(esp_modem_default_attach(dte, dce, ppp_netif));

    ESP_ERROR_CHECK(esp_modem_default_start(dte)); // use retry
    ESP_ERROR_CHECK(esp_modem_start_ppp(dte));
    /* Wait for the first connection */
    EventBits_t bits;
    do {
        bits = xEventGroupWaitBits(event_group, (CONNECT_BIT | DISCONNECT_BIT), pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits&DISCONNECT_BIT) {
        }
    } while ((bits&CONNECT_BIT) == 0);

    /* Init the AP with NAT enabled */
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    esp_netif_dns_info_t dns;
    ESP_ERROR_CHECK(esp_netif_get_dns_info(ppp_netif, ESP_NETIF_DNS_MAIN, &dns));
    set_dhcps_dns(ap_netif, dns.ip.u_addr.ip4.addr);

    wifi_init_softap();
    ip_napt_enable(_g_esp_netif_soft_ap_ip.ip.addr, 1);

    /* Provide recovery if disconnection of some kind registered */
    while (DISCONNECT_BIT&xEventGroupWaitBits(event_group, DISCONNECT_BIT, pdTRUE, pdFALSE, portMAX_DELAY)) {
        // restart the modem PPP mode
        ESP_ERROR_CHECK(esp_modem_stop_ppp(dte));
        ESP_ERROR_CHECK(esp_modem_start_ppp(dte));
    }

}

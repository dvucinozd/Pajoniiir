/* SPDX-License-Identifier: MIT
 * RMII/IP101 wiring from kayrozen/Pajoniiir 428b97dd, main/eth_bringup.c:
 * P4 default MDC31/MDIO52/external clock50, PHY address1, reset51.
 * Copyright (c) 2024 The Pajoniiir Contributors. Hardware acceptance NOT RUN.
 */
#include "board_ethernet.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
static esp_eth_handle_t s_driver;
static esp_netif_t *s_netif;
esp_netif_t *board_ethernet_netif(void) { return s_netif; }
esp_err_t board_ethernet_start(void)
{
    if (s_driver) return ESP_OK;
    esp_err_t rc = esp_netif_init();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) return rc;
    rc = esp_event_loop_create_default();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) return rc;
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = 51;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
    esp_eth_handle_t driver = NULL;
    esp_eth_netif_glue_handle_t glue = NULL;
    esp_netif_t *netif = NULL;
    if (!mac || !phy) { rc = ESP_ERR_NO_MEM; goto fail; }
    esp_eth_config_t eth = ETH_DEFAULT_CONFIG(mac, phy);
    rc = esp_eth_driver_install(&eth, &driver);
    if (rc != ESP_OK) goto fail;
    uint8_t address[6];
    rc = esp_read_mac(address, ESP_MAC_ETH);
    if (rc != ESP_OK) goto fail;
    rc = esp_eth_ioctl(driver, ETH_CMD_S_MAC_ADDR, address);
    if (rc != ESP_OK) goto fail;
    esp_netif_config_t config = ESP_NETIF_DEFAULT_ETH();
    netif = esp_netif_new(&config);
    if (!netif) { rc = ESP_ERR_NO_MEM; goto fail; }
    glue = esp_eth_new_netif_glue(driver);
    if (!glue) { rc = ESP_ERR_NO_MEM; goto fail; }
    rc = esp_netif_attach(netif, glue);
    if (rc != ESP_OK) goto fail;
    rc = esp_eth_start(driver);
    if (rc != ESP_OK) goto fail;
    s_netif = netif;
    s_driver = driver;
    ESP_LOGI("board_eth", "RMII DHCP started; Link protocol remains disabled");
    return ESP_OK;
fail:
    if (glue) esp_eth_del_netif_glue(glue);
    if (netif) esp_netif_destroy(netif);
    if (driver) esp_eth_driver_uninstall(driver);
    if (phy) phy->del(phy);
    if (mac) mac->del(mac);
    return rc;
}

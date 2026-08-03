/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <AP_HAL_ESP32/WiFiNowDriver.h>
#include <AP_Math/AP_Math.h>
#include <AP_HAL_ESP32/Scheduler.h>

#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"

#include "freertos/idf_additions.h"

using namespace ESP32;

extern const AP_HAL::HAL& hal;

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. 
#if CONFIG_ESPNOW_WIFI_MODE_STATION
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif
*/


#define CONFIG_ESPNOW_CHANNEL  6
//#define NOW_DEBUG

// ----- MAKROS -----
#define MACSTR     "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

uint8_t _myMac[6];
int _rxLen = 0;
uint8_t _rxBuffer[230];
#if defined (NOW_DEBUG)
  uint8_t _info = 0;
#endif

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
  uint8_t * mac_addr = recv_info->src_addr;
  if (mac_addr == NULL || data == NULL || len <= 0) {
    hal.console->printf("Receive cb arg error\n");
    return;
  }
  
#if defined (NOW_DEBUG)
  uint8_t _desMac[6];
  if(!_info){
    mac_addr = recv_info->des_addr;
    hal.console->printf("OK espnow _myMac(%02X:%02X:%02X:%02X:%02X:%02X)\n",_myMac[0], _myMac[1], _myMac[2], _myMac[3], _myMac[4], _myMac[5]);
    
    memcpy(&_desMac, mac_addr, 6);
    hal.console->printf("OK espnow _desMac(%02X:%02X:%02X:%02X:%02X:%02X)\n",_desMac[0], _desMac[1], _desMac[2], _desMac[3], _desMac[4], _desMac[5]);
    _info = 10;
  } else {
    _info --;
  }
#endif

  if(memcmp(&_myMac, recv_info->des_addr, 6) == 0) {
    _rxLen = len;
    memcpy(&_rxBuffer, data, len);
  }
}

WiFiNowDriver::WiFiNowDriver()
{
      _initialized = false;
}

void WiFiNowDriver::_begin(uint32_t b, uint16_t rxS, uint16_t txS)
{
  if(!_initialized){
    initialize_wifi();

    // keep main tasks that need speed on CPU 0
    // pin potentially slow stuff to CPU 1, as we have disabled the WDT on that core.
    #define FASTCPU 0
    #define SLOWCPU 1

    if (xTaskCreatePinnedToCore(_wifi_thread, "APM_WIFI1", Scheduler::WIFI_SS1, this, Scheduler::WIFI_PRIO1, &_wifi_task_handle, SLOWCPU) != pdPASS) {
      hal.console->printf("FAILED to create task _wifi_thread on SLOWCPU\n");
    } else {
	    hal.console->printf("OK created task _wifi_thread for ESPNOW on SLOWCPU\n"); //ESPNOW_PORT
    }
		
    _readbuf.set_size(RX_BUF_SIZE);
    _writebuf.set_size(TX_BUF_SIZE);
    _initialized = true;
   }
}

void WiFiNowDriver::_end()
{
    //TODO
}

void WiFiNowDriver::_flush()
{
}

bool WiFiNowDriver::is_initialized()
{
    return _initialized;
}

bool WiFiNowDriver::tx_pending()
{
    return (_writebuf.available() > 0);
}

uint32_t WiFiNowDriver::_available()
{
    if(!_initialized){
      return 0;
    }
    return _readbuf.available();
}

uint32_t WiFiNowDriver::txspace()
{
    int result =  _writebuf.space();
    result -= TX_BUF_SIZE / 4;
    return MAX(result, 0);
}

ssize_t IRAM_ATTR WiFiNowDriver::_read(uint8_t *buffer, uint16_t count)
{
    if (!_initialized) {
        return -1;
    }

    const uint32_t ret = _readbuf.read(buffer, count);
    if (ret == 0) {
        return 0;
    }

    return ret;
}


void IRAM_ATTR WiFiNowDriver::read_data()
{
  if (_rxLen > 0) {
      _readbuf.write(_rxBuffer, _rxLen);
      _rxLen = 0;
  }
}

void IRAM_ATTR WiFiNowDriver::write_data()
{
    int count = 0;
    _write_mutex.take_blocking();
    do {
        count = _writebuf.peekbytes(_buffer, sizeof(_buffer));
        if (count > 0) {
          esp_err_t error = esp_now_send(broadcast_mac, _buffer, count);
          if(error != ESP_OK) {
            hal.console->printf("ESPNOW send error %u\n", error);
            _write_mutex.give();
          }
            _writebuf.advance(count);
        }
    } while (count > 0);
    _write_mutex.give();

}

void WiFiNowDriver::initialize_wifi()
{

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
//    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_start());

    ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );

    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) );
    
    ESP_ERROR_CHECK( esp_wifi_set_max_tx_power(80) ); // 80 / 4 = 20dBm

    ESP_ERROR_CHECK( esp_wifi_get_mac(WIFI_IF_AP, _myMac) );

    // ESPNOW init
    ESP_ERROR_CHECK( esp_now_init() );
    
    ESP_ERROR_CHECK( esp_wifi_config_espnow_rate(WIFI_IF_AP, WIFI_PHY_RATE_LORA_250K) );
    
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );

    /* Add broadcast peer information to peer list. */
      esp_now_peer_info_t peer;
      memset(&peer, 0, sizeof(esp_now_peer_info_t));
      peer.channel = CONFIG_ESPNOW_CHANNEL;
      peer.ifidx = WIFI_IF_AP;  //ESPNOW_WIFI_IF;
      peer.encrypt = false;
      memcpy(peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
      ESP_ERROR_CHECK( esp_now_add_peer(&peer) );
    
    wifi_mode_t _mode;    
    ESP_ERROR_CHECK( esp_wifi_get_mode(&_mode) );
    
    int8_t _txPwr;
    ESP_ERROR_CHECK( esp_wifi_get_max_tx_power(&_txPwr) ); // x * 0,25 dBm

    uint8_t _protocol;
    ESP_ERROR_CHECK( esp_wifi_get_protocol(WIFI_IF_AP, &_protocol) );
    
    uint8_t _channel = 6;
    wifi_second_chan_t _second;
    ESP_ERROR_CHECK( esp_wifi_get_channel(&_channel, &_second) );

    hal.console->printf("OK ESPNOW init finished.\n Mode: %d\n MAC Address: " MACSTR "\n Tx Power: %i\n Protocol: %u\n Channel: %d\n", _mode, MAC2STR(_myMac), _txPwr, _protocol, _channel);

}

size_t IRAM_ATTR WiFiNowDriver::_write(const uint8_t *buffer, size_t size)
{
    if (!_initialized) {
        return 0;
    }

    _write_mutex.take_blocking();

    
    size_t ret = _writebuf.write(buffer, size);
    _write_mutex.give();
    return ret;
}

void WiFiNowDriver::_wifi_thread(void *arg)
{
    WiFiNowDriver *self = (WiFiNowDriver *) arg;
    while (true) {
        self->read_data();        
        self->write_data();
        hal.scheduler->delay_microseconds(1000);
    }
}

bool WiFiNowDriver::_discard_input()
{
    return false;
}

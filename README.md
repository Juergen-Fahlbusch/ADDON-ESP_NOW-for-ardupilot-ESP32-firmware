# ADDON ESP_NOW for ARDUPILOT ESP32 firmware
This non offical addon is a test bench to use the Espressif ESP-NOW protocol 
directly on an ESP32 based ARDUPILOT ESP32 FC

Usage of this addon is without any warranty and on your own risk
Check all ardupilot functionality first especially if you use this
on an flying object.

Hardware used: ESP32 WROOM-32 (eg ESP32 Dev Kit C); 
Software IDE:  ARDUPILOT based ESP-IDF
Additional 2nd ESP32 with compatible ESP-NOW-SERIAL-BRIDGE from
https://github.com/Juergen-Fahlbusch/ESP-NOW-Serial-Bridge is needed

## Installation
- copy files 'WiFiNowDriver.h' and 'WifiNowDriver.cpp' to your 
  ardupilot/libaries/AP_HAL_32 directory
- backup your files 'HAL_ESP32_Class.cpp' and 'HAL_ESP32_Namespace.h' at your
  ardupilot/libaries/AP_HAL_32 directory
- compare files 'HAL_ESP32_Class.cpp' and 'HAL_ESP32_Namespace.h' with your 
  existing files at ardupilot/libaries/AP_HAL_32 directory and add all differences
- change in your 'hwdef.dat' at ardupilot/libaries/AP_HAL_32/hwdef/yourESP32module 
  the value of 'HAL_ESP32_WIFI' to '3'
- compile your new customized firmware on your own responsibility 

## Usage
Connect your 2nd ESP32 to your PC with Mission Planner via USB. Select the shown COM
for this module with Baudrate 115200 and click connect. If everythink works well now 
Mission Planner reads the data from your ESP32 FC

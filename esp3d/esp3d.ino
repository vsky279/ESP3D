/*
    This file is part of ESP3D Firmware for 3D printer.

    ESP3D Firmware for 3D printer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ESP3D Firmware for 3D printer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this Firmware.  If not, see <http://www.gnu.org/licenses/>.

    This firmware is using the standard arduino IDE with module to support ESP8266:
    https://github.com/esp8266/Arduino from Bootmanager

    Latest version of the code and documentation can be found here :
    https://github.com/luc-github/ESP3D

    Main author: luc lebosse

*/
//be sure correct IDE and settings are used for ESP8266
#ifndef ARDUINO_ARCH_ESP8266
#error Oops!  Make sure you have 'ESP8266' compatible board selected from the 'Tools -> Boards' menu.
#endif
#include <EEPROM.h>
#include "config.h"
#ifdef TIMESTAMP_FEATURE
#include <time.h>
#endif
#include "wifi.h"
#include "bridge.h"
#include "webinterface.h"
#include "command.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#ifdef MDNS_FEATURE
#include <ESP8266mDNS.h>
#endif
#ifdef CAPTIVE_PORTAL_FEATURE
#include <DNSServer.h>
const byte DNS_PORT = 53;
DNSServer dnsServer;
#endif
#ifdef SSDP_FEATURE
#include <ESP8266SSDP.h>
#endif
#ifdef NETBIOS_FEATURE
#include <ESP8266NetBIOS.h>
#endif
#ifdef SDCARD_FEATURE
SdFat SD;
#endif
#ifdef SDCARD_CONFIG_FEATURE
#include <IniFile.h>
#endif

#if defined(TIMESTAMP_FEATURE) && defined(SDCARD_FEATURE)
void dateTime(uint16_t* date, uint16_t* dtime) {
time_t n;
time(&n);
tm * tmstruct = gmtime((const time_t *)&n);
 *date = FAT_DATE((tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday);
 *dtime = FAT_TIME(tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
}
#endif

void setup()
{
    bool breset_config=false;
    bool directsd_check = false;
    web_interface = NULL;
#ifdef TCP_IP_DATA_FEATURE
    data_server = NULL;
#endif
    // init:
#ifdef DEBUG_ESP3D
    Serial.begin(DEFAULT_BAUD_RATE);
    delay(2000);
    LOG("\r\nDebug Serial set\r\n")
#endif
    //WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(8000);
    CONFIG::InitDirectSD();
    CONFIG::InitPins();
#ifdef RECOVERY_FEATURE
    delay(8000);
    //check if reset config is requested
    if (digitalRead(RESET_CONFIG_PIN)==0) {
        breset_config=true;    //if requested =>reset settings
    }
#endif
    //check if EEPROM has value
    if (  !CONFIG::InitBaudrate() || !CONFIG::InitExternalPorts()) {
        breset_config=true;    //cannot access to config settings=> reset settings
        LOG("Error no EEPROM access\r\n")
    }

    //reset is requested
    if(breset_config) {
        //update EEPROM with default settings
        Serial.begin(DEFAULT_BAUD_RATE);
        Serial.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
        delay(2000);
        Serial.println(F("M117 ESP EEPROM reset"));
#ifdef DEBUG_ESP3D
        CONFIG::print_config(DEBUG_PIPE, true);
        delay(1000);
#endif
        CONFIG::reset_config();
        delay(1000);
        //put some default value to a void some exception at first start
        WiFi.mode(WIFI_AP);
        WiFi.setPhyMode(WIFI_PHY_MODE_11G);
        CONFIG::esp_restart();
    }
#if defined(DEBUG_ESP3D) && defined(DEBUG_OUTPUT_SERIAL)
    LOG("\r\n");
    delay(500);
    Serial.flush();
#endif
    //get target FW
    CONFIG::InitFirmwareTarget();
    //Update is done if any so should be Ok
    SPIFFS.begin();
       
    //setup wifi according settings
    if (!wifi_config.Setup()) {
        Serial.println(F("M117 Safe mode 1"));
        //try again in AP mode
       if (!wifi_config.Setup(true)) {
            Serial.println(F("M117 Safe mode 2"));
            wifi_config.Safe_Setup();
        }
    }
    delay(1000);
    //start web interface
    web_interface = new WEBINTERFACE_CLASS(wifi_config.iweb_port);
    //here the list of headers to be recorded
    const char * headerkeys[] = {"Cookie"} ;
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    //ask server to track these headers
    web_interface->WebServer.collectHeaders(headerkeys, headerkeyssize );
#ifdef CAPTIVE_PORTAL_FEATURE
    if (WiFi.getMode()!=WIFI_STA ) {
        // if DNSServer is started with "*" for domain name, it will reply with
        // provided IP to all DNS request
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    }
#endif
    web_interface->WebServer.begin();
#ifdef TCP_IP_DATA_FEATURE
    //start TCP/IP interface
    data_server = new WiFiServer (wifi_config.idata_port);
    data_server->begin();
    data_server->setNoDelay(true);
#endif

#ifdef MDNS_FEATURE
    // Check for any mDNS queries and send responses
    //useless in AP mode and service consuming 
    if (WiFi.getMode()!=WIFI_AP )wifi_config.mdns.addService("http", "tcp", wifi_config.iweb_port);
#endif
#if defined(SSDP_FEATURE) || defined(NETBIOS_FEATURE)
    String shost;
    if (!CONFIG::read_string(EP_HOSTNAME, shost, MAX_HOSTNAME_LENGTH)) {
        shost=wifi_config.get_default_hostname();
    }
#endif
#ifdef SSDP_FEATURE
    String stmp;
    SSDP.setSchemaURL("description.xml");
    SSDP.setHTTPPort( wifi_config.iweb_port);
    SSDP.setName(shost.c_str());
    stmp=String(ESP.getChipId());
    SSDP.setSerialNumber(stmp.c_str());
    SSDP.setURL("/");
    SSDP.setModelName("ESP8266 01");
    SSDP.setModelNumber("01");
    SSDP.setModelURL("http://espressif.com/en/products/esp8266/");
    SSDP.setManufacturer("Espressif Systems");
    SSDP.setManufacturerURL("http://espressif.com");
    SSDP.setDeviceType("upnp:rootdevice");
    if (WiFi.getMode()!=WIFI_AP )SSDP.begin();
#endif
#ifdef NETBIOS_FEATURE
    //useless in AP mode and service consuming 
    if (WiFi.getMode()!=WIFI_AP )NBNS.begin(shost.c_str());
#endif

#if defined(TIMESTAMP_FEATURE)
    CONFIG::init_time_client();
#ifdef SDCARD_FEATURE
if(CONFIG::is_direct_sd) {
    //set callback to get time on files on SD
    SdFile::dateTimeCallback(dateTime);
    }
#endif
#endif
    LOG("Setup Done\r\n");
}



//main loop
void loop()
{
#ifdef CAPTIVE_PORTAL_FEATURE
    if (WiFi.getMode()!=WIFI_STA ) {
        dnsServer.processNextRequest();
    }
#endif
//web requests
    web_interface->WebServer.handleClient();
#ifdef TCP_IP_DATA_FEATURE
    BRIDGE::processFromTCP2Serial();
#endif
    BRIDGE::processFromSerial2TCP();
    if (web_interface->restartmodule) {
        CONFIG::esp_restart();
    }
}

/*
  config.cpp- ESP3D configuration class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "config.h"
#include <EEPROM.h>
#include <WiFiUdp.h>
#include "wifi.h"
extern "C" {
#include "user_interface.h"
}
#include "bridge.h"

uint8_t CONFIG::FirmwareTarget = UNKNOWN_FW;

bool CONFIG::SetFirmwareTarget(uint8_t fw){
    if ( fw >= 0 && fw <= MAX_FW_ID) {
        FirmwareTarget = fw;
        return true;
    } else return false;
}
uint8_t CONFIG::GetFirmwareTarget() {
    return FirmwareTarget;
}
const char* CONFIG::GetFirmwareTargetName() {
    static String response;
    if ( CONFIG::FirmwareTarget == REPETIER4DV)response = F("Repetier for Davinci");
    else if ( CONFIG::FirmwareTarget == REPETIER)response = F("Repetier");
    else if ( CONFIG::FirmwareTarget == MARLIN) response = F("Marlin");
    else if ( CONFIG::FirmwareTarget == MARLINKIMBRA) response = F("MarlinKimbra");
    else if ( CONFIG::FirmwareTarget == SMOOTHIEWARE) response = F("Smoothieware");
    else response = F("???");
    return response.c_str();
}

const char* CONFIG::GetFirmwareTargetShortName() {
    static String response;
    if ( CONFIG::FirmwareTarget == REPETIER4DV)response = F("repetier4davinci");
    else if ( CONFIG::FirmwareTarget == REPETIER)response = F("repetier");
    else if ( CONFIG::FirmwareTarget == MARLIN) response = F("marlin");
    else if ( CONFIG::FirmwareTarget == MARLINKIMBRA) response = F("marlinkimbra");
    else if ( CONFIG::FirmwareTarget == SMOOTHIEWARE) response = F("smoothieware");
    else response = F("???");
    return response.c_str();
}

void CONFIG::InitFirmwareTarget(){
    uint8_t b = UNKNOWN_FW;
    if (!CONFIG::read_byte(EP_TARGET_FW, &b )) {
        b = UNKNOWN_FW;
        }
    if (!SetFirmwareTarget(b))SetFirmwareTarget(UNKNOWN_FW) ;
}

void CONFIG::InitDirectSD(){
 CONFIG::is_direct_sd = false;
}

bool CONFIG::InitBaudrate(){
    long baud_rate=0;
     if ( !CONFIG::read_buffer(EP_BAUD_RATE,  (byte *)&baud_rate, INTEGER_LENGTH)) return false;
      if ( ! (baud_rate==9600 || baud_rate==19200 ||baud_rate==38400 ||baud_rate==57600 ||baud_rate==115200 ||baud_rate==230400 ||baud_rate==250000) ) return false;
     //setup serial
     Serial.begin(baud_rate);
     Serial.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
     wifi_config.baud_rate=baud_rate;
     delay(1000);
     return true;
}

bool CONFIG::InitExternalPorts(){
    if (!CONFIG::read_buffer(EP_WEB_PORT,  (byte *)&(wifi_config.iweb_port), INTEGER_LENGTH) || !CONFIG::read_buffer(EP_DATA_PORT,  (byte *)&(wifi_config.idata_port), INTEGER_LENGTH)) return false;
    if (wifi_config.iweb_port < DEFAULT_MIN_WEB_PORT ||wifi_config.iweb_port > DEFAULT_MAX_WEB_PORT || wifi_config.idata_port < DEFAULT_MIN_DATA_PORT || wifi_config.idata_port > DEFAULT_MAX_DATA_PORT) return false;
    return true;
}

void CONFIG::esp_restart()
{
    LOG("Restarting\r\n")
    Serial.flush();
    delay(500);
    Serial.swap();
    delay(100);
    ESP.restart();
    while (1) {
        delay(1);
    };
}

void  CONFIG::InitPins(){
#ifdef RECOVERY_FEATURE
    pinMode(RESET_CONFIG_PIN, INPUT);
#endif
}

bool CONFIG::is_direct_sd = false;

bool CONFIG::isHostnameValid(const char * hostname)
{
    //limited size
    char c;
    if (strlen(hostname)>MAX_HOSTNAME_LENGTH || strlen(hostname) < MIN_HOSTNAME_LENGTH) {
        return false;
    }
    //only letter and digit
    for (int i=0; i < strlen(hostname); i++) {
        c = hostname[i];
        if (!(isdigit(c) || isalpha(c) || c=='_')) {
            return false;
        }
        if (c==' ') {
            return false;
        }
    }
    return true;
}

bool CONFIG::isSSIDValid(const char * ssid)
{
    //limited size
    char c;
    if (strlen(ssid)>MAX_SSID_LENGTH || strlen(ssid)<MIN_SSID_LENGTH) {
        return false;
    }
    //only letter and digit
    for (int i=0; i < strlen(ssid); i++) {
        if (!isPrintable(ssid[i]))return false;
        //if (!(isdigit(c) || isalpha(c))) return false;
        //if (c==' ') {
        //     return false;
        //}
    }
    return true;
}

bool CONFIG::isPasswordValid(const char * password)
{
    //limited size
    if ((strlen(password)>MAX_PASSWORD_LENGTH)||  (strlen(password)<MIN_PASSWORD_LENGTH)) {
        return false;
    }
    //no space allowed
    for (int i=0; i < strlen(password); i++)
        if (password[i] == ' ') {
            return false;
        }
    return true;
}

bool CONFIG::isLocalPasswordValid(const char * password)
{
    char c;
    //limited size
    if ((strlen(password)>MAX_LOCAL_PASSWORD_LENGTH)||  (strlen(password)<MIN_LOCAL_PASSWORD_LENGTH)) {
        return false;
    }
    //no space allowed
    for (int i=0; i < strlen(password); i++) {
        c= password[i];
        if (c==' ') {
            return false;
        }
    }
    return true;
}

bool CONFIG::isIPValid(const char * IP)
{
    //limited size
    int internalcount=0;
    int dotcount = 0;
    bool previouswasdot=false;
    char c;

    if (strlen(IP)>15 || strlen(IP)==0) {
        return false;
    }
    //cannot start with .
    if (IP[0]=='.') {
        return false;
    }
    //only letter and digit
    for (int i=0; i < strlen(IP); i++) {
        c = IP[i];
        if (isdigit(c)) {
            //only 3 digit at once
            internalcount++;
            previouswasdot=false;
            if (internalcount>3) {
                return false;
            }
        } else if(c=='.') {
            //cannot have 2 dots side by side
            if (previouswasdot) {
                return false;
            }
            previouswasdot=true;
            internalcount=0;
            dotcount++;
        }//if not a dot neither a digit it is wrong
        else {
            return false;
        }
    }
    //if not 3 dots then it is wrong
    if (dotcount!=3) {
        return false;
    }
    //cannot have the last dot as last char
    if (IP[strlen(IP)-1]=='.') {
        return false;
    }
    return true;
}

char * CONFIG::intTostr(int value)
{
    static char result [12];
    sprintf(result,"%d",value);
    return result;
}

String CONFIG::formatBytes(size_t bytes)
{
    if (bytes < 1024) {
        return String(bytes)+" B";
    } else if(bytes < (1024 * 1024)) {
        return String(bytes/1024.0)+" KB";
    } else if(bytes < (1024 * 1024 * 1024)) {
        return String(bytes/1024.0/1024.0)+" MB";
    } else {
        return String(bytes/1024.0/1024.0/1024.0)+" GB";
    }
}

//helper to convert string to IP
//do not use IPAddress.fromString() because lack of check point and error result
//return number of parts
byte CONFIG::split_ip (const char * ptr,byte * part)
{
    if (strlen(ptr)>15 || strlen(ptr)< 7) {
        part[0]=0;
        part[1]=0;
        part[2]=0;
        part[3]=0;
        return 0;
    }

    char pstart [16];
    char * ptr2;
    strcpy(pstart,ptr);
    ptr2 = pstart;
    byte i = strlen(pstart);
    byte pos = 0;
    for (byte j=0; j<i; j++) {
        if (pstart[j]=='.') {
            if (pos==4) {
                part[0]=0;
                part[1]=0;
                part[2]=0;
                part[3]=0;
                return 0;
            }
            pstart[j]=0x0;
            part[pos]=atoi(ptr2);
            pos++;
            ptr2 = &pstart[j+1];
        }
    }
    part[pos]=atoi(ptr2);
    return pos+1;
}

//just simple helper to convert mac address to string
char * CONFIG::mac2str(uint8_t mac [WL_MAC_ADDR_LENGTH])
{
    static char macstr [18];
    if (0>sprintf(macstr,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5])) {
        strcpy (macstr, "00:00:00:00:00:00");
    }
    return macstr;
}


//read a string
//a string is multibyte + \0, this is won't work if 1 char is multibyte like chinese char
bool CONFIG::read_string(int pos, char byte_buffer[], int size_max)
{
    //check if parameters are acceptable
    if (size_max==0 ||  pos+size_max+1 > EEPROM_SIZE || byte_buffer== NULL) {
        LOG("Error read string\r\n")
        return false;
    }
    EEPROM.begin(EEPROM_SIZE);
    byte b = 13; // non zero for the while loop below
    int i=0;

    //read until max size is reached or \0 is found
    while (i < size_max && b != 0) {
        b = EEPROM.read(pos+i);
        byte_buffer[i]=b;
        i++;
    }

    // Be sure there is a 0 at the end.
    if (b!=0) {
        byte_buffer[i-1]=0x00;
    }
    EEPROM.end();

    return true;
}

bool CONFIG::read_string(int pos, String & sbuffer, int size_max)
{
    //check if parameters are acceptable
    if (size_max==0 ||  pos+size_max+1 > EEPROM_SIZE ) {
        LOG("Error read string\r\n")
        return false;
    }
    byte b = 13; // non zero for the while loop below
    int i=0;
    sbuffer="";

    EEPROM.begin(EEPROM_SIZE);
    //read until max size is reached or \0 is found
    while (i < size_max && b != 0) {
        b = EEPROM.read(pos+i);
        if (b!=0) {
            sbuffer+=char(b);
        }
        i++;
    }
    EEPROM.end();

    return true;
}

//read a buffer of size_buffer
bool CONFIG::read_buffer(int pos, byte byte_buffer[], int size_buffer)
{
    //check if parameters are acceptable
    if (size_buffer==0 ||  pos+size_buffer > EEPROM_SIZE || byte_buffer== NULL) {
        LOG("Error read buffer\r\n")
        return false;
    }
    int i=0;
    EEPROM.begin(EEPROM_SIZE);
    //read until max size is reached
    while (i<size_buffer ) {
        byte_buffer[i]=EEPROM.read(pos+i);
        i++;
    }
    EEPROM.end();
    return true;
}

//read a flag / byte
bool CONFIG::read_byte(int pos, byte * value)
{
    //check if parameters are acceptable
    if (pos+1 > EEPROM_SIZE) {
        LOG("Error read byte\r\n")
        return false;
    }
    EEPROM.begin(EEPROM_SIZE);
    value[0] = EEPROM.read(pos);
    EEPROM.end();
    return true;
}

bool CONFIG::write_string(int pos, const __FlashStringHelper *str)
{
    String stmp = str;
    return write_string(pos,stmp.c_str());
}

bool CONFIG::check_update_presence( ){ 
     bool result = false;
     if (CONFIG::is_direct_sd) { 
         long baud_rate=0;
         if (!CONFIG::read_buffer(EP_BAUD_RATE,  (byte *)&baud_rate, INTEGER_LENGTH)) return false;
         Serial.begin(baud_rate);
         CONFIG::InitFirmwareTarget();
         String cmd = "M20";
         if (CONFIG::FirmwareTarget == UNKNOWN_FW) return false;
         if (CONFIG::FirmwareTarget == SMOOTHIEWARE) {
             byte sd_dir = 0;
             if (!CONFIG::read_byte(EP_PRIMARY_SD, &sd_dir )) sd_dir = DEFAULT_PRIMARY_SD;
             if (sd_dir == SD_DIRECTORY) cmd = "ls /sd";
             else if (sd_dir == EXT_DIRECTORY) cmd = "ls /ext";
             else return false;
         }
        String tmp;
        
        int count ;
        //send command to serial as no need to transfer ESP command
        //to avoid any pollution if Uploading file to SDCard
        //block every query
        //empty the serial buffer and incoming data
        if(Serial.available()) {
            BRIDGE::processFromSerial2TCP();
            delay(1);
        }
        //Send command
        Serial.println(cmd);
        count = 0;
        String current_buffer;
        String current_line;
        int pos;
        int temp_counter = 0;
       
        //pickup the list
        while (count < MAX_TRY) {
            //give some time between each buffer
            if (Serial.available()) {
                count = 0;
                size_t len = Serial.available();
                uint8_t sbuf[len+1];
                //read buffer
                Serial.readBytes(sbuf, len);
                //change buffer as string
                sbuf[len]='\0';
                //add buffer to current one if any
                current_buffer += (char * ) sbuf;
                while (current_buffer.indexOf("\n") !=-1) {
                    //remove the possible "\r"
                    current_buffer.replace("\r","");
                    pos = current_buffer.indexOf("\n");
                    //get line
                    current_line = current_buffer.substring(0,current_buffer.indexOf("\n"));
                    //if line is command ack - just exit so save the time out period
                    if ((current_line == "ok") || (current_line == "wait")) {
                        count = MAX_TRY;
                        break;
                    }
                    //check line
                    //save time no need to continue
                    if (current_line.indexOf("busy:") > -1 || current_line.indexOf("T:") > -1 || current_line.indexOf("B:") > -1) {
                        temp_counter++;
                    } else {
                    }
                    if (temp_counter > 5) {
                        break;
                    }
                    //current remove line from buffer
                    tmp = current_buffer.substring(current_buffer.indexOf("\n")+1,current_buffer.length());
                    current_buffer = tmp;
                    delay(0);
                }
                delay (0);
            } else {
                delay(1);
            }
            //it is sending too many temp status should be heating so let's exit the loop
            if (temp_counter > 5) {
                count = MAX_TRY;
            }
            count++;
        }
        if(Serial.available()) {
            BRIDGE::processFromSerial2TCP();
            delay(1);
        }
    }
    return result;
}


//write a string (array of byte with a 0x00  at the end)
bool CONFIG::write_string(int pos, const char * byte_buffer)
{
    int size_buffer;
    int maxsize = EEPROM_SIZE;
    size_buffer= strlen(byte_buffer);
    //check if parameters are acceptable
    switch (pos) {
    case EP_ADMIN_PWD:
    case EP_USER_PWD:
        maxsize = MAX_LOCAL_PASSWORD_LENGTH;
        break;
    case EP_AP_SSID:
    case EP_STA_SSID:
        maxsize = MAX_SSID_LENGTH;
        break;
    case EP_AP_PASSWORD:
    case EP_STA_PASSWORD:
        maxsize = MAX_PASSWORD_LENGTH;
        break;
    case EP_HOSTNAME:
        maxsize = MAX_HOSTNAME_LENGTH;
        break;
    case EP_TIME_SERVER1:
    case EP_TIME_SERVER2:
    case EP_TIME_SERVER3:
    case EP_DATA_STRING:
        maxsize = MAX_DATA_LENGTH;
        break;
    default:
        maxsize = EEPROM_SIZE;
        break;
    }
    if ((size_buffer==0 && !(pos == EP_DATA_STRING)) ||  pos+size_buffer+1 > EEPROM_SIZE || size_buffer > maxsize  || byte_buffer== NULL) {
        LOG("Error write string\r\n")
        return false;
    }
    //copy the value(s)
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < size_buffer; i++) {
        EEPROM.write(pos + i, byte_buffer[i]);
    }

    //0 terminal
    EEPROM.write(pos + size_buffer, 0x00);
    EEPROM.commit();
    EEPROM.end();
    return true;
}

//write a buffer
bool CONFIG::write_buffer(int pos, const byte * byte_buffer, int size_buffer)
{
    //check if parameters are acceptable
    if (size_buffer==0 ||  pos+size_buffer > EEPROM_SIZE || byte_buffer== NULL) {
        LOG("Error write buffer\r\n")
        return false;
    }
    EEPROM.begin(EEPROM_SIZE);
    //copy the value(s)
    for (int i = 0; i < size_buffer; i++) {
        EEPROM.write(pos + i, byte_buffer[i]);
    }
    EEPROM.commit();
    EEPROM.end();
    return true;
}

//read a flag / byte
bool CONFIG::write_byte(int pos, const byte value)
{
    //check if parameters are acceptable
    if (pos+1 > EEPROM_SIZE) {
        LOG("Error write byte\r\n")
        return false;
    }
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.write(pos, value);
    EEPROM.commit();
    EEPROM.end();
    return true;
}

bool CONFIG::reset_config()
{
    if(!CONFIG::write_string(EP_DATA_STRING,"")) {
        return false;
    }
    if(!CONFIG::write_byte(EP_WIFI_MODE,DEFAULT_WIFI_MODE)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_BAUD_RATE,(const byte *)&DEFAULT_BAUD_RATE,INTEGER_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_string(EP_AP_SSID,FPSTR(DEFAULT_AP_SSID))) {
        return false;
    }
    if(!CONFIG::write_string(EP_AP_PASSWORD,FPSTR(DEFAULT_AP_PASSWORD))) {
        return false;
    }
    if(!CONFIG::write_string(EP_STA_SSID,FPSTR(DEFAULT_STA_SSID))) {
        return false;
    }
    if(!CONFIG::write_string(EP_STA_PASSWORD,FPSTR(DEFAULT_STA_PASSWORD))) {
        return false;
    }
    if(!CONFIG::write_byte(EP_AP_IP_MODE,DEFAULT_AP_IP_MODE)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_STA_IP_MODE,DEFAULT_STA_IP_MODE)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_STA_IP_VALUE,DEFAULT_IP_VALUE,IP_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_STA_MASK_VALUE,DEFAULT_MASK_VALUE,IP_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_STA_GATEWAY_VALUE,DEFAULT_GATEWAY_VALUE,IP_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_STA_PHY_MODE,DEFAULT_PHY_MODE)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_AP_IP_VALUE,DEFAULT_IP_VALUE,IP_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_AP_MASK_VALUE,DEFAULT_MASK_VALUE,IP_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_AP_GATEWAY_VALUE,DEFAULT_GATEWAY_VALUE,IP_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_AP_PHY_MODE,DEFAULT_PHY_MODE)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_SLEEP_MODE,DEFAULT_SLEEP_MODE)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_CHANNEL,DEFAULT_CHANNEL)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_AUTH_TYPE,DEFAULT_AUTH_TYPE)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_SSID_VISIBLE,DEFAULT_SSID_VISIBLE)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_WEB_PORT,(const byte *)&DEFAULT_WEB_PORT,INTEGER_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_DATA_PORT,(const byte *)&DEFAULT_DATA_PORT,INTEGER_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_REFRESH_PAGE_TIME,DEFAULT_REFRESH_PAGE_TIME)) {
        return false;
    }
    if(!CONFIG::write_byte(EP_REFRESH_PAGE_TIME2,DEFAULT_REFRESH_PAGE_TIME)) {
        return false;
    }
    if(!CONFIG::write_string(EP_HOSTNAME,wifi_config.get_default_hostname())) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_XY_FEEDRATE,(const byte *)&DEFAULT_XY_FEEDRATE,INTEGER_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_Z_FEEDRATE,(const byte *)&DEFAULT_Z_FEEDRATE,INTEGER_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_buffer(EP_E_FEEDRATE,(const byte *)&DEFAULT_E_FEEDRATE,INTEGER_LENGTH)) {
        return false;
    }
    if(!CONFIG::write_string(EP_ADMIN_PWD,FPSTR(DEFAULT_ADMIN_PWD))) {
        return false;
    }
    if(!CONFIG::write_string(EP_USER_PWD,FPSTR(DEFAULT_USER_PWD))) {
        return false;
    }
    
    if(!CONFIG::write_byte(EP_TARGET_FW, UNKNOWN_FW)) {
        return false;
    }
    
    if(!CONFIG::write_byte(EP_TIMEZONE, DEFAULT_TIME_ZONE)) {
        return false;
    }
    
    if(!CONFIG::write_byte(EP_TIME_ISDST, DEFAULT_TIME_DST)) {
        return false;
    }
    
     if(!CONFIG::write_byte(EP_PRIMARY_SD, DEFAULT_PRIMARY_SD)) {
        return false;
    }
    
     if(!CONFIG::write_byte(EP_SECONDARY_SD, DEFAULT_SECONDARY_SD)) {
        return false;
    }
    
    if(!CONFIG::write_byte(EP_IS_DIRECT_SD, DEFAULT_IS_DIRECT_SD)) {
        return false;
    }
    
     if(!CONFIG::write_byte(EP_DIRECT_SD_CHECK, DEFAULT_DIRECT_SD_CHECK)) {
        return false;
    }

    if(!CONFIG::write_string(EP_TIME_SERVER1,FPSTR(DEFAULT_TIME_SERVER1))) {
        return false;
    }
    
    if(!CONFIG::write_string(EP_TIME_SERVER2,FPSTR(DEFAULT_TIME_SERVER2))) {
        return false;
    }
    
    if(!CONFIG::write_string(EP_TIME_SERVER3,FPSTR(DEFAULT_TIME_SERVER3))) {
        return false;
    }
    return true;
}

void CONFIG::print_config(tpipe output, bool plaintext)
{    
    if (!plaintext)BRIDGE::print(F("{\"chip_id\":\""), output);
    else BRIDGE::print(F("Chip ID: "), output);
    BRIDGE::print(String(ESP.getChipId()).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"cpu\":\""), output);
    else BRIDGE::print(F("CPU Frequency: "), output);
    BRIDGE::print(String(ESP.getCpuFreqMHz()).c_str(), output);
    BRIDGE::print(F("Mhz"), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"freemem\":\""), output);
    else BRIDGE::print(F("Free memory: "), output);
    BRIDGE::print(formatBytes(ESP.getFreeHeap()).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\""), output);
    BRIDGE::print(F("SDK"), output);
    if (!plaintext)BRIDGE::print(F("\":\""), output);
    else BRIDGE::print(F(": "), output);
    BRIDGE::print(ESP.getSdkVersion(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
      
    if (!plaintext)BRIDGE::print(F("\"flash_size\":\""), output);
    else BRIDGE::print(F("Flash Size: "), output);
    BRIDGE::print(formatBytes(ESP.getFlashChipSize()).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"update_size\":\""), output);
    else BRIDGE::print(F("Available Size for update: "), output);
    uint32_t  flashsize = ESP.getFlashChipSize();
    if (flashsize > 1024 * 1024) flashsize = 1024 * 1024;
    BRIDGE::print(formatBytes(flashsize - ESP.getSketchSize()).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else {
        if ((flashsize - ESP.getSketchSize()) > (flashsize / 2)) BRIDGE::println(F("(Ok)"), output);
        else BRIDGE::print(F("(Not enough)"), output);
        }
    
    if (!plaintext)BRIDGE::print(F("\"spiffs_size\":\""), output);
    else BRIDGE::print(F("Available Size for SPIFFS: "), output);
    if (ESP.getFlashChipRealSize() > 1024 * 1024) {
        BRIDGE::print(formatBytes(ESP.getFlashChipRealSize()-(1024 * 1024)).c_str(), output);
        }
    else {
        BRIDGE::print(formatBytes(ESP.getFlashChipSize() - (ESP.getSketchSize()*2)).c_str(), output);
    }
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"baud_rate\":\""), output);
    else BRIDGE::print(F("Baud rate: "), output);
    BRIDGE::print(String(Serial.baudRate()).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);

    if (!plaintext)BRIDGE::print(F("\"sleep_mode\":\""), output);
    else BRIDGE::print(F("Sleep mode: "), output);
    if (WiFi.getSleepMode() == WIFI_NONE_SLEEP) {
        BRIDGE::print(F("None"), output);
    } else if (WiFi.getSleepMode() == WIFI_LIGHT_SLEEP) {
        BRIDGE::print(F("Light"), output);
    } else if (WiFi.getSleepMode() == WIFI_MODEM_SLEEP) {
        BRIDGE::print(F("Modem"), output);
    } else {
        BRIDGE::print(F("???"), output);
    }
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"channel\":\""), output);
    else BRIDGE::print(F("Channel: "), output);
    BRIDGE::print(String(WiFi.channel()).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"phy_mode\":\""), output);
    else BRIDGE::print(F("Phy Mode: "), output);
    if (WiFi.getPhyMode() == WIFI_PHY_MODE_11G )BRIDGE::print(F("11g"), output);
    else if (WiFi.getPhyMode() == WIFI_PHY_MODE_11B )BRIDGE::print(F("11b"), output);
    else if (WiFi.getPhyMode() == WIFI_PHY_MODE_11N )BRIDGE::print(F("11n"), output);
    else BRIDGE::print(F("???"), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"web_port\":\""), output);
    else BRIDGE::print(F("Web port: "), output);
    BRIDGE::print(String(wifi_config.iweb_port).c_str(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"data_port\":\""), output);
    else BRIDGE::print(F("Data port: "), output);
    #ifdef TCP_IP_DATA_FEATURE
    BRIDGE::print(String(wifi_config.idata_port).c_str(), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
        if (!plaintext)BRIDGE::print(F("\"hostname\":\""), output);
        else BRIDGE::print(F("Hostname: "), output);
        BRIDGE::print(WiFi.hostname().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
    }

    if (!plaintext)BRIDGE::print(F("\"active_mode\":\""), output);
    else BRIDGE::print(F("Active Mode: "), output);
    if (WiFi.getMode() == WIFI_STA) {
        BRIDGE::print(F("Station ("), output);
        BRIDGE::print(WiFi.macAddress().c_str(), output);
        BRIDGE::print(F(")"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        if (WiFi.isConnected()) {
             if (!plaintext)BRIDGE::print(F("\"connected_ssid\":\""), output);
            else BRIDGE::print(F("Connected to: "), output);
            BRIDGE::print(WiFi.SSID().c_str(), output);
            if (!plaintext)BRIDGE::print(F("\","), output);
            else BRIDGE::print(F("\n"), output);
            if (!plaintext)BRIDGE::print(F("\"connected_signal\":\""), output);
            else BRIDGE::print(F("Signal: "), output);
            BRIDGE::print(String(wifi_config.getSignal(WiFi.RSSI())).c_str(), output);
            BRIDGE::print(F("%"), output);
            if (!plaintext)BRIDGE::print(F("\","), output);
            else BRIDGE::print(F("\n"), output);
            }
        else {
             if (!plaintext)BRIDGE::print(F("\"connection_status\":\""), output);
            else BRIDGE::print(F("Connection Status: "), output);
            BRIDGE::print(F("Connection Status: "), output);
            if (WiFi.status() == WL_DISCONNECTED) {
                BRIDGE::print(F("Disconnected"), output);
            } else if (WiFi.status() == WL_CONNECTION_LOST) {
                BRIDGE::print(F("Connection lost"), output);
            } else if (WiFi.status() == WL_CONNECT_FAILED) {
                BRIDGE::print(F("Connection failed"), output);
            } else if (WiFi.status() == WL_NO_SSID_AVAIL) {
                BRIDGE::print(F("No connection"), output);
            } else if (WiFi.status() == WL_IDLE_STATUS   ) {
                BRIDGE::print(F("Idle"), output);
            } else  BRIDGE::print(F("Unknown"), output);
            if (!plaintext)BRIDGE::print(F("\","), output);
            else BRIDGE::print(F("\n"), output);
        }
         if (!plaintext)BRIDGE::print(F("\"ip_mode\":\""), output);
        else BRIDGE::print(F("IP Mode: "), output);
        if (wifi_station_dhcpc_status()==DHCP_STARTED) BRIDGE::print(F("DHCP"), output);
        else BRIDGE::print(F("Static"), output);
         if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"ip\":\""), output);
        else BRIDGE::print(F("IP: "), output);
        BRIDGE::print(WiFi.localIP().toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"gw\":\""), output);
        else BRIDGE::print(F("Gateway: "), output);
        BRIDGE::print(WiFi.gatewayIP().toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"msk\":\""), output);
        else BRIDGE::print(F("Mask: "), output);
        BRIDGE::print(WiFi.subnetMask().toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"dns\":\""), output);
        else BRIDGE::print(F("DNS: "), output);
        BRIDGE::print(WiFi.dnsIP().toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"disabled_mode\":\""), output);
        else BRIDGE::print(F("Disabled Mode: "), output);
        BRIDGE::print(F("Access Point ("), output);
        BRIDGE::print(WiFi.softAPmacAddress().c_str(), output);
        BRIDGE::print(F(")"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
    } else if (WiFi.getMode() == WIFI_AP) {
        BRIDGE::print(F("Access Point ("), output);
        BRIDGE::print(WiFi.softAPmacAddress().c_str(), output);
        BRIDGE::print(F(")"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        //get current config
        struct softap_config apconfig;
        wifi_softap_get_config(&apconfig);
        if (!plaintext)BRIDGE::print(F("\"ap_ssid\":\""), output);
        else BRIDGE::print(F("SSID: "), output);
        BRIDGE::print((const char*)apconfig.ssid, output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"ssid_visible\":\""), output);
        else BRIDGE::print(F("Visible: "), output);
        BRIDGE::print((apconfig.ssid_hidden == 0)?F("Yes"):F("No"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"ssid_authentication\":\""), output);
        else BRIDGE::print(F("Authentication: "), output);
        if (apconfig.authmode==AUTH_OPEN) {
            BRIDGE::print(F("None"), output);
        } else if (apconfig.authmode==AUTH_WEP) {
            BRIDGE::print(F("WEP"), output);
        } else if (apconfig.authmode==AUTH_WPA_PSK) {
           BRIDGE::print(F("WPA"), output);
        } else if (apconfig.authmode==AUTH_WPA2_PSK) {
            BRIDGE::print(F("WPA2"), output);
        } else {
            BRIDGE::print(F("WPA/WPA2"), output);
        }
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"ssid_max_connections\":\""), output);
        else BRIDGE::print(F("Max Connections: "), output);
        BRIDGE::print(String(apconfig.max_connection).c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"ssid_dhcp\":\""), output);
        else BRIDGE::print(F("DHCP Server: "), output);
        if(wifi_softap_dhcps_status() == DHCP_STARTED) BRIDGE::print(F("Started"), output);
        else BRIDGE::print(F("Stopped"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"ip\":\""), output);
        else BRIDGE::print(F("IP: "), output);
        BRIDGE::print(WiFi.softAPIP().toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        struct ip_info ip;
        wifi_get_ip_info(SOFTAP_IF, &ip);
        if (!plaintext)BRIDGE::print(F("\"gw\":\""), output);
        else BRIDGE::print(F("Gateway: "), output);
        BRIDGE::print(IPAddress(ip.gw.addr).toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        if (!plaintext)BRIDGE::print(F("\"msk\":\""), output);
        else BRIDGE::print(F("Mask: "), output);
        BRIDGE::print(IPAddress(ip.netmask.addr).toString().c_str(), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
        
        if (!plaintext)BRIDGE::print(F("\"connected_clients\":["), output);
        else BRIDGE::print(F("Connected clients: "), output);
        int client_counter=0;
        struct station_info * station;
        station = wifi_softap_get_station_info();
        String stmp ="";
        while(station) {
            if(stmp.length() > 0) {
                if (!plaintext)stmp+=F(",");
                else stmp+=F("\n");
                
            }
            if (!plaintext)stmp+=F("{\"bssid\":\"");
            //BSSID
            stmp += CONFIG::mac2str(station->bssid);
            if (!plaintext)stmp+=F("\",\"ip\":\"");
            else stmp += F(" ");
            //IP
            stmp += IPAddress((const uint8_t *)&station->ip).toString().c_str();
            if (!plaintext)stmp+=F("\"}");
            //increment counter
            client_counter++;
            //go next record
            station = station->next;
        }
        wifi_softap_free_station_info();
        if (!plaintext) {
            BRIDGE::print(stmp.c_str(), output);
            BRIDGE::print(F("],"), output);
            }
        else {
                //display number of client
                BRIDGE::println(String(client_counter).c_str(), output);
                //display list if any
                if(stmp.length() > 0)BRIDGE::println(stmp.c_str(), output);
            }
        
        if (!plaintext)BRIDGE::print(F("\"disabled_mode\":\""), output);
        else BRIDGE::print(F("Disabled Mode: "), output);
        BRIDGE::print(F("Station ("), output);
        BRIDGE::print(WiFi.macAddress().c_str(), output);
        BRIDGE::print(F(") is disabled"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        
    } else if (WiFi.getMode() == WIFI_AP_STA) {
        BRIDGE::print(F("Mixed"), output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
        if (!plaintext)BRIDGE::print(F("\"active_mode\":\""), output);
        else BRIDGE::print(F("Active Mode: "), output);
        BRIDGE::print(F("Access Point ("), output);
        BRIDGE::print(WiFi.softAPmacAddress().c_str(), output);
        BRIDGE::println(F(")"), output);
        BRIDGE::print(F("Station ("), output);
        BRIDGE::print(WiFi.macAddress().c_str(), output);
        BRIDGE::print(F(")"), output);
         if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
    } else {
        BRIDGE::print("Wifi Off", output);
        if (!plaintext)BRIDGE::print(F("\","), output);
        else BRIDGE::print(F("\n"), output);
    }

    if (!plaintext)BRIDGE::print(F("\"captive_portal\":\""), output);
    else BRIDGE::print(F("Captive portal: "), output);
#ifdef CAPTIVE_PORTAL_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"ssdp\":\""), output);
    else BRIDGE::print(F("SSDP: "), output);
#ifdef SSDP_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"netbios\":\""), output);
    else BRIDGE::print(F("NetBios: "), output);
#ifdef NETBIOS_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"mdns\":\""), output);
    else BRIDGE::print(F("mDNS: "), output);
#ifdef MDNS_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);

    if (!plaintext)BRIDGE::print(F("\"web_update\":\""), output);
    else BRIDGE::print(F("Web Update: "), output);
#ifdef WEB_UPDATE_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);

    if (!plaintext)BRIDGE::print(F("\"pin recovery\":\""), output);
    else BRIDGE::print(F("Pin Recovery: "), output);
#ifdef RECOVERY_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);

    if (!plaintext)BRIDGE::print(F("\"autentication\":\""), output);
    else BRIDGE::print(F("Authentication: "), output);
#ifdef AUTHENTICATION_FEATURE
    BRIDGE::print(F("Enabled"), output);
#else
    BRIDGE::print(F("Disabled"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
    
    if (!plaintext)BRIDGE::print(F("\"target_fw\":\""), output);
    else BRIDGE::print(F("Target Firmware: "), output);
    BRIDGE::print(CONFIG::GetFirmwareTargetName(), output);
    if (!plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);

#ifdef DEBUG_ESP3D
    if (!plaintext)BRIDGE::print(F("\"debug\":\""), output);
    else BRIDGE::print(F("Debug: "), output);
    BRIDGE::print(F("Debug Enabled :"), output);
#ifdef DEBUG_OUTPUT_SPIFFS
    BRIDGE::print(F("SPIFFS"), output);
#endif
#ifdef DEBUG_OUTPUT_SD
    BRIDGE::print(F("SD"), output);
#endif
#ifdef DEBUG_OUTPUT_SERIAL
    BRIDGE::print(F("serial"), output);
#endif
#ifdef DEBUG_OUTPUT_TCP
    BRIDGE::print(F("TCP"), output);
#endif
    if (plaintext)BRIDGE::print(F("\","), output);
    else BRIDGE::print(F("\n"), output);
#endif
    if (!plaintext)BRIDGE::print(F("\"fw\":\""), output);
    else BRIDGE::print(F("FW version: "), output);
    BRIDGE::print(FW_VERSION, output);
    if (!plaintext)BRIDGE::print(F("\"}"), output);
    else BRIDGE::print(F("\n"), output);
}

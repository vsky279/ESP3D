# ESP3D
Firmware used with 3D printer  for :   
ESP8266/ESP8285 using [arduino core version](https://github.com/esp8266/Arduino)   
ESP32 using [arduino core version](https://github.com/espressif/arduino-esp32)   
This firmware allows not only to have a cheap bridge between Wifi and serial, but also to have a web UI to configure wifi, to monitor 3D printer and even control it, and to make things easy,
UI is fully customizable without reflashing FW.
Firmware should work with any 3D printer firmware (repetier/marlin/etc..) if serial connection has correct setup.
I currently use it with my personnal flavor of [repetier for Due based boards](https://github.com/luc-github/Repetier-Firmware-0.92).
Please use ESP8266 with at least 1M flash to get Web Update working

<u>Stable version:</u>    
<u>Development version:</u>    
Arduino ide 1.8.4 with git from ESP8266 and git from ESP32 : [![build status](https://travis-ci.org/luc-github/ESP3D.svg?branch=devt)](https://travis-ci.org/luc-github/ESP3D)

[All releases](https://github.com/luc-github/ESP3D/wiki)

:question:Any question ?[![Join the chat at https://gitter.im/luc-github/ESP3D](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/luc-github/ESP3D?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)   
:exclamation:Any issue ? check [FAQ](https://github.com/luc-github/ESP3D/issues?utf8=%E2%9C%93&q=label%3AFAQ+) or [submit ticket](https://github.com/luc-github/ESP3D/issues)    


:+1:Thanks
* to @disneysw for bringing this module idea
* to @lkarlslund for suggestion about independant reset using GPIO2
* to all contributors (treepleks, j0hnlittle, openhardwarecoza, TRoager, all feedbacks owners and donations)

Every support is welcome: [<img src="https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG_global.gif" border="0" alt="PayPal – The safer, easier way to pay online.">](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=Y8FFE7NA4LJWQ)    
Especially if need to buy new modules for testing.

## Features
* Serial/Wifi bridge using configurable port 8888, here to enable/disable [TCP_IP_DATA_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* Use GPIO2 to ground to reset all settings in hard way - 2-6 sec after boot / not before!! Set GPIO2 to ground before boot change boot mode and go to special boot that do not reach FW. Currently boot take 10 sec - giving 8 seconds to connect GPIO2 to GND and do an hard recovery for settings, here to enable/disable [RECOVERY_FEATURE](https://github.com/luc-github/ESP8266/blob/master/esp8266/config.h)   
* Wifi configuration by web browser (Station or Access point)
* Authentication for sensitive pages, here to enable/disable [AUTHENTICATION_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* Update firmware by web browser, here to enable/disable [WEB_UPDATE_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* Control ESP module using commands on serial or data port, here to enable/disable [SERIAL_COMMAND_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* UI fully constomizable without reflashing FW using html templates, [keywords](https://raw.githubusercontent.com/luc-github/ESP3D/master/docs/keywords.txt) and html files/images
* Captive portal in Access point mode which redirect all unknow call to main page, here to enable/disable [CAPTIVE_PORTAL_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h) 
* mDNS which allows to key the name defined in web browser and connect only with bonjour installed on computer, here to enable/disable [MDNS_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* SSDP, this feature is a discovery protocol, supported on Windows out of the box, here to enable/disable [SSDP_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* Printer monitoring / control (temperatures/speed/jog/list SDCard content/launch,pause or stop a print/etc...), here to enable/disable [MONITORING_FEATURE/INFO_MSG_FEATURE/ERROR_MSG_FEATURE/STATUS_MSG_FEATURE](https://github.com/luc-github/ESP3D/blob/master/esp3d/config.h)
* Fail safe mode (Access point)is enabled if cannot connect to defined station at boot.

## Web configuration      
*Wifi Mode : Access point / Client station  
*IP Generation: DHCP/Static IP      
*IP/MASK/GATEWAY for static data    
*Baud Rate for serial (supported : 9600, 19200, 38400, 57600, 115200, 230400, 250000)    
*web port and data port      

## Commands
The list of available commands is here: [command.txt](https://raw.githubusercontent.com/luc-github/ESP3D/devt/docs/Commands.txt)    
    
## Default Configuration      
Default Settings:    
AP:ESP8266    
PW:12345678   
Authentification: WPA     
Mode: g (n is not supported by AP, just by STA)    
channel: 11    
AP: visible    
Sleep Mode: Modem    
IP Mode: Static IP    
IP: 192.168.0.1   
Mask: 255.255.255.0   
GW:192.168.0.1    
Baud rate: 9600   
Web port:80   
Data port: 8888     
Web Page refresh: 3 secondes    
User (if authentication is enabled): admin     
Password (if authentication is enabled): admin
User (if authentication is enabled):user
Password(if authentication is enabled): user 

## Installation
Please use [Arduino IDE 1.8.4](http://arduino.cc/en/Main/Software)
and install ESP core according your board:
* ESP8266/ESP8285
https://github.com/esp8266/Arduino#using-git-version
* ESP32
https://github.com/espressif/arduino-esp32#installation-instructions
In addition you will need to copy the WebServer and DNSServer libraries to library directory of ESP32 as they are currently not installed by default



* Parameter to flash the module :   
```
//MDNS_FEATURE: this feature allow  type the name defined
//in web browser by default: http:\\esp8266.local and connect
//#define MDNS_FEATURE

//SSDD_FEATURE: this feature is a discovery protocol, supported on Windows out of the box
#define SSDP_FEATURE

//CAPTIVE_PORTAL_FEATURE: In SoftAP redirect all unknow call to main page
#define CAPTIVE_PORTAL_FEATURE

//AUTHENTICATION_FEATURE: protect pages by login password
#define AUTHENTICATION_FEATURE

//WEB_UPDATE_FEATURE: allow to flash fw using web UI
#define WEB_UPDATE_FEATURE

//SERIAL_COMMAND_FEATURE: allow to send command by serial
#define SERIAL_COMMAND_FEATURE

//TCP_IP_DATA_FEATURE: allow to connect serial from TCP/IP
#define TCP_IP_DATA_FEATURE

//RECOVERY_FEATURE: allow to use GPIO2 pin as hardware reset for EEPROM, add 8s to boot time to let user to jump GPIO2 to GND
#define RECOVERY_FEATURE
```
## ESP8266/ESP8285
On ESP8266 for better performance select CPU Frequency to be 160MHz instead of default 80MHz

## ESP32
* To get silent boot connect GPIO 15 to GND or select default output as Serial2 instead of Serial in config.h
`#define ESP_SERIAL_OUT Serial2`

* Currently NETBIOS and SSDP libraries are not available on ESP32 and are automaticaly disabled.

## After flash
UI is present in data directory and must be flashed on SPIFFS also but latest version can found at https://github.com/luc-github/ESP3D-WEBUI   
404.html (the page not found) is not mandatory, a fail safe version is embeded if it is not present.    
If no index.html.gz file is found a default web page is displayed to upload web server files (the ones present in data directory), this page can also be used to flash FW    
You can also use a pluggin to flash data content to SPIFFS.   
* For ESP8266: installation and usage is explained [here](https://github.com/esp8266/Arduino/blob/master/doc/filesystem.md#uploading-files-to-file-system)    
If no target firmware is defined (Marlin / repetier, etc...) do not forget to set it during setup wizard or after in ESP3D settings   
   
* To flash the html files present in data directory you need to use another tool, installation and usage is explained [here](https://github.com/esp8266/Arduino/blob/master/doc/filesystem.md#uploading-files-to-file-system)    
Once flashed you also can use the web updater to flash new FW in System Configuration Page or go to settings to change html files   

<H3>:warning:Do not flash Printer fw with ESP connected - it bring troubles, at least on DaVinci</H3>



## TODO   
-- Close open topics    
-- Do testing (a lot)    
-- UI Improvement   

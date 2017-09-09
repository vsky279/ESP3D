/*
  webinterface.cpp - ESP3D configuration class

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

#include <pgmspace.h>
#include "config.h"
#include "webinterface.h"
#include "wifi.h"
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "GenLinkedList.h"
#include "storestrings.h"
#include "command.h"
#include "bridge.h"
#ifndef FS_NO_GLOBALS
#define FS_NO_GLOBALS
#endif
#include <FS.h>

#ifdef SSDP_FEATURE
#include <ESP8266SSDP.h>
#endif

//embedded response file if no files on SPIFFS
#include "nofile.h"

#define MAX_AUTH_IP 10
#define HIDDEN_PASSWORD "********"


typedef enum {
    UPLOAD_STATUS_NONE = 0,
    UPLOAD_STATUS_FAILED = 1,
    UPLOAD_STATUS_CANCELLED = 2,
    UPLOAD_STATUS_SUCCESSFUL = 3,
    UPLOAD_STATUS_ONGOING  =4
} upload_status_type;

const char PAGE_404 [] PROGMEM ="<HTML>\n<HEAD>\n<title>Redirecting...</title> \n</HEAD>\n<BODY>\n<CENTER>Unknown page : $QUERY$- you will be redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) \n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";
const char PAGE_CAPTIVE [] PROGMEM ="<HTML>\n<HEAD>\n<title>Captive Portal</title> \n</HEAD>\n<BODY>\n<CENTER>Captive Portal page : $QUERY$- you will be redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) \n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";
const char CONTENT_TYPE_HTML [] PROGMEM ="text/html";

void handle_web_interface_root()
{
    String path = "/index.html";
    String contentType =  web_interface->getContentType(path);
    String pathWithGz = path + ".gz";
    //if have a index.html or gzip version this is default root page
    if((SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) && !web_interface->WebServer.hasArg("fallback") && web_interface->WebServer.arg("forcefallback")!="yes") {
        if(SPIFFS.exists(pathWithGz)) {
            path = pathWithGz;
        }
         fs::File file = SPIFFS.open(path, "r");
        web_interface->WebServer.streamFile(file, contentType);
        file.close();
        return;
    }
    //if no lets launch the default content
    web_interface->WebServer.sendHeader("Content-Encoding", "gzip");
    web_interface->WebServer.send_P(200,CONTENT_TYPE_HTML,PAGE_NOFILES,PAGE_NOFILES_SIZE);
}

//concat several catched informations temperatures/position/status/flow/speed
void handle_web_interface_status()
{
  //  static const char NO_TEMP_LINE[] PROGMEM = "\"temperature\":\"0\",\"target\":\"0\",\"active\":\"0\"";
    //we do not care if need authentication - just reset counter
    web_interface->is_authenticated();
    int tagpos,tagpos2;
    String buffer2send;
    String value;
    //start JSON answer
    buffer2send="{";
#ifdef INFO_MSG_FEATURE
    //information
    buffer2send.concat(F("\"InformationMsg\":["));

    for (int i=0; i<web_interface->info_msg.size(); i++) {
        if (i>0) {
            buffer2send+=",";
        }
        buffer2send+="{\"line\":\"";
        buffer2send+=web_interface->info_msg.get(i);
        buffer2send+="\"}";
    }
    buffer2send+="],";
#endif
#ifdef ERROR_MSG_FEATURE
    //Error
    buffer2send.concat(F("\"ErrorMsg\":["));
    for (int i=0; i<web_interface->error_msg.size(); i++) {
        if (i>0) {
            buffer2send+=",";
        }
        buffer2send+="{\"line\":\"";
        buffer2send+=web_interface->error_msg.get(i);
        buffer2send+="\"}";
    }
    buffer2send+="],";
#endif
#ifdef STATUS_MSG_FEATURE
    //Status
    buffer2send.concat(F("\"StatusMsg\":["));

    for (int i=0; i<web_interface->status_msg.size(); i++) {
        if (i>0) {
            buffer2send+=",";
        }
        buffer2send+="{\"line\":\"";
        buffer2send+=web_interface->status_msg.get(i);
        buffer2send+="\"}";
    }
    buffer2send+="],";
#endif
    //status color
    buffer2send+="\"status\":\""+value +"\"";
    buffer2send+="}";
    web_interface->WebServer.sendHeader("Cache-Control", "no-cache");
    web_interface->WebServer.send(200, "application/json",buffer2send);
}

//SPIFFS files uploader handle
void SPIFFSFileupload()
{
    //get authentication status
    level_authenticate_type auth_level= web_interface->is_authenticated();
    //Guest cannot upload
    if (auth_level == LEVEL_GUEST) {
        web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
        Serial.println("M117 Error ESP upload");
        web_interface->WebServer.client().stopAll();
        return;
    }
    static String filename;
    //get current file ID
    HTTPUpload& upload = (web_interface->WebServer).upload();
    //Upload start
    //**************
    if(upload.status == UPLOAD_FILE_START) {
        //according User or Admin the root is different as user is isolate to /user when admin has full access
        if(auth_level == LEVEL_ADMIN) {
            filename = upload.filename;
        } else {
            filename = "/user" + upload.filename;
        }
        Serial.println("M117 Start ESP upload");
        //create file
        web_interface->fsUploadFile = SPIFFS.open(filename, "w");
        filename = String();
        //check If creation succeed
        if (web_interface->fsUploadFile) {
            //if yes upload is started
            web_interface->_upload_status= UPLOAD_STATUS_ONGOING;
        } else {
            //if no set cancel flag
            web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
            Serial.println("M117 Error ESP create");
            web_interface->WebServer.client().stopAll();
        }
        //Upload write
        //**************
    } else if(upload.status == UPLOAD_FILE_WRITE) {
        //check if file is available and no error
        if(web_interface->fsUploadFile && web_interface->_upload_status == UPLOAD_STATUS_ONGOING) {
            //no error so write post date
            web_interface->fsUploadFile.write(upload.buf, upload.currentSize);
        } else {
            //we have a problem set flag UPLOAD_STATUS_CANCELLED
            web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
            web_interface->WebServer.client().stopAll();
            Serial.println("M117 Error ESP write");
        }
        //Upload end
        //**************
    } else if(upload.status == UPLOAD_FILE_END) {
        Serial.println("M117 End ESP upload");
        //check if file is still open
        if(web_interface->fsUploadFile) {
            //close it
            web_interface->fsUploadFile.close();
            web_interface->_upload_status=UPLOAD_STATUS_SUCCESSFUL;
        } else {
            //we have a problem set flag UPLOAD_STATUS_CANCELLED
            web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
            web_interface->WebServer.client().stopAll();
            SPIFFS.remove(filename);
            Serial.println("M117 Error ESP close");
        }
        //Upload cancelled
        //**************
    } else {
        web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
        SPIFFS.remove(filename);
        Serial.println("M117 Error ESP upload");
    }
    delay(0);
}

#define NB_RETRY 5
#define MAX_RESEND_BUFFER 128
//SD file upload by serial
void SDFile_serial_upload()
{
    static char buffer_line[MAX_RESEND_BUFFER]; //if need to resend
    static char previous = 0;
    static int  buffer_size;
    static bool com_error = false;
    static bool is_comment = false;
    bool client_closed = false;
    static String filename;
    String response;
    //Guest cannot upload - only admin and user
    if(web_interface->is_authenticated() == LEVEL_GUEST) {
        web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
        Serial.println("M117 SD upload rejected");
        LOG("SD upload rejected\r\n");
        if (!client_closed){
            //web_interface->WebServer.client().stopAll();
            LOG("Need to stop");
            client_closed = true;
        }
        return;
    }
#ifdef DEBUG_PERFORMANCE
    static uint32_t startupload;
    static uint32_t write_time;
    static size_t filesize;
#endif
    //retrieve current file id
    HTTPUpload& upload = (web_interface->WebServer).upload();
    //Upload start
    //**************
    if(upload.status == UPLOAD_FILE_START) {
        //need to lock serial out to avoid garbage in file
        (web_interface->blockserial) = true;
        //init flags
        buffer_size=0;
        com_error = false;
        is_comment = false;
        previous = 0;
        web_interface->_upload_status= UPLOAD_STATUS_ONGOING;
        Serial.println("M117 Uploading...");
        Serial.flush();
#ifdef DEBUG_PERFORMANCE
        startupload = millis();
        write_time = 0;
        filesize = 0;
#endif
        LOG("Clear Serial\r\n");
        if(Serial.available()) {
                //get size of buffer
                size_t len = Serial.available();
                uint8_t sbuf[len+1];
                //read buffer
                Serial.readBytes(sbuf, len);
                //convert buffer to zero end array
                sbuf[len]='\0';
                //use string because easier to handle
                response = (const char*)sbuf;
                LOG(response);
                LOG("\r\n");
                }
        //command to pritnter to start print
        String command = "M28 " + upload.filename;
        LOG(command);
        LOG("\r\n");
        Serial.println(command);
        Serial.flush();
        filename = upload.filename;
        //now need to purge all serial data
        //let's sleep 1s
        //delay(1000);
        for (int retry=0; retry < 400; retry++) { //time out is  5x400ms = 2000ms
            //if there is something in serial buffer
            if(Serial.available()) {
                //get size of buffer
                size_t len = Serial.available();
                uint8_t sbuf[len+1];
                //read buffer
                Serial.readBytes(sbuf, len);
                //convert buffer to zero end array
                sbuf[len]='\0';
                //use string because easier to handle
                response =(const char*)sbuf;
                LOG(response);
                //if there is a wait it means purge is done
                if (response.indexOf("wait")>-1) {
                    LOG("Exit start writing\r\n");
                    break;
                }
               if (response.indexOf("Resend")>-1 || response.indexOf("failed")>-1) {
                    com_error = true;
                    web_interface->blockserial = false;
                    LOG("Error start writing\r\n");
                    break;
                }
            }
            delay(5);
        }
        //Upload write
        //**************
        //upload is on going with data coming by 2K blocks
    } else if((upload.status == UPLOAD_FILE_WRITE) && (com_error == false)) { //if com error no need to send more data to serial
        web_interface->_upload_status= UPLOAD_STATUS_ONGOING;
#ifdef DEBUG_PERFORMANCE
        filesize+=upload.currentSize;
        uint32_t startwrite = millis();
#endif
        for (int pos = 0; pos < upload.currentSize; pos++) { //parse full post data
            if (buffer_size < MAX_RESEND_BUFFER-1) { //raise error/handle if overbuffer - copy is space available
                //remove/ignore every comment to save transfert time and avoid over buffer issues
                if (upload.buf[pos] == ';') {
                    is_comment = true;
                    previous = ';';
                }
                if (!is_comment) {
                    buffer_line[buffer_size] = upload.buf[pos]; //copy current char to buffer to send/resend
                    buffer_size++;
                    //convert buffer to zero end array
                    buffer_line[buffer_size] = '\0';
                    //check it is not an end line char and line is not empty
                    if (((buffer_line[0] == '\n') && (buffer_size==1)) ||((buffer_line[1] == '\n') && (buffer_line[0] == '\r') && (buffer_size==2)) || ((buffer_line[0] == ' ') && (buffer_size==1)) ) {
                        //ignore empty line
                        buffer_size=0;
                        buffer_line[buffer_size] = '\0';
                    }
                    //line is not empty so check if last char is an end line
                    //if error no need to proceed
                    else if (((buffer_line[buffer_size-1] == '\n')) && (com_error == false)) { //end of line and no error
                        //if resend use buffer
                        bool success = false;

                        //check NB_RETRY times if get no error when send line
                        for (int r = 0 ; r < NB_RETRY ; r++) {
                            response = "";
                            //print out line
                            Serial.print(buffer_line);
                            LOG(buffer_line);
                            //ensure buffer is empty before continuing
                            Serial.flush();
                            //wait for answer with time out
                            for (int retry=0; retry < 30; retry++) { //time out 30x5ms = 150ms
                                //if there is serial data
                                if(Serial.available()) {
                                    //get size of available data
                                    size_t len = Serial.available();
                                    uint8_t sbuf[len+1];
                                    //read serial buffer
                                    Serial.readBytes(sbuf, len);
                                    //convert buffer in zero end array
                                    sbuf[len]='\0';
                                    //use string because easier
                                    response = (const char*)sbuf;
                                    LOG("Retry:");
                                    LOG(String(retry));
                                    LOG("\r\n");
                                    LOG(response);
                                    //if buffer contain ok or wait - it means command is pass
                                    if ((response.indexOf("wait")>-1)||(response.indexOf("ok")>-1)) {
                                        success = true;
                                        break;
                                    }
                                    //if buffer contain resend then need to resend
                                    if (response.indexOf("Resend") > -1) { //if error
                                        success = false;
                                        break;
                                    }
                                }
                                delay(5);
                            }
                            //if command is pass no need to retry
                            if (success == true) {
                                break;
                            }
                            //purge extra serial if any
                            if(Serial.available()) {
                                //get size of available data
                                size_t len = Serial.available();
                                uint8_t sbuf[len+1];
                                //read serial buffer
                                Serial.readBytes(sbuf, len);
                                //convert buffer in zero end array
                                sbuf[len]='\0';
                            }
                        }
                        //if even after the number of retry still have error - then we are in error
                        if (!success) {
                            //raise error
                            LOG("Error detected\r\n");
                            LOG(response);
                            com_error = true;
                        }
                        //reset buffer for next command
                        buffer_size = 0;
                        buffer_line[buffer_size] = '\0';
                    }
                } else { //it is a comment
                    if (upload.buf[pos] == '\r') { //store if CR
                        previous = '\r';
                    } else if (upload.buf[pos] == '\n') { //this is the end of the comment
                        is_comment = false;
                        if (buffer_size > 0) {
                            if (previous == '\r') {
                                pos--;
                            }
                            pos--; //do a loop back and process as normal
                        }
                        previous = '\n';
                    }//if not just ignore and continue
                    else {
                        previous = upload.buf[pos];
                    }

                }
            } else { //raise error
                LOG("\r\nlong line detected\r\n");
                LOG(buffer_line);
                com_error = true;
            }
        }
#ifdef DEBUG_PERFORMANCE
        write_time += (millis()-startwrite);
#endif
        //Upload end
        //**************
    } else if(upload.status == UPLOAD_FILE_END) {
        if (buffer_size > 0) { //if last part does not have '\n'
            //print the line
            Serial.print(buffer_line);
            if (is_comment && (previous == '\r')) {
                Serial.print("\r\n");
            } else {
                Serial.print("\n");
            }
            Serial.flush();
            //if resend use buffer
            bool success = false;
            //check NB_RETRY times if get no error when send line
            for (int r = 0 ; r < NB_RETRY ; r++) {
                response = "";
                Serial.print(buffer_line);
                Serial.flush();
                //wait for answer with time out
                for (int retry=0; retry < 20; retry++) { //time out
                    if(Serial.available()) {
                        size_t len = Serial.available();
                        uint8_t sbuf[len+1];
                        Serial.readBytes(sbuf, len);
                        sbuf[len]='\0';
                        response = (const char*)sbuf;
                        if ((response.indexOf("wait")>-1)||(response.indexOf("ok")>-1)) {
                            success = true;
                            break;
                        }
                        if (response.indexOf("Resend") > -1) { //if error
                            success = false;
                            break;
                        }
                    }
                    delay(5);
                }
                if (success == true) {
                    break;
                }
            }
            if (!success) {
                //raise error
                LOG("Error detected 2\r\n");
                LOG(response);
                com_error = true;
            }
            //reset buffer for next command
            buffer_size = 0;
            buffer_line[buffer_size] = '\0';

        }
        LOG("Upload finished ");
        buffer_size=0;
        buffer_line[buffer_size] = '\0';
        //send M29 command to close file on SD
        Serial.print("\r\nM29\r\n");
        Serial.flush();
        web_interface->blockserial = false;
        delay(1000);//give time to FW
        //resend M29 command to close file on SD as first command may be lost
        Serial.print("\r\nM29\r\n");
        Serial.flush();
#ifdef DEBUG_PERFORMANCE
        uint32_t endupload = millis();
        DEBUG_PERF_VARIABLE.add(String(endupload-startupload).c_str());
        DEBUG_PERF_VARIABLE.add(String(write_time).c_str());
        DEBUG_PERF_VARIABLE.add(String(filesize).c_str());
#endif
        if (com_error) {
            web_interface->blockserial = false;
            LOG("with error\r\n");
            web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
            if (!client_closed){
                //web_interface->WebServer.client().stopAll();
                 LOG("Need to stop");
                client_closed = true;
            }   
            filename = "M30 " + filename;
            Serial.println(filename);
            Serial.println("M117 SD upload failed");
            Serial.flush();

        } else {
            LOG("with success\r\n");
            web_interface->_upload_status=UPLOAD_STATUS_SUCCESSFUL;
            Serial.println("M117 SD upload done");
            Serial.flush();
        }
        //Upload cancelled
        //**************
    } else { //UPLOAD_FILE_ABORTED
        LOG("Error, Something happened\r\n");
        com_error = true;
        web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
        buffer_size=0;
        buffer_line[buffer_size] = '\0';
        //send M29 command to close file on SD
        Serial.print("\r\nM29\r\n");
        Serial.flush();
        web_interface->blockserial = false;
        delay(1000);
        //resend M29 command to close file on SD as first command may be lost
        Serial.print("\r\nM29\r\n");
        Serial.flush();
        filename = "M30 " + filename;
        Serial.println(filename);
        Serial.println("M117 SD upload failed");
        Serial.flush();
    }
}


//FW update using Web interface
#ifdef WEB_UPDATE_FEATURE
void WebUpdateUpload()
{
    static size_t last_upload_update;
    static uint32_t maxSketchSpace ;
    //only admin can update FW
    if(web_interface->is_authenticated() != LEVEL_ADMIN) {
        web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
        web_interface->WebServer.client().stopAll();
        Serial.println("M117 Update failed");
        LOG("SD Update failed\r\n");
        return;
    }
    //get current file ID
    HTTPUpload& upload = (web_interface->WebServer).upload();
    //Upload start
    //**************
    if(upload.status == UPLOAD_FILE_START) {
        Serial.println(F("M117 Update Firmware"));
        web_interface->_upload_status= UPLOAD_STATUS_ONGOING;
        WiFiUDP::stopAll();
        maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        last_upload_update = 0;
        if(!Update.begin(maxSketchSpace)) { //start with max available size
            web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
        } else {
        if (( CONFIG::GetFirmwareTarget() == REPETIER4DV) || (CONFIG::GetFirmwareTarget() == REPETIER)) Serial.println(F("M117 Update 0%%"));
        else Serial.println(F("M117 Update 0%"));
        }
        //Upload write
        //**************
    } else if(upload.status == UPLOAD_FILE_WRITE) {
        //check if no error
        if (web_interface->_upload_status == UPLOAD_STATUS_ONGOING) {
            //we do not know the total file size yet but we know the available space so let's use it
            if ( ((100 * upload.totalSize) / maxSketchSpace) !=last_upload_update) {
                last_upload_update = (100 * upload.totalSize) / maxSketchSpace;
                Serial.print(F("M117 Update "));
                Serial.print(last_upload_update);
                if (( CONFIG::GetFirmwareTarget() == REPETIER4DV) || (CONFIG::GetFirmwareTarget() == REPETIER)) Serial.println(F("%%"));
                else Serial.println(F("%"));
            }
            if(Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
            }
        }
        //Upload end
        //**************
    } else if(upload.status == UPLOAD_FILE_END) {
        if(Update.end(true)) { //true to set the size to the current progress
            //Now Reboot
            if (( CONFIG::GetFirmwareTarget() == REPETIER4DV) || (CONFIG::GetFirmwareTarget() == REPETIER)) Serial.println(F("M117 Update 100%%"));
            else Serial.println(F("M117 Update 100%"));
            web_interface->_upload_status=UPLOAD_STATUS_SUCCESSFUL;
        }
    } else if(upload.status == UPLOAD_FILE_ABORTED) {
        Serial.println(F("M117 Update Failed"));
        Update.end();
        web_interface->_upload_status=UPLOAD_STATUS_CANCELLED;
    }
    delay(0);
}

void handleUpdate()
{
    level_authenticate_type auth_level = web_interface->is_authenticated();
    if (auth_level != LEVEL_ADMIN) {
		web_interface->_upload_status=UPLOAD_STATUS_NONE;
        web_interface->WebServer.send(403,"text/plain","Not allowed, log in first!\n");
        return;
    }
    String jsonfile = "{\"status\":\"" ;
    jsonfile+=CONFIG::intTostr(web_interface->_upload_status);
    jsonfile+="\"}";
    //send status
    web_interface->WebServer.sendHeader("Cache-Control", "no-cache");
    web_interface->WebServer.send(200, "application/json", jsonfile);
    //if success restart
    if (web_interface->_upload_status==UPLOAD_STATUS_SUCCESSFUL) {
        web_interface->restartmodule=true;
    } else {
        web_interface->_upload_status=UPLOAD_STATUS_NONE;
    }
}
#endif

//SPIFFS files list and file commands
void handleFileList()
{
    level_authenticate_type auth_level = web_interface->is_authenticated();
    if (auth_level == LEVEL_GUEST) {
        web_interface->_upload_status=UPLOAD_STATUS_NONE;
        web_interface->WebServer.send(401,"text/plain","Authentication failed!\n");
        return;
    }
    String path ;
    String status = "Ok";
    if ((web_interface->_upload_status == UPLOAD_STATUS_FAILED) || (web_interface->_upload_status == UPLOAD_STATUS_CANCELLED)) {
        status = "Upload failed";
    }
    //be sure root is correct according authentication
    if (auth_level == LEVEL_ADMIN) {
        path = "/";
    } else {
        path = "/user";
    }
    //get current path
    if(web_interface->WebServer.hasArg("path")) {
        path += web_interface->WebServer.arg("path") ;
    }
    //to have a clean path
    path.trim();
    path.replace("//","/");
    if (path[path.length()-1] !='/') {
        path +="/";
    }
    //check if query need some action
    if(web_interface->WebServer.hasArg("action")) {
        //delete a file
        if(web_interface->WebServer.arg("action") == "delete" && web_interface->WebServer.hasArg("filename")) {
            String filename;
            String shortname = web_interface->WebServer.arg("filename");
            shortname.replace("/","");
            filename = path + web_interface->WebServer.arg("filename");
            filename.replace("//","/");
            if(!SPIFFS.exists(filename)) {
                status = shortname + F(" does not exists!");
            } else {
                if (SPIFFS.remove(filename)) {
                    status = shortname + F(" deleted");
                    //what happen if no "/." and no other subfiles ?
                    fs::Dir dir = SPIFFS.openDir(path);
                    if (!dir.next()) {
                        //keep directory alive even empty
                         fs::File r = SPIFFS.open(path+"/.","w");
                        if (r) {
                            r.close();
                        }
                    }
                } else {
                    status = F("Cannot deleted ") ;
                    status+=shortname ;
                }
            }
        }
        //delete a directory
        if(web_interface->WebServer.arg("action") == "deletedir" && web_interface->WebServer.hasArg("filename")) {
            String filename;
            String shortname = web_interface->WebServer.arg("filename");
            shortname.replace("/","");
            filename = path + web_interface->WebServer.arg("filename");
            filename += "/.";
            filename.replace("//","/");
            if (filename != "/") {
                bool delete_error = false;
                fs::Dir dir = SPIFFS.openDir(path + shortname);
                {
                    while (dir.next()) {
                        String fullpath = dir.fileName();
                        if (!SPIFFS.remove(dir.fileName())) {
                            delete_error = true;
                            status = F("Cannot deleted ") ;
                            status+=fullpath;
                        }
                    }
                }
                if (!delete_error) {
                    status = shortname ;
                    status+=" deleted";
                }
            }
        }
        //create a directory
        if(web_interface->WebServer.arg("action")=="createdir" && web_interface->WebServer.hasArg("filename")) {
            String filename;
            filename = path + web_interface->WebServer.arg("filename") +"/.";
            String shortname = web_interface->WebServer.arg("filename");
            shortname.replace("/","");
            filename.replace("//","/");
            if(SPIFFS.exists(filename)) {
                status = shortname + F(" already exists!");
            } else {
                 fs::File r = SPIFFS.open(filename,"w");
                if (!r) {
                    status = F("Cannot create ");
                    status += shortname ;
                } else {
                    r.close();
                    status = shortname + F(" created");
                }
            }
        }
    }
    String jsonfile = "{";
    fs::Dir dir = SPIFFS.openDir(path);
    jsonfile+="\"files\":[";
    bool firstentry=true;
    String subdirlist="";
    while (dir.next()) {
        String filename = dir.fileName();
        String size ="";
        bool addtolist=true;
        //remove path from name
        filename = filename.substring(path.length(),filename.length());
        //check if file or subfile
        if (filename.indexOf("/")>-1) {
            //Do not rely on "/." to define directory as SPIFFS upload won't create it but directly files
            //and no need to overload SPIFFS if not necessary to create "/." if no need
            //it will reduce SPIFFS available space so limit it to creation
            filename = filename.substring(0,filename.indexOf("/"));
            String tag="*";
            tag = filename + "*";
            if (subdirlist.indexOf(tag)>-1) { //already in list
                addtolist = false; //no need to add
            } else {
                size = -1; //it is subfile so display only directory, size will be -1 to describe it is directory
                if (subdirlist.length()==0) {
                    subdirlist+="*";
                }
                subdirlist += filename + "*"; //add to list
            }
        } else {
            //do not add "." file
            if (filename!=".") {
                 fs::File f = dir.openFile("r");
                size = CONFIG::formatBytes(f.size());
                f.close();
            } else {
                addtolist = false;
            }
        }
        if(addtolist) {
            if (!firstentry) {
                jsonfile+=",";
            } else {
                firstentry=false;
            }
            jsonfile+="{";
            jsonfile+="\"name\":\"";
            jsonfile+=filename;
            jsonfile+="\",\"size\":\"";
            jsonfile+=size;
            jsonfile+="\"";
            jsonfile+="}";
        }
    }
    jsonfile+="],";
    jsonfile+="\"path\":\"" + path + "\",";
    jsonfile+="\"status\":\"" + status + "\",";
    fs::FSInfo info;
    SPIFFS.info(info);
    jsonfile+="\"total\":\"" + CONFIG::formatBytes(info.totalBytes) + "\",";
    jsonfile+="\"used\":\"" + CONFIG::formatBytes(info.usedBytes) + "\",";
    jsonfile.concat(F("\"occupation\":\""));
    jsonfile+= CONFIG::intTostr(100*info.usedBytes/info.totalBytes);
    jsonfile+="\"";
    jsonfile+="}";
    path = "";
    web_interface->WebServer.sendHeader("Cache-Control", "no-cache");
    web_interface->WebServer.send(200, "application/json", jsonfile);
    web_interface->_upload_status=UPLOAD_STATUS_NONE;
}

//serial SD files list
void handle_serial_SDFileList()
{
    //this is only for admin an user
    if (web_interface->is_authenticated() == LEVEL_GUEST) {
        web_interface->_upload_status=UPLOAD_STATUS_NONE;
        web_interface->WebServer.sendHeader("Cache-Control", "no-cache");
        web_interface->WebServer.send(401, "application/json", "{\"status\":\"Authentication failed!\"}");
        return;
    }
    LOG("serial SD upload done\r\n")
    String sstatus="Ok";
    if ((web_interface->_upload_status == UPLOAD_STATUS_FAILED) || (web_interface->_upload_status == UPLOAD_STATUS_CANCELLED)) {
        sstatus = "Upload failed";
        web_interface->_upload_status = UPLOAD_STATUS_NONE;
    }
    String jsonfile = "{\"status\":\"" + sstatus + "\"}";
    web_interface->WebServer.sendHeader("Cache-Control", "no-cache");
    web_interface->WebServer.send(200, "application/json", jsonfile);
    web_interface->blockserial = false;
    web_interface->_upload_status=UPLOAD_STATUS_NONE;
}


//do a redirect to avoid to many query
//and handle not registred path
void handle_not_found()
{
    static const char NOT_AUTH_NF [] PROGMEM = "HTTP/1.1 301 OK\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";

    if (web_interface->is_authenticated() == LEVEL_GUEST) {
        web_interface->WebServer.sendContent_P(NOT_AUTH_NF);
        //web_interface->WebServer.client().stop();
        return;
    }
    bool page_not_found = false;
    String path = web_interface->WebServer.urlDecode(web_interface->WebServer.uri());
    String contentType =  web_interface->getContentType(path);
    String pathWithGz = path + ".gz";
    LOG("request:")
    LOG(path)
    LOG("\r\n")
#ifdef DEBUG_ESP3D
    int nb = web_interface->WebServer.args();
    for (int i = 0 ; i < nb; i++) {
        LOG(web_interface->WebServer.argName(i))
        LOG(":")
        LOG(web_interface->WebServer.arg(i))
        LOG("\r\n")

    }
#endif
    LOG("type:")
    LOG(contentType)
    LOG("\r\n")
        if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
            if(SPIFFS.exists(pathWithGz)) {
                path = pathWithGz;
            }
            fs::File file = SPIFFS.open(path, "r");
            web_interface->WebServer.streamFile(file, contentType);
            file.close();
            return;
        } else {
            page_not_found = true;
        }

    if (page_not_found ) {
#ifdef CAPTIVE_PORTAL_FEATURE
        if (WiFi.getMode()!=WIFI_STA ) {
            String contentType=FPSTR(PAGE_CAPTIVE);
            String stmp = WiFi.softAPIP().toString();
            //Web address = ip + port
            String KEY_IP = F("$WEB_ADDRESS$");
            String KEY_QUERY = F("$QUERY$");
            if (wifi_config.iweb_port!=80) {
                stmp+=":";
                stmp+=CONFIG::intTostr(wifi_config.iweb_port);
            }
            contentType.replace(KEY_IP,stmp);
             contentType.replace(KEY_IP,stmp);
            contentType.replace(KEY_QUERY,web_interface->WebServer.uri());
            web_interface->WebServer.send(200,"text/html",contentType);
            //web_interface->WebServer.sendContent_P(NOT_AUTH_NF);
            //web_interface->WebServer.client().stop();
            return;
        }
#endif
        LOG("Page not found\r\n")
        path = F("/404.htm");
        contentType =  web_interface->getContentType(path);
        pathWithGz =  path + F(".gz");
        if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
            if(SPIFFS.exists(pathWithGz)) {
                path = pathWithGz;
            }
             fs::File file = SPIFFS.open(path, "r");
            web_interface->WebServer.streamFile(file, contentType);
            file.close();
           
        } else {
            //if not template use default page
            contentType=FPSTR(PAGE_404);
            String stmp;
            if (WiFi.getMode()==WIFI_STA ) {
                stmp=WiFi.localIP().toString();
            } else {
                stmp=WiFi.softAPIP().toString();
            }
            //Web address = ip + port
            String KEY_IP = F("$WEB_ADDRESS$");
            String KEY_QUERY = F("$QUERY$");
            if (wifi_config.iweb_port!=80) {
                stmp+=":";
                stmp+=CONFIG::intTostr(wifi_config.iweb_port);
            }
            contentType.replace(KEY_IP,stmp);
            contentType.replace(KEY_QUERY,web_interface->WebServer.uri());
            web_interface->WebServer.send(200,"text/html",contentType);
        }
    }
}

#ifdef AUTHENTICATION_FEATURE
void handle_login()
{
    String smsg;
    String sUser,sPassword;
    String auths;
    int code = 200;
    bool msg_alert_error=false;
    //disconnect can be done anytime no need to check credential
    if (web_interface->WebServer.hasArg("DISCONNECT")) {
        String cookie = web_interface->WebServer.header("Cookie");
        int pos = cookie.indexOf("ESPSESSIONID=");
        String sessionID;
        if (pos!= -1) {
            int pos2 = cookie.indexOf(";",pos);
            sessionID = cookie.substring(pos+strlen("ESPSESSIONID="),pos2);
            }
        web_interface->ClearAuthIP(web_interface->WebServer.client().remoteIP(), sessionID.c_str());
        web_interface->WebServer.sendHeader("Set-Cookie","ESPSESSIONID=0");
        web_interface->WebServer.sendHeader("Cache-Control","no-cache");
        String buffer2send = "{\"status\":\"Ok\",\"authentication_lvl\":\"guest\"}";
        web_interface->WebServer.send(code, "application/json", buffer2send);
        //web_interface->WebServer.client().stop();
        return;
    }
    
    level_authenticate_type auth_level= web_interface->is_authenticated();
   if (auth_level == LEVEL_GUEST) auths = F("guest");
    else if (auth_level == LEVEL_USER) auths = F("user");
    else if (auth_level == LEVEL_ADMIN) auths = F("admin");
    else auths = F("???");
        
    //check is it is a submission or a query
    if (web_interface->WebServer.hasArg("SUBMIT")) {
        //is there a correct list of query?
        if ( web_interface->WebServer.hasArg("PASSWORD")&& web_interface->WebServer.hasArg("USER")) {
            //USER
            sUser = web_interface->WebServer.arg("USER");
            if ( !((sUser==FPSTR(DEFAULT_ADMIN_LOGIN)) || (sUser==FPSTR(DEFAULT_USER_LOGIN)))) {
                msg_alert_error=true;
                smsg=F("Error : Incorrect User");
                code=401;
            }
            if (msg_alert_error == false) {
                //Password
                sPassword = web_interface->WebServer.arg("PASSWORD");
                String sadminPassword;

                if (!CONFIG::read_string(EP_ADMIN_PWD, sadminPassword, MAX_LOCAL_PASSWORD_LENGTH)) {
                    sadminPassword=FPSTR(DEFAULT_ADMIN_PWD);
                }

                String suserPassword;

                if (!CONFIG::read_string(EP_USER_PWD, suserPassword, MAX_LOCAL_PASSWORD_LENGTH)) {
                    suserPassword=FPSTR(DEFAULT_USER_PWD);
                }

                if(!(((sUser==FPSTR(DEFAULT_ADMIN_LOGIN)) && (strcmp(sPassword.c_str(),sadminPassword.c_str())==0)) ||
                        ((sUser==FPSTR(DEFAULT_USER_LOGIN)) && (strcmp(sPassword.c_str(),suserPassword.c_str()) == 0)))) {
                    msg_alert_error=true;
                    smsg=F("Error: Incorrect password");
                    code = 401;
                }
            }
        } else {
            msg_alert_error=true;
            smsg = F("Error: Missing data");
            code = 500;
        }
        //change password
        if ( web_interface->WebServer.hasArg("PASSWORD")&& web_interface->WebServer.hasArg("USER") && web_interface->WebServer.hasArg("NEWPASSWORD") && (msg_alert_error==false) ) {
            String newpassword =  web_interface->WebServer.arg("NEWPASSWORD");
            if (CONFIG::isLocalPasswordValid(newpassword.c_str())) {
                int pos=0;
                if(sUser==FPSTR(DEFAULT_ADMIN_LOGIN)) pos = EP_ADMIN_PWD;
                else pos = EP_USER_PWD;
                if (!CONFIG::write_string(pos,newpassword.c_str())){
                     msg_alert_error=true;
                     smsg = F("Error: Cannot apply changes");
                     code = 500;
                }
            } else {
                msg_alert_error=true;
                smsg = F("Error: Incorrect password");
                code = 500;
            }
        }
   if ((code == 200) || (code == 500)) {
       level_authenticate_type current_auth_level;
      if(sUser == FPSTR(DEFAULT_ADMIN_LOGIN)) {
            current_auth_level = LEVEL_ADMIN;
        } else  if(sUser == FPSTR(DEFAULT_USER_LOGIN)){
            current_auth_level = LEVEL_USER;
        } else {
            current_auth_level = LEVEL_GUEST;
        }
        //create Session
        if ((current_auth_level != auth_level) || (auth_level== LEVEL_GUEST)) {
            auth_ip * current_auth = new auth_ip;
            current_auth->level = current_auth_level;
            current_auth->ip=web_interface->WebServer.client().remoteIP();
            strcpy(current_auth->sessionID,web_interface->create_session_ID());
            strcpy(current_auth->userID,sUser.c_str());
            current_auth->last_time=millis();
            if (web_interface->AddAuthIP(current_auth)) {
                String tmps ="ESPSESSIONID="; 
                tmps+=current_auth->sessionID;
                web_interface->WebServer.sendHeader("Set-Cookie",tmps);
                web_interface->WebServer.sendHeader("Cache-Control","no-cache");
                switch(current_auth->level) {
                    case LEVEL_ADMIN:
                        auths = "admin";
                        break;
                     case LEVEL_USER:
                        auths = "user";
                        break;
                    default:
                        auths = "guest";
                    }
            } else {
                delete current_auth;
                msg_alert_error=true;
                code = 500;
                smsg = F("Error: Too many connections");
            }
        }
   } 
    if (code == 200) smsg = F("Ok");
    
    //build  JSON
    String buffer2send = "{\"status\":\"" + smsg + "\",\"authentication_lvl\":\"";
    buffer2send += auths;
    buffer2send += "\"}";
    web_interface->WebServer.send(code, "application/json", buffer2send);
    } else {
    if (auth_level != LEVEL_GUEST) {
        String cookie = web_interface->WebServer.header("Cookie");
        int pos = cookie.indexOf("ESPSESSIONID=");
        String sessionID;
        if (pos!= -1) {
            int pos2 = cookie.indexOf(";",pos);
            sessionID = cookie.substring(pos+strlen("ESPSESSIONID="),pos2);
            auth_ip * current_auth_info = web_interface->GetAuth(web_interface->WebServer.client().remoteIP(), sessionID.c_str());
            if (current_auth_info != NULL){
                    sUser = current_auth_info->userID;
                }
        }
    }
    String buffer2send = "{\"status\":\"200\",\"authentication_lvl\":\"";
    buffer2send += auths;
    buffer2send += "\",\"user\":\"";
    buffer2send += sUser;
    buffer2send +="\"}";
    web_interface->WebServer.send(code, "application/json", buffer2send);
    }
}
#endif


//Handle web command query and send answer
void handle_web_command()
{
    level_authenticate_type auth_level= web_interface->is_authenticated();
  /*  if (auth_level == LEVEL_GUEST) {
        web_interface->WebServer.send(403,"text/plain","Not allowed, log in first!\n");
        return;
    }*/
    String buffer2send = "";
    LOG(String (web_interface->WebServer.args()))
    LOG(" Web command\r\n")
#ifdef DEBUG_ESP3D
    int nb = web_interface->WebServer.args();
    for (int i = 0 ; i < nb; i++) {
        LOG(web_interface->WebServer.argName(i))
        LOG(":")
        LOG(web_interface->WebServer.arg(i))
        LOG("\r\n")
    }
#endif
    String cmd = "";
    int count ;
    if (web_interface->WebServer.hasArg("plain") || web_interface->WebServer.hasArg("commandText")) {
        if (web_interface->WebServer.hasArg("plain")) {
            cmd = web_interface->WebServer.arg("plain");
        } else {
            cmd = web_interface->WebServer.arg("commandText");
        }
        LOG("Web Command:")
        LOG(cmd)
        LOG("\r\n")
    } else {
        LOG("invalid argument\r\n")
        web_interface->WebServer.send(200,"text/plain","Invalid command");
        return;
    }
    //if it is for ESP module [ESPXXX]<parameter>
    cmd.trim();
    int ESPpos = cmd.indexOf("[ESP");
    if (ESPpos>-1) {
        //is there the second part?
        int ESPpos2 = cmd.indexOf("]",ESPpos);
        if (ESPpos2>-1) {
            //Split in command and parameters
            String cmd_part1=cmd.substring(ESPpos+4,ESPpos2);
            String cmd_part2="";
            //only [ESP800] is allowed login free if authentication is enabled
             if ((auth_level == LEVEL_GUEST)  && (cmd_part1.toInt()!=800)) {
                web_interface->WebServer.send(401,"text/plain","Authentication failed!\n");
                return;
            }
            //is there space for parameters?
            if (ESPpos2<cmd.length()) {
                cmd_part2=cmd.substring(ESPpos2+1);
            }
            //if command is a valid number then execute command
            if(cmd_part1.toInt()!=0) {
                COMMAND::execute_command(cmd_part1.toInt(), cmd_part2, WEB_PIPE, auth_level);
                BRIDGE::flush(WEB_PIPE);
            }
            //if not is not a valid [ESPXXX] command
        }
    } else {
         if (auth_level == LEVEL_GUEST) {
        web_interface->WebServer.send(401,"text/plain","Authentication failed!\n");
        return;
    }
        //send command to serial as no need to transfer ESP command
        //to avoid any pollution if Uploading file to SDCard
        if ((web_interface->blockserial) == false) {
            //block every query
            web_interface->blockserial = true;
            LOG("Block Serial\r\n")
            //empty the serial buffer and incoming data
            LOG("Start PurgeSerial\r\n")
            if(Serial.available()) {
                BRIDGE::processFromSerial2TCP();
                delay(1);
            }
            LOG("End PurgeSerial\r\n")
            web_interface->WebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
            web_interface->WebServer.sendHeader("Content-Type","text/plain",true);
            web_interface->WebServer.sendHeader("Cache-Control","no-cache");
            web_interface->WebServer.send(200);
            //send command
            LOG(String(cmd.length()))
            LOG("Start PurgeSerial\r\n")
            if(Serial.available()) {
                BRIDGE::processFromSerial2TCP();
                delay(1);
            }
            LOG("End PurgeSerial\r\n")
            LOG("Send Command\r\n")
            Serial.println(cmd);
            count = 0;
            String current_buffer;
            String current_line;
            int pos;
            int temp_counter = 0;
            String tmp;
            bool datasent = false;
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
                            LOG("Found ok\r\n")
                            break;
                        }
                        //get the line and transmit it
                        LOG("Check command: ")
                        LOG(current_line)
                        LOG("\r\n")
                        //check command
                        if ((CONFIG::GetFirmwareTarget() == REPETIER) || (CONFIG::GetFirmwareTarget() == REPETIER4DV)){
                            //save time no need to continue
                            if (current_line.indexOf("busy:") > -1) {
                                temp_counter++;
                            } else if (COMMAND::check_command(current_line, NO_PIPE, false)) {
                                    temp_counter ++ ;
                                }
                        }else {
                            if (COMMAND::check_command(current_line, NO_PIPE, false)) {
                                temp_counter ++ ;
                            }
                        }
                        if (temp_counter > 5) {
                            break;
                        }
                        if ((CONFIG::GetFirmwareTarget()  == REPETIER) || (CONFIG::GetFirmwareTarget() == REPETIER4DV)) {
                            if (!current_line.startsWith( "ok "))
                                {
                                    buffer2send +=current_line;
                                    buffer2send +="\n";
                                }
                        } else {
                            buffer2send +=current_line;
                            buffer2send +="\n";
                        }
                        if (buffer2send.length() > 1200) {
                            web_interface->WebServer.sendContent(buffer2send);
                            buffer2send = "";
                            datasent = true;
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
            //to be sure connection close
            if (buffer2send.length() > 0) {
                web_interface->WebServer.sendContent(buffer2send);
                datasent = true;
            }
            if (!datasent) {
                web_interface->WebServer.sendContent(" \r\n");
            }
            web_interface->WebServer.sendContent("");
            LOG("Start PurgeSerial\r\n")
            if(Serial.available()) {
                BRIDGE::processFromSerial2TCP();
                delay(1);
            }
            LOG("End PurgeSerial\r\n")
            web_interface->blockserial = false;
            LOG("Release Serial\r\n")
        } else {
            web_interface->WebServer.send(200,"text/plain","Serial is busy, retry later!");
        }
    }
}

//Handle web command query and sent ack or fail instead of answer
void handle_web_command_silent()
{
    level_authenticate_type auth_level= web_interface->is_authenticated();
    if (auth_level == LEVEL_GUEST) {
        web_interface->WebServer.send(401,"text/plain","Authentication failed!\n");
        return;
    }
    String buffer2send = "";
    LOG(String (web_interface->WebServer.args()))
    LOG(" Web silent command\r\n")
#ifdef DEBUG_ESP3D
    int nb = web_interface->WebServer.args();
    for (int i = 0 ; i < nb; i++) {
        LOG(web_interface->WebServer.argName(i))
        LOG(":")
        LOG(web_interface->WebServer.arg(i))
        LOG("\r\n")
    }
#endif
    String cmd = "";
    int count ;
    if (web_interface->WebServer.hasArg("plain") || web_interface->WebServer.hasArg("commandText")) {
        if (web_interface->WebServer.hasArg("plain")) {
            cmd = web_interface->WebServer.arg("plain");
        } else {
            cmd = web_interface->WebServer.arg("commandText");
        }
        LOG("Web Command:")
        LOG(cmd)
        LOG("\r\n")
    } else {
        LOG("invalid argument\r\n")
        web_interface->WebServer.send(200,"text/plain","Invalid command");
        return;
    }
    //if it is for ESP module [ESPXXX]<parameter>
    cmd.trim();
    int ESPpos = cmd.indexOf("[ESP");
    if (ESPpos>-1) {
        //is there the second part?
        int ESPpos2 = cmd.indexOf("]",ESPpos);
        if (ESPpos2>-1) {
            //Split in command and parameters
            String cmd_part1=cmd.substring(ESPpos+4,ESPpos2);
            String cmd_part2="";
            //is there space for parameters?
            if (ESPpos2<cmd.length()) {
                cmd_part2=cmd.substring(ESPpos2+1);
            }
            //if command is a valid number then execute command
            if(cmd_part1.toInt()!=0) {
                if (COMMAND::execute_command(cmd_part1.toInt(),cmd_part2,NO_PIPE, auth_level)) {
                    web_interface->WebServer.send(200,"text/plain","ok");
                } else {
                    web_interface->WebServer.send(500,"text/plain","error");
                }

            }
            //if not is not a valid [ESPXXX] command
        }
    } else {
        //send command to serial as no need to transfer ESP command
        //to avoid any pollution if Uploading file to SDCard
        if ((web_interface->blockserial) == false) {
            LOG("Send Command\r\n")
            //send command
            Serial.println(cmd);
            web_interface->WebServer.send(200,"text/plain","ok");
        } else {
            web_interface->WebServer.send(200,"text/plain","Serial is busy, retry later!");
        }
    }

}

#ifdef SSDP_FEATURE
void handle_SSDP()
{
    SSDP.schema(web_interface->WebServer.client());
}
#endif

//constructor
WEBINTERFACE_CLASS::WEBINTERFACE_CLASS (int port):WebServer(port)
{
    //init what will handle "/"
    WebServer.on("/",HTTP_ANY, handle_web_interface_root);
    WebServer.on("/command",HTTP_ANY, handle_web_command);
    WebServer.on("/command_silent",HTTP_ANY, handle_web_command_silent);
    WebServer.on("/upload_serial", HTTP_ANY, handle_serial_SDFileList,SDFile_serial_upload);
    WebServer.on("/files", HTTP_ANY, handleFileList,SPIFFSFileupload);
#ifdef WEB_UPDATE_FEATURE
    WebServer.on("/updatefw",HTTP_ANY, handleUpdate,WebUpdateUpload);
#endif
#ifdef AUTHENTICATION_FEATURE
    WebServer.on("/login", HTTP_ANY, handle_login);
#endif
    //TODO: to be reviewed
    WebServer.on("/STATUS",HTTP_ANY, handle_web_interface_status);
#ifdef SSDP_FEATURE
    WebServer.on("/description.xml", HTTP_GET, handle_SSDP);
#endif
#ifdef CAPTIVE_PORTAL_FEATURE
    WebServer.on("/generate_204",HTTP_ANY, handle_web_interface_root);
    WebServer.on("/gconnectivitycheck.gstatic.com",HTTP_ANY, handle_web_interface_root);
    WebServer.on("/fwlink",HTTP_ANY, handle_web_interface_root);
#endif
    WebServer.onNotFound( handle_not_found);
    blockserial = false;
    restartmodule=false;
    //rolling list of 4entries with a maximum of 50 char for each entry
#ifdef ERROR_MSG_FEATURE
    error_msg.setsize(4);
    error_msg.setlength(50);
#endif
#ifdef INFO_MSG_FEATURE
    info_msg.setsize(4);
    info_msg.setlength(50);
#endif
#ifdef STATUS_MSG_FEATURE
    status_msg.setsize(4);
    status_msg.setlength(50);
#endif
    fsUploadFile=( fs::File)0;
    _head=NULL;
    _nb_ip=0;
    _upload_status=UPLOAD_STATUS_NONE;
}
//Destructor
WEBINTERFACE_CLASS::~WEBINTERFACE_CLASS()
{
#ifdef INFO_MSG_FEATURE
    info_msg.clear();
#endif
#ifdef ERROR_MSG_FEATURE
    error_msg.clear();
#endif
#ifdef STATUS_MSG_FEATURE
    status_msg.clear();
#endif
    while (_head) {
        auth_ip * current = _head;
        _head=_head->_next;
        delete current;
    }
    _nb_ip=0;
}
//check authentification
level_authenticate_type  WEBINTERFACE_CLASS::is_authenticated()
{
#ifdef AUTHENTICATION_FEATURE
    if (WebServer.hasHeader("Cookie")) {
        String cookie = WebServer.header("Cookie");
        int pos = cookie.indexOf("ESPSESSIONID=");
        if (pos!= -1) {
            int pos2 = cookie.indexOf(";",pos);
            String sessionID = cookie.substring(pos+strlen("ESPSESSIONID="),pos2);
            IPAddress ip = WebServer.client().remoteIP();
            //check if cookie can be reset and clean table in same time
            return ResetAuthIP(ip,sessionID.c_str());
        }
    }
    return LEVEL_GUEST;
#else
    return LEVEL_ADMIN;
#endif
}

#ifdef AUTHENTICATION_FEATURE
//add the information in the linked list if possible
bool WEBINTERFACE_CLASS::AddAuthIP(auth_ip * item)
{
    if (_nb_ip>MAX_AUTH_IP) {
        return false;
    }
    item->_next=_head;
    _head=item;
    _nb_ip++;
    return true;
}

//Session ID based on IP and time using 16 char
char * WEBINTERFACE_CLASS::create_session_ID()
{
    static char  sessionID[17];
//reset SESSIONID
    for (int i=0; i<17; i++) {
        sessionID[i]='\0';
    }
//get time
    uint32_t now = millis();
//get remote IP
    IPAddress remoteIP=WebServer.client().remoteIP();
//generate SESSIONID
    if (0>sprintf(sessionID,"%02X%02X%02X%02X%02X%02X%02X%02X",remoteIP[0],remoteIP[1],remoteIP[2],remoteIP[3],(uint8_t) ((now >> 0) & 0xff),(uint8_t) ((now >> 8) & 0xff),(uint8_t) ((now >> 16) & 0xff),(uint8_t) ((now >> 24) & 0xff))) {
        strcpy(sessionID,"NONE");
    }
    return sessionID;
}



bool WEBINTERFACE_CLASS::ClearAuthIP(IPAddress ip, const char * sessionID){
    auth_ip * current = _head;
    auth_ip * previous = NULL;
    bool done = false;
    while (current) {
        if ((ip == current->ip) && (strcmp(sessionID,current->sessionID)==0)) {
            //remove
            done = true;
            if (current ==_head) {
                _head=current->_next;
                _nb_ip--;
                delete current;
                current=_head;
            } else {
                previous->_next=current->_next;
                _nb_ip--;
                delete current;
                current=previous->_next;
            }
        } else {
            previous = current;
            current=current->_next;
        }
    }
    return done;
}

//Get info
auth_ip * WEBINTERFACE_CLASS::GetAuth(IPAddress ip,const char * sessionID)
{
    auth_ip * current = _head;
    auth_ip * previous = NULL;
    //get time
    //uint32_t now = millis();
    while (current) {
        if (ip==current->ip) {
            if (strcmp(sessionID,current->sessionID)==0) {
                //found
                return current;
            }
        }
        previous = current;
        current=current->_next;
    }
    return NULL;
}

//Review all IP to reset timers
level_authenticate_type WEBINTERFACE_CLASS::ResetAuthIP(IPAddress ip,const char * sessionID)
{
    auth_ip * current = _head;
    auth_ip * previous = NULL;
    //get time
    //uint32_t now = millis();
    while (current) {
        if ((millis()-current->last_time)>180000) {
            //remove
            if (current==_head) {
                _head=current->_next;
                _nb_ip--;
                delete current;
                current=_head;
            } else {
                previous->_next=current->_next;
                _nb_ip--;
                delete current;
                current=previous->_next;
            }
        } else {
            if (ip==current->ip) {
                if (strcmp(sessionID,current->sessionID)==0) {
                    //reset time
                    current->last_time=millis();
                    return (level_authenticate_type)current->level;
                }
            }
            previous = current;
            current=current->_next;
        }
    }
    return LEVEL_GUEST;
}
#endif

//Check what is the content tye according extension file
String WEBINTERFACE_CLASS::getContentType(String filename)
{
    if(filename.endsWith(".htm")) {
        return "text/html";
    } else if(filename.endsWith(".html")) {
        return "text/html";
    } else if(filename.endsWith(".css")) {
        return "text/css";
    } else if(filename.endsWith(".js")) {
        return "application/javascript";
    } else if(filename.endsWith(".png")) {
        return "image/png";
    } else if(filename.endsWith(".gif")) {
        return "image/gif";
    } else if(filename.endsWith(".jpeg")) {
        return "image/jpeg";
    } else if(filename.endsWith(".jpg")) {
        return "image/jpeg";
    } else if(filename.endsWith(".ico")) {
        return "image/x-icon";
    } else if(filename.endsWith(".xml")) {
        return "text/xml";
    } else if(filename.endsWith(".pdf")) {
        return "application/x-pdf";
    } else if(filename.endsWith(".zip")) {
        return "application/x-zip";
    } else if(filename.endsWith(".gz")) {
        return "application/x-gzip";
    } else if(filename.endsWith(".tpl")) {
        return "text/plain";
    } else if(filename.endsWith(".inc")) {
        return "text/plain";
    } else if(filename.endsWith(".txt")) {
        return "text/plain";
    }
    return "application/octet-stream";
}


WEBINTERFACE_CLASS * web_interface;

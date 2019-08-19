/*
 * Project 2Wireless
 * Description:
 * Author:
 * Date:
 */

#pragma SPARK_NO_PREPROCESSOR


#include "Particle.h"
#include "Arduino.h"
#include "softap_http.h"
#include "Wire.h"


SYSTEM_THREAD(ENABLED);

#define CARD_ADDRESS 0x00
#define MAX_DATA_SIZE 8000 //max number of i2c bytes permitted

volatile int bytes_read = 0; //i2c bytes received
volatile uint8_t data[MAX_DATA_SIZE]; //i2c data received

Writer* response;

void receiveEvent(int howMany) {
  while (1 < Wire.available()) { // loop through all but the last
    data[bytes_read] = Wire.read(); // receive byte as a character
    //Serial.print(c);         // print the character
    bytes_read++;
  }
  data[bytes_read] = Wire.read();    // receive byte as an integer
  //Serial.println(x);         // print the integer
  bytes_read++;
}

void receiveEventNew(int howMany) {
    String tmp;
    if (response != nullptr){
        //response->write(":");
    }
    
    while (1 < Wire.available()) { // loop through all but the last
        tmp = String(Wire.read(),HEX);
        if (tmp.length() == 1) tmp = "0" + tmp;
        if (response != nullptr) {
           // response->write(tmp);
        }
        
        if (((bytes_read % 8) == 0) && (bytes_read != 0)){
            if (response != nullptr){
                //response->write("\r\n");
            }
           
        }
        bytes_read++;
    }
    tmp = String(Wire.read(),HEX);
    if (tmp.length() == 1) tmp = "0" + tmp;
    if (response != nullptr){
        //response->write(tmp);
    }
    
    bytes_read++;
}

void switchToMaster(){
    Wire.end();
    Wire.begin();
}

void switchToSlave(){
    Wire.end();
    Wire.begin(0x50 | CARD_ADDRESS);
    Wire.onReceive(receiveEvent);
}

void sendRemoteEnable() {
    //Remote Enable
    Wire.beginTransmission(0);
    Wire.write(0x04);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x16);
    Wire.write(0xFF);
    Wire.endTransmission();
}

void sendRemoteDisable() {
    //Remote Disable
    Wire.beginTransmission(0);
    Wire.write(0x04);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x17);
    Wire.write(0xFF);
    Wire.endTransmission();

}

void getPresetData(int address) {
    //Fetch presets from specified module
    Wire.beginTransmission(0);
    Wire.write(0x07);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x04);
    Wire.write(address); //Module address
    Wire.write(CARD_ADDRESS); //Card address lower byte. Upper byte is always 0x50. 
    Wire.write(0x00); //Card memory address LSB
    Wire.write(0x00); //Card memory address MSB
    Wire.endTransmission();
}

int r, g, b = 0;

struct Page
{
    const char* url;
    const char* mime_type;
    const char* data;
};


const char index_html[] = "<html><div align=\"center\"><form action=\"color\" method=\"get\"><input id=\"background-color\" name=\"color\" type=\"color\"/><input type=\"submit\" value=\"Go!\"/></form></div></html>";

Page myPages[] = {
     { "/index.html", "text/html", index_html },
     { nullptr }
};

void myPage(const char* url, ResponseCallback* cb, void* cbArg, Reader* body, Writer* result, void* reserved)
{
    String urlString = String(url);
    Serial.printlnf("handling page %s", url);
    char* data = body->fetch_as_string();
    Serial.println(String(data));
    free(data);

    if (strcmp(url,"/index")==0) {
        Serial.println("sending redirect");
        Header h("Location: /index.html\r\n");
        cb(cbArg, 0, 301, "text/plain", &h);
        return;
    }
    if (urlString.indexOf("/color") != -1) {
        r = (int)strtol(urlString.substring(14, 16).c_str(), nullptr, 16);
        g = (int)strtol(urlString.substring(16, 18).c_str(), nullptr, 16);
        b = (int)strtol(urlString.substring(18).c_str(), nullptr, 16);
        Serial.println(r);
        Serial.println(g);
        Serial.println(b);
        if (r > 0) {
            //System.set(SYSTEM_CONFIG_SOFTAP_DISABLE_BROADCAST,"1");

        } else {
            //System.set(SYSTEM_CONFIG_SOFTAP_DISABLE_BROADCAST,"0");
        }
        

    }

    int8_t idx = 0;
    for (;;idx++) {
        Page& p = myPages[idx];
        if (!p.url) {
            idx = -1;
            break;
        }
        else if (strcmp(url, p.url)==0) {
            break;
        }
    }

    if (!strcmp(url, "/remoteenable")) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        switchToMaster();
        sendRemoteEnable();
        switchToSlave();
    } 
    else if (!strcmp(url, "/remotedisable")) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        switchToMaster();
        sendRemoteDisable();
        switchToSlave();
    }
    else if (urlString.indexOf("/getpresets?addr") == 0) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        int len = urlString.length();
        int address = (int)strtol(urlString.substring(len - 2, len).c_str(), nullptr, 16);
        Serial.println(address,HEX);
        //Begin the message
        result->write("{ \"data\": {\r\n"); 
        bytes_read = 0;
        switchToMaster(); //Send preset backup request to module
        getPresetData(address); 
        switchToSlave(); //Switch to slave to receive the response.
         //Wait for all of the data to come in.
        int bytes_read_last = 0;
        int done = false;
        unsigned long lastTime = millis();
        while (!done){ 
            unsigned long now = millis();
            if ((now - lastTime) >= 1000) {
                Serial.printlnf("%lu", now);
                lastTime = now;
                if (bytes_read == bytes_read_last){
                    done = true;
                }
                bytes_read_last = bytes_read; 
            }
        }
        String tmp;
        for (int i = 0; i < bytes_read; i++) {
            tmp = String(data[i],HEX);
            if (tmp.length() == 1) tmp = "0" + tmp;
            result->write(tmp);
            if (((i % 8) == 0) && (i != 0)){
                result->write("\r\n");
            }
        }
        //Finish the message.
        result->write("}\r\n");
        result->write("}\r\n");
    } 
    else if (!strcmp(url, "/favicon.ico")) {
        cb(cbArg, 0, 200, "image/x-icon", nullptr);
        //return URL icon here, if desired.
    } 
    else if (!strcmp(url, "/generate_204")) {
        cb(cbArg, 0, 204, "text/plain", nullptr);
    } 
    else if (idx==-1) {
        //Redirect not found to index.html
        //Header h("Location: /index.html\r\n");
        //cb(cbArg, 0, 301, "text/plain", &h);
        cb(cbArg, 0, 301, "text/plain", nullptr);
    }
    else {
        cb(cbArg, 0, 200, myPages[idx].mime_type, nullptr);
        result->write(myPages[idx].data);        
    }
}

//unsigned long t = 0;


STARTUP(softap_set_application_page_handler(myPage, nullptr));

void setup() {
    WiFi.listen();
    switchToSlave(); //Run in slave mode so that other modules can be master 
}

void loop() {

}




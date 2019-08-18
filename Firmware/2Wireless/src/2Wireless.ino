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

volatile int bytes_read = 0;

void receiveEvent(int howMany) {
  while (1 < Wire.available()) { // loop through all but the last
    char c = Wire.read(); // receive byte as a character
    Serial.print(c);         // print the character
    bytes_read++;
  }
  int x = Wire.read();    // receive byte as an integer
  Serial.println(x);         // print the integer
  bytes_read++;
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
    bytes_read = 0;
    Wire.beginTransmission(0);
    Wire.write(0x07);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x04);
    Wire.write(address); //Module address
    Wire.write(CARD_ADDRESS); //Card address lower byte. Upper byte is always 0x05. 
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
    Serial.printlnf("urlString is %s", urlString);
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
        sendRemoteEnable();
    } 
    else if (!strcmp(url, "/remotedisable")) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        sendRemoteDisable();
    }
    else if (!strcmp(url, "/getpresetdata")) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        getPresetData(0x44);//Test with 291e
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
        if (bytes_read > 10){
            result->write("<html>Yes</html>"); 
        } else {
            result->write(myPages[idx].data);
        }
    }
}

//unsigned long t = 0;


STARTUP(softap_set_application_page_handler(myPage, nullptr));

void setup() {
    WiFi.listen();
    Wire.begin(); //Can't use card address here because GC broadcasts
    
}

void loop() {

}




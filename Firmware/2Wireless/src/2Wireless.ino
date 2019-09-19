/*
 * Project 2Wireless
 * Description: Particle Photon code to host 200e preset bus using REST calls over wifi.
 * Author: Doug Clauder
 * Date: August 2019
 * License: This code is public domain. Distributed as-is; no warranty is given.
 */

#pragma SPARK_NO_PREPROCESSOR


#include "Particle.h"
#include "Arduino.h"
#include "softap_http.h"
#include "Wire.h"
#include "SPI.h"


SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(MANUAL); //Found in softap.cpp example. 

#define CARD_ADDRESS 0x00
#define BUFFER_SIZE 2400
#define FRAM_WREN 0x06 //FRAM WRITE ENABLE COMMAND
#define FRAM_WRITE 0x02 //FRAM WRITE COMMAND
#define FRAM_READ 0x03 //FRAM READ COMMAND



class RingBuffer {
    private:
        volatile uint8_t buffer[BUFFER_SIZE];
        volatile uint32_t head;
        volatile uint32_t tail;  
    public:
        RingBuffer();
        int bytesFree(void);
        int bytesQueued(void);
        uint8_t read(void);
        void write(uint8_t byte);
        void flush();
};

RingBuffer::RingBuffer() {
    this->head = 0;
    this->tail = 0;
    this->buffer[0] = 0;
}

int RingBuffer::bytesQueued(void) {
     int result = 0;
    result = (unsigned int)(BUFFER_SIZE + this->head - this->tail) % BUFFER_SIZE;
    return result;
}

int RingBuffer::bytesFree(void) {
    return BUFFER_SIZE - this->bytesQueued();
}

uint8_t RingBuffer::read(void){
    uint8_t result = 0x00;
    if (this->bytesQueued() > 0){
        result = this->buffer[this->tail];
        this->tail = (uint32_t)(this->tail+1) % BUFFER_SIZE;
    }
    return result; //Note byte returned, so always call bytesQueued() first to check.
}
 
void RingBuffer::write(uint8_t byte){
    if (this->bytesFree()>0) {
        this->buffer[this->head] = byte;
        this->head = (uint32_t)(this->head + 1) % BUFFER_SIZE;
    }
}

void RingBuffer::flush() {
    this->tail = this->head;
}

RingBuffer ringBuffer = RingBuffer();

volatile int bytes_read = 0; //i2c bytes received

void receiveEvent(int howMany) {
    while (Wire.available()) {   
        //Just assume room in buffer to avoid blocking.
        ringBuffer.write(Wire.read());
        bytes_read++;
    }
}

void framEnableWrite(){
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WREN); // send write enable OPCODE
    digitalWrite(A2, HIGH); //Set CS high to de-select
}

void framWrite(uint16_t addr, uint8_t value){
    framEnableWrite();
    uint8_t addr_msb = (0xFF00 & addr) >>8;
    uint8_t addr_lsb = (0x00FF & addr);
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WRITE); // send write enable OPCODE
    SPI.transfer(addr_msb); // send address msb
    SPI.transfer(addr_lsb); // send address lsb
    SPI.transfer(value); // send value
    digitalWrite(A2, HIGH); //Set CS high to de-select
}

uint8_t framRead(uint16_t addr){
    uint8_t result = 0;
    uint8_t addr_msb = (0xFF00 & addr) >>8;
    uint8_t addr_lsb = (0x00FF & addr);
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_READ); // send write enable OPCODE
    SPI.transfer(addr_msb); // send address msb
    SPI.transfer(addr_lsb); // send address lsb
    result = SPI.transfer(0); // read byte
    digitalWrite(A2, HIGH); //Set CS high to de-select
    return result;
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

void sendSavePreset(byte preset) {
    //Save Preset
    //Preset=0-29
    Wire.beginTransmission(0);
    Wire.write(0x04);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x02);
    Wire.write(preset);
    Wire.endTransmission();

}

void sendRecallPreset(byte preset) {
    //Recall Preset
    //Preset=0-29
    Wire.beginTransmission(0);
    Wire.write(0x04);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x01);
    Wire.write(preset);
    Wire.endTransmission();

}

void sendBackupPresets(byte address) {
    //Fetch presets from specified module
    //Address 0x44=291e.
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

void sendMidiNoteOn(byte mask, byte note, byte velo){
    //Send MIDI note ON event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Note 0=C-1, 127=G9.
    //Velo=0-255.
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22); //addr
    Wire.write(0x0F);
    Wire.write(0x90 | mask);
    Wire.write(0x00);
    Wire.write(note);
    Wire.write(velo);
    Wire.write(0x00);
    Wire.endTransmission();
}

void sendMidiNoteOff(byte mask, byte note, byte velo){
    //Send MIDI note OFF event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Note 0=C-1, 127=G9.
    //Velo=0-255.
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22);//addr
    Wire.write(0x0F);
    Wire.write(0x80 | mask);
    Wire.write(0x00);
    Wire.write(note);
    Wire.write(velo);
    Wire.write(0x00);
    Wire.endTransmission();
}

void sendMidiClockStart(){
    //Send MIDI clock start event.
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22);//addr
    Wire.write(0x0F);
    Wire.write(0xFA);
    Wire.write(0x00);
    Wire.write(0x00); //not sure what this is
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();
}

void sendMidiClockStop(){
    //Send MIDI clock stop event.
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22);//addr
    Wire.write(0x0F);
    Wire.write(0xFC);
    Wire.write(0x00);
    Wire.write(0x00); //not sure what this is
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();
}

void sendMidiClock(){
    //Send MIDI clock event. 24 per beat required?
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22);//addr
    Wire.write(0x0F);
    Wire.write(0xF8);
    Wire.write(0x00);
    Wire.write(0x00); //not sure what this is
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();
}

void sendMidiFineTune(byte mask, byte tune){
    //Send MIDI Fine Tune event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Tune 0x00=-49, 0x32=An, 0x63=49
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22);//addr
    Wire.write(0x0F);
    Wire.write(0xB0 | mask);
    Wire.write(0x00);
    Wire.write(0x1F);
    Wire.write(tune);
    Wire.write(0x00);
    Wire.endTransmission();
}

void sendMidiBend(byte mask, byte bend_lsb, byte bend_msb){
    //Send MIDI Bend.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //bend_msb 0x00=min bend, 0x40=no bend, 0x7F=max bend.
    //bend_lsb=0x00-0x7F fine tune of bend.
    Wire.beginTransmission(0);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.write(0x22);//addr
    Wire.write(0x0F);
    Wire.write(0xE0 | mask);
    Wire.write(0x00);
    Wire.write(bend_lsb);
    Wire.write(bend_msb);
    Wire.write(0x00);
    Wire.endTransmission();
}

int r, g, b = 0;

struct Page
{
    const char* url;
    const char* mime_type;
    const char* data;
};


static const char index_html[] = "<html><div align=\"center\"><form action=\"color\" method=\"get\"><input id=\"background-color\" name=\"color\" type=\"color\"/><input type=\"submit\" value=\"Go!\"/></form></div></html>";

Page myPages[] = {
     { "/index.html", "text/html", index_html },
     { nullptr }
};

static void myPage(const char* url, ResponseCallback* cb, void* cbArg, Reader* body, Writer* result, void* reserved)
{
    String urlString = String(url);
    Serial.printlnf("handling page %s", url);

    if (strcmp(url,"/index")==0) {
        //Serial.println("sending redirect");
        Header h("Location: /index.html\r\n");
        cb(cbArg, 0, 301, "text/plain", &h);
        return;
    }
    if (urlString.indexOf("/color") != -1) {
        r = (int)strtol(urlString.substring(14, 16).c_str(), nullptr, 16);
        g = (int)strtol(urlString.substring(16, 18).c_str(), nullptr, 16);
        b = (int)strtol(urlString.substring(18).c_str(), nullptr, 16);
        //Serial.println(r);
        //Serial.println(g);
        //Serial.println(b);
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
        cb(cbArg, 0, 200, "text/plain", nullptr);
        switchToMaster();
        sendRemoteEnable();
        switchToSlave();
    } 
    else if (!strcmp(url, "/remotedisable")) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        switchToMaster();
        sendRemoteDisable();
        switchToSlave();
    }
    else if (urlString.indexOf("/savepreset?preset") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        preset = preset % 29;
        //Serial.println(preset);
        switchToMaster();
        sendSavePreset(preset);
        switchToSlave();
    }
    else if (urlString.indexOf("/recallpreset?preset") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        preset = preset % 29;
        //Serial.println(preset);
        switchToMaster();
        sendRecallPreset(preset);
        switchToSlave();
    }
    else if (urlString.indexOf("/midinoteon?mask") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int amploc = urlString.indexOf('&');
        int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
        mask = mask & 0x0F;
        eqloc = urlString.indexOf('=',amploc);
        amploc = urlString.indexOf('&',eqloc);
        int note = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
        eqloc = urlString.indexOf('=',amploc);
        int velo = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        mask = mask & 0x0F;
        note = note & 0x7F;
        velo = velo & 0xFF;
        //Serial.println(mask);
        //Serial.println(note);
        //Serial.println(velo);
        switchToMaster();
        sendMidiNoteOn(mask,note,velo);
        switchToSlave();
    }
    else if (urlString.indexOf("/midinoteoff?mask") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int amploc = urlString.indexOf('&');
        int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
        mask = mask & 0x0F;
        eqloc = urlString.indexOf('=',amploc);
        amploc = urlString.indexOf('&',eqloc);
        int note = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
        eqloc = urlString.indexOf('=',amploc);
        int velo = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        mask = mask & 0x0F;
        note = note & 0x7F;
        velo = velo & 0xFF;
        //Serial.println(mask);
        //Serial.println(note);
        //Serial.println(velo);
        switchToMaster();
        sendMidiNoteOff(mask,note,velo);
        switchToSlave();
    }
    else if (urlString.indexOf("/midiclockstart") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        switchToMaster();
        sendMidiClockStart();
        switchToSlave();
    }
    else if (urlString.indexOf("/midiclockstop") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        switchToMaster();
        sendMidiClockStop();
        switchToSlave();
    }
    else if (urlString.indexOf("/midiclock") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        switchToMaster();
        sendMidiClock();
        switchToSlave();
    }
    else if (urlString.indexOf("/midifinetune?mask") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int amploc = urlString.indexOf('&');
        int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
        eqloc = urlString.indexOf('=',amploc);
        int tune = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        mask = mask & 0x0F;
        tune = tune & 0x3F; //max value is 63 decimal.
        //Serial.println(mask);
        //Serial.println(tune);
        switchToMaster();
        sendMidiFineTune(mask,tune);
        switchToSlave();
    }
    else if (urlString.indexOf("/midibend?mask") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int amploc = urlString.indexOf('&');
        int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
        eqloc = urlString.indexOf('=',amploc);
        amploc = urlString.indexOf('&',eqloc);
        int bend_lsb = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
        eqloc = urlString.indexOf('=',amploc);
        int bend_msb = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
        mask = mask & 0x0F;
        bend_lsb = bend_lsb & 0x7F; //max value is 7F.
        bend_msb = bend_msb & 0x7F; //max value is 7F.
        //Serial.println(mask);
        //Serial.println(bend_lsb);
        //Serial.println(bend_msb);
        switchToMaster();
        sendMidiBend(mask,bend_lsb,bend_msb);
        switchToSlave();
    }
    else if (urlString.indexOf("/getpresets?addr") == 0) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        int len = urlString.length();
        int address = (int)strtol(urlString.substring(len - 2, len).c_str(), nullptr, 16);
        //Serial.println(address,HEX);
        //Begin the message
        result->write("{ \"data\": {\r\n"); 
        bytes_read = 0;
        ringBuffer.flush();
        switchToMaster(); //Send preset backup request to module
        sendBackupPresets(address); 
        switchToSlave(); //Switch to slave to receive the response.
         //Wait for all of the data to come in.
        int bytes_read_last = 0;
        bool done = false;
        int bytes_written = 0;
        unsigned long lastTime = millis();
        String tmp;
        while (!done){ 
            tmp = "";     
            int bytesQueued = ringBuffer.bytesQueued(); 
            if (bytesQueued > 0) {
                tmp = String(ringBuffer.read(),HEX);
            }
            if (tmp.length() == 1) tmp = "0" + tmp;
            if (tmp.length() > 0) {
                result->write(tmp);
                bytes_written++;
                if (((bytes_written % 8) == 0) && (bytes_written != 0)){
                    result->write("\r\n");
                }
            }
            //Check to see if bytes are still arriving
            unsigned long now = millis();
            if ((now - lastTime) >= 2000) {
                //Serial.printlnf("%lu", now);
                lastTime = now;
                if ((bytes_read == bytes_read_last) && (bytesQueued == 0)){
                    done = true;
                }
                bytes_read_last = bytes_read; 
            }
        }
        //Finish the message.
        result->write("}\r\n");
        result->write("}\r\n");
    } else if (urlString.indexOf("/readmemory?addr") == 0) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int addr = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        uint8_t value;
        value = framRead(addr);
        String tmp = String(value,HEX);
        result->write("{ \"data\": {\r\n"); 
        result->write(tmp);
        result->write("\r\n");
        result->write("}\r\n");
        result->write("}\r\n");
    } else if (urlString.indexOf("/writememory?addr") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int amploc = urlString.indexOf('&');
        int addr = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
        eqloc = urlString.indexOf('=',amploc);
        int value = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
        framWrite(addr,value);
    } else if (urlString.indexOf("/writefirmware") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        //char* postData = body->fetch_as_string();
        //Serial.printlnf("received data %s", postData);
        //free(postData);

        static uint8_t s_buffer[1024] = {0};
        int bread = 0;
        do {
            bread = body->read(s_buffer, sizeof(s_buffer));
            if (bread > 0) {
                result->write(s_buffer, bread);
            }
        } while(bread > 0);
    } else if (!strcmp(url, "/favicon.ico")) {
        cb(cbArg, 0, 200, "image/x-icon", nullptr);
        //return URL icon here, if desired.
    } else if (!strcmp(url, "/generate_204")) {
        cb(cbArg, 0, 204, "text/plain", nullptr);
    } else if (idx==-1) {
        //Redirect not found to index.html
        //Header h("Location: /index.html\r\n");
        //cb(cbArg, 0, 301, "text/plain", &h);
        cb(cbArg, 0, 301, "text/plain", nullptr);
    } else {
        cb(cbArg, 0, 200, myPages[idx].mime_type, nullptr);
        result->write(myPages[idx].data);        
    }
}

//unsigned long t = 0;


STARTUP(softap_set_application_page_handler(myPage, nullptr));

void setup() {
    WiFi.listen();
    switchToSlave(); //Run in slave mode so that other modules can be master 
    pinMode(A2, OUTPUT); //SPI CS
    SPI.begin(); //Defaults to A2 as CS 
    SPI.setClockSpeed(10, MHZ);
    SPI.setBitOrder(MSBFIRST); // Data is read and written MSB first.
    SPI.setDataMode(SPI_MODE0);
    waitUntil(WiFi.listening); //found in softap.cpp example
}

void loop() {
    Particle.process(); //found in softap.cpp example
}




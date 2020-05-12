/*
 * MIT License
 *
 * Copyright (c) 2019 Doug Clauder
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * Project 2Wireless
 * Description: Particle Photon code to host 200e preset bus using REST calls over wifi.
 * Author: Doug Clauder
 * Date: August 2019
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

class JsonToken {
    public:
        JsonToken();
        int count;
        int status;
        String data;
};

JsonToken::JsonToken() {
    this->count = 0;
    this->status = 0;
    this->data = "";
}


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

RingBuffer ringBuffer = RingBuffer(); //Used to buffer data between HTTP and I2C.
volatile int read_counter; //Used to auto increment read addresses during I2C READ from master.
volatile int fram_address=0; //This is global only because it must span OnRequest calls during I2C READ from master. 

void framEnableWrite(){
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WREN); // send write enable OPCODE
    digitalWrite(A2, HIGH); //Set CS high to de-select
}

void framWrite(int addr, uint8_t value){
    framEnableWrite();
    //Address on the MB85RS2MTA is 18 bits total, requiring 3 bytes. 
    uint8_t addr_byte_upper = (0x3F0000 & addr) >>16;
    uint8_t addr_byte_middle = (0x00FF00 & addr) >>8;
    uint8_t addr_byte_lower = (0x0000FF & addr);
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WRITE); // send write enable OPCODE
    SPI.transfer(addr_byte_upper); // send address upper byte
    SPI.transfer(addr_byte_middle); // send address middle byte
    SPI.transfer(addr_byte_lower); // send address lower byte
    SPI.transfer(value); // send value
    digitalWrite(A2, HIGH); //Set CS high to de-select
}

void framWriteHexString(int addr, String value){
    framEnableWrite();
    //Address on the MB85RS2MTA is 18 bits total, requiring 3 bytes. 
    uint8_t addr_byte_upper = (0x3F0000 & addr) >>16;
    uint8_t addr_byte_middle = (0x00FF00 & addr) >>8;
    uint8_t addr_byte_lower = (0x0000FF & addr);
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WRITE); // send write enable OPCODE
    SPI.transfer(addr_byte_upper); // send address upper byte
    SPI.transfer(addr_byte_middle); // send address middle byte
    SPI.transfer(addr_byte_lower); // send address lower byte
    String remainder = value;
    while (remainder.length() >= 2){
        uint8_t data = (int)strtol(remainder.substring(0,2).c_str(), nullptr, 16); 
        SPI.transfer(data); // send value
        remainder = remainder.substring(2,remainder.length());
    }
    digitalWrite(A2, HIGH); //Set CS high to de-select
}

uint8_t framRead(int addr){
    uint8_t result = 0;
    //Address on the MB85RS2MTA is 18 bits total, requiring 3 bytes. 
    uint8_t addr_byte_upper = (0x3F0000 & addr) >>16;
    uint8_t addr_byte_middle = (0x00FF00 & addr) >>8;
    uint8_t addr_byte_lower = (0x0000FF & addr);
    digitalWrite(A2, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_READ); // send write enable OPCODE
    SPI.transfer(addr_byte_upper); // send address upper byte
    SPI.transfer(addr_byte_middle); // send address middle byte
    SPI.transfer(addr_byte_lower); // send address lower byte
    result = SPI.transfer(0); // read byte
    digitalWrite(A2, HIGH); //Set CS high to de-select
    return result;
}

void receiveEvent(int howMany) {
    while (Wire.available() > 0) {   
        //Just assume room in buffer to avoid blocking.
        ringBuffer.write(Wire.read()); //Save into ring buffer to support I2C WRITE from master.
    }
}
void requestEvent() {
    //Photon core (i2c_hal.c) modified so that this handler is called on every byte requested.
    //Module reads presets here during preset restore. The module first sends an EEPROM memory
    //address, then issues a (maybe large) number of byte read requests. 
    //The number of bytes requested varies by module. 292e requests 8 bytes per preset, and 291e
    //requests 253 bytes per preset. 251e is over 2k. When the module is done reading, it issues
    //a STOP and this handler will not be called until the next time a restore is requested.
    if (Wire.available() > 0){
        //This is the first read following address write from the module.
        //The master wrote two or three bytes for the memory address, and we now retrieve it
        //from the rxBuffer. MSB is sent first, LSB second.
        //Typically this happens once at the beginning of each preset.
        read_counter = 0;
        uint8_t addr_byte_upper = 0x00;
        uint8_t addr_byte_middle = 0x00;
        uint8_t addr_byte_lower = 0x00;

        while (Wire.available() > 3) {
            Wire.read(); //Only three bytes supported. If buffer has more, clear it out.
        }
        if (Wire.available() == 1){
            addr_byte_lower = Wire.read();
        } else if (Wire.available() == 2) {
            addr_byte_middle = Wire.read();
            addr_byte_lower = Wire.read();
        } else if (Wire.available() == 3) {
            addr_byte_upper = Wire.read();
            addr_byte_middle = Wire.read();
            addr_byte_lower = Wire.read();
        } 
        fram_address = 0;
        fram_address = (addr_byte_upper) << 16;
        fram_address = fram_address | (addr_byte_middle) << 8;
        fram_address = fram_address | addr_byte_lower;
    } 
    
    if (read_counter < 65535) { //Max 16 bit address possible, although FRAM supports 18 bits. Will this ever happen?
        I2C_SendData (I2C1, framRead(fram_address + read_counter));
    } else {
        I2C_StretchClockCmd(I2C1, DISABLE); //Say goodbye by releasing SCL. Restored on next START received.
    }
    read_counter++;
}

void switchToMaster(){
    Wire.end();
    Wire.begin();
}

void switchToSlave(){
    Wire.end();
    Wire.begin(0x50 | CARD_ADDRESS);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);
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

void sendRestorePresets(byte address) {
    //Request specified module to restore presets.
    //Address 0x44=291e.
    Wire.beginTransmission(0);
    Wire.write(0x07);
    Wire.write(0x00);
    Wire.write(0x22);
    Wire.write(0x05);
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

String getFormattedError(String errorString) {
    String result = "{\r\n";
    result = result + "\"error\" : {\r\n";
    result = result + errorString;
    result = result + "\r\n";
    result = result + "}\r\n";
    result = result + "}\r\n";
    return result;
};

void getJsonToken(JsonToken* token, Reader* body) {
    char begin_char = '\"'; 
    char end_char = '\"';
    static uint8_t s_buffer[1] = {0};
    int bread = 0;
    token->data = "";
    bread = body->read(s_buffer, sizeof(s_buffer));
    char s = (char)s_buffer[0];
    while ((token->status == 0) && (bread > 0)) {
        if (s == begin_char){
            token->status = 1;
        }
        bread = body->read(s_buffer, sizeof(s_buffer));
        s = (char)s_buffer[0];
    } 

    while ((token->status == 1) && (bread > 0) && ((token->data).length() < 256)) {
        if (s == end_char){
            token->status = 2;
        } else {
            token->data = token->data + s;
            bread = body->read(s_buffer, sizeof(s_buffer));
            s = (char)s_buffer[0];
        }
    }
    token->status = 0; //ready for next time
};

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
        if ((preset >=1) && (preset <=30)) {
            preset = preset - 1; //0-29 internally.
            //Serial.println(preset);
            switchToMaster();
            sendSavePreset(preset);
            switchToSlave();
        }

    }
    else if (urlString.indexOf("/recallpreset?preset") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        if ((preset >=1) && (preset <=30)) {
            preset = preset - 1; //0-29 internally.
            //Serial.println(preset);
            switchToMaster();
            sendRecallPreset(preset);
            switchToSlave();
        }

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
        //Parse module address and optional length parameter (number of bytes in each line of the result data).
        int len = urlString.length();
        int amploc = urlString.indexOf('&');
        if (amploc==-1) amploc=len;
        int address = (int)strtol(urlString.substring(amploc - 2, amploc).c_str(), nullptr, 16);
        int eqloc = urlString.indexOf('=',amploc);
        int line_length = 0; 
        if (eqloc > -1) {
            line_length = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
        }
         //Serial.println(address,HEX);
        //Begin the message
        result->write("{\r\n"); 
        result->write("\"module_address\": \"0x" + String(address,HEX) + "\",\r\n"); 
        result->write("\"data\": \""); 
        ringBuffer.flush();
        switchToMaster(); //Send preset backup request to module
        sendBackupPresets(address); 
        switchToSlave();
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
                if ((line_length != 0) && ((bytes_written % line_length) == 0) && (bytes_written != 0)){
                    result->write("\r\n");
                }
                lastTime = millis();
            }
            //Check to see if bytes are still arriving
            unsigned long now = millis();
            if ((now - lastTime) >= 1000) {
                //Serial.printlnf("%lu", now);
                done = true;
            }
        }
        //Finish the message.
        result->write("\"\r\n");
        result->write("}\r\n");
    } else if (urlString.indexOf("/setpresets") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        uint8_t module_address = 0x00;
        fram_address = 0;
        String fram_write_only = "";
        JsonToken* token = new JsonToken();
        getJsonToken(token, body);
        while (token->data != "") {
            if (token->data ==  "module_address") 
            {
                //REQUIRED: The module address is needed in the restore presets command
                //to the module. Ignored and unnecessary if fram_write_only = "true".
                getJsonToken(token, body);
                int len = (token->data).length();
                if (len >= 2){
                    module_address = (int)strtol((token->data).c_str(), nullptr, 16);
                }
            } else if (token->data ==  "fram_write_only") 
            {
                //Optional parameter to suppress module update. Write to fram only. 
                //This can be used to send multiple POSTs to fram before updating 
                //the module. Maximum POST size is about 20k, and this is about 1/8
                //of the 251e JSON. So we need to be able to lay down the data to 
                //fram with multiple POSTs before telling the module to restore.
                //Last POST of the batch should either omit this parameter or include 
                //it with value of "false".
                getJsonToken(token, body);
                fram_write_only = token->data;
            } else if (token->data ==  "addr") 
            {
                //This parameter carries the memory address for the preset data.
                getJsonToken(token, body);
                fram_address = (int)strtol((token->data).c_str(), nullptr, 16);
            } else if (token->data ==  "data") {
                //This parameter carries the data that will be written to fram and then later read
                //by the module. Some modules have one per preset, others (eg. 251e) 
                //have many per preset. 
                String data_string = "";
                getJsonToken(token, body);
                data_string = token->data;
                if (data_string.length() > 0) {
                    framWriteHexString(fram_address,data_string);
                }
            }
            getJsonToken(token, body);
        } 
        if ((module_address != 0x00) && (fram_write_only != "true")) {
            read_counter = 0; //Initialize counter for subsequent I2C READs from master.
            switchToMaster(); //Send preset restore request to module
            sendRestorePresets(module_address); 
            switchToSlave(); //Switch to slave to receive the read requests.
        }
        delete token;
    } else if (urlString.indexOf("/readbyte?addr") == 0) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int addr = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
        uint8_t value;
        value = framRead(addr);
        String tmp = String(value,HEX);
        result->write("{ \"data\": \"" + tmp + "\"\r\n"); 
        result->write("}\r\n");
    } else if (urlString.indexOf("/writebyte?addr") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        int len = urlString.length();
        int eqloc = urlString.indexOf('=');
        int amploc = urlString.indexOf('&');
        int addr = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
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
    } else if (urlString.indexOf("/debug") == 0) {
        cb(cbArg, 0, 200, "text/plain", nullptr);
        while (ringBuffer.bytesQueued())
        {
            result->write(String(ringBuffer.read(),HEX) + "\r\n");
        }
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



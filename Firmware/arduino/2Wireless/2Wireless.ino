#include <Wire.h>
#include <WiFi101.h>
#include <SPI.h>
//#include "arduino_secrets.h"

#define CARD_ADDRESS 0x00
#define BUFFER_SIZE 4800
#define FRAM_WREN 0x06 //FRAM WRITE ENABLE COMMAND
#define FRAM_WRITE 0x02 //FRAM WRITE COMMAND
#define FRAM_READ 0x03 //FRAM READ COMMAND

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = "BUCHLA";        // your network SSID (name)
char pass[] = "BUCHLA200E";    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                // your network key Index number (needed only for WEP)

int led =  LED_BUILTIN;
int status = WL_IDLE_STATUS;
WiFiServer server(80);

bool v2version=false; //used to support pre PRIMO firmware.
bool triggerRemoteEnable = false;
bool triggerRemoteDisable = false;

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  //while (!Serial) {
  //  ; // wait for serial port to connect. Needed for native USB port only
  //}

  Serial.println("Access Point Web Server");

  pinMode(led, OUTPUT);      // set the LED pin mode

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }

  // by default the local IP address of will be 192.168.1.1
  // you can override it with the following:
  WiFi.config(IPAddress(192, 168, 0, 1));

  // print the network name (SSID);
  Serial.print("Creating access point named: ");
  Serial.println(ssid);

  // Create open network. Change this line if you want to create an WEP network:
  //2WIRELESS added pass
  status = WiFi.beginAP(ssid,pass);
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating access point failed");
    // don't continue
    while (true);
  }

  // wait 10 seconds for connection:
  delay(10000);

  // start the web server on port 80
  server.begin();

  // you're connected now, so print out the status
  printWiFiStatus();
}


void loop() {
  // compare the previous status to the current status
  if (status != WiFi.status()) {
    // it has changed update the variable
    status = WiFi.status();

    if (status == WL_AP_CONNECTED) {
      byte remoteMac[6];

      // a device has connected to the AP
      Serial.print("Device connected to AP, MAC address: ");
      WiFi.APClientMacAddress(remoteMac);
      printMacAddress(remoteMac);
    } else {
      // a device has disconnected from the AP, and we are back in listening mode
      Serial.println("Device disconnected from AP");
    }
  }
 
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    String urlString = "";                  // string to hold the request URL
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            Serial.println(""); 
            Serial.println("URL is: " + urlString); 
            // Check to see if the client request was "GET /H" or "GET /L":
            if (urlString.startsWith("GET /H")) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              writeHomepage(client);
              digitalWrite(led, HIGH);               // GET /H turns the LED on
            }
            else if (urlString.startsWith("GET /L")) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              writeHomepage(client);
              digitalWrite(led, LOW);                // GET /L turns the LED off
            } 
            else if (urlString.indexOf("/remoteenable") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain");
              Serial.println("Remote Enable Received"); 
              triggerRemoteEnable = true;
              //switchToMaster();
              //sendRemoteEnable();
              //switchToSlave();
            } 
            else if (urlString.indexOf("/remotedisable") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain");
              Serial.println("Remote Disable Received"); 
              triggerRemoteDisable = true;
              //switchToMaster();
              //sendRemoteDisable();
              //switchToSlave();
            } 
            else if (urlString.indexOf("/savepreset?preset") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Save Prest Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //if ((preset >=1) && (preset <=30)) {
              //    preset = preset - 1; //0-29 internally.
                  //Serial.println(preset);
              //    switchToMaster();
              //    sendSavePreset(preset);
              //    switchToSlave();
              //}
            }
            else if (urlString.indexOf("/recallpreset?preset") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Recall Preset Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //if ((preset >=1) && (preset <=30)) {
              //    preset = preset - 1; //0-29 internally.
                  //Serial.println(preset);
              //    switchToMaster();
              //    sendRecallPreset(preset);
              //    switchToSlave();
              //}
            }
            else if (urlString.indexOf("/midinoteon?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Note On Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int amploc = urlString.indexOf('&');
              //int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //mask = mask & 0x0F;
              //eqloc = urlString.indexOf('=',amploc);
              //amploc = urlString.indexOf('&',eqloc);
              //int note = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
              //eqloc = urlString.indexOf('=',amploc);
              //int velo = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //mask = mask & 0x0F;
              //note = note & 0x7F;
              //velo = velo & 0xFF;
              //Serial.println(mask);
              //Serial.println(note);
              //Serial.println(velo);
              //switchToMaster();
              //sendMidiNoteOn(mask,note,velo);
              //switchToSlave();
            }
            else if (urlString.indexOf("/midinoteoff?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Note Off Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int amploc = urlString.indexOf('&');
              //int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //mask = mask & 0x0F;
              //eqloc = urlString.indexOf('=',amploc);
              //amploc = urlString.indexOf('&',eqloc);
              //int note = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
              //eqloc = urlString.indexOf('=',amploc);
              //int velo = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //mask = mask & 0x0F;
              //note = note & 0x7F;
              //velo = velo & 0xFF;
              //Serial.println(mask);
              //Serial.println(note);
              //Serial.println(velo);
              //switchToMaster();
              //sendMidiNoteOff(mask,note,velo);
              //switchToSlave();
            }
            else if (urlString.indexOf("/midiclockstart") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Clock Start Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //switchToMaster();
              //sendMidiClockStart();
              //switchToSlave();
            }
            else if (urlString.indexOf("/midiclockstop") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Clock Stop Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //switchToMaster();
              //sendMidiClockStop();
              //switchToSlave();
            }
            else if (urlString.indexOf("/midiclock") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Clock Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //switchToMaster();
              //sendMidiClock();
              //switchToSlave();
            }
            else if (urlString.indexOf("/midifinetune?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Fine Tune Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int amploc = urlString.indexOf('&');
              //int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //eqloc = urlString.indexOf('=',amploc);
              //int tune = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //mask = mask & 0x0F;
              //tune = tune & 0x3F; //max value is 63 decimal.
              //Serial.println(mask);
              //Serial.println(tune);
              //switchToMaster();
              //sendMidiFineTune(mask,tune);
              //switchToSlave();
            }
            else if (urlString.indexOf("/midibend?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("MIDI Bend Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int amploc = urlString.indexOf('&');
              //int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //eqloc = urlString.indexOf('=',amploc);
              //amploc = urlString.indexOf('&',eqloc);
              //int bend_lsb = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //eqloc = urlString.indexOf('=',amploc);
              //int bend_msb = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
              //mask = mask & 0x0F;
              //bend_lsb = bend_lsb & 0x7F; //max value is 7F.
              //bend_msb = bend_msb & 0x7F; //max value is 7F.
              //Serial.println(mask);
              //Serial.println(bend_lsb);
              //Serial.println(bend_msb);
              //switchToMaster();
              //sendMidiBend(mask,bend_lsb,bend_msb);
              //switchToSlave();
            }
            else if (urlString.indexOf("/getpresets?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Get Presets Received"); 
              //cb(cbArg, 0, 200, "application/json", nullptr);
              //Parse module address and optional length parameter (number of bytes in each line of the result data).
              //int len = urlString.length();
              //int amploc = urlString.indexOf('&');
              //if (amploc==-1) amploc=len;
              //int address = (int)strtol(urlString.substring(amploc - 2, amploc).c_str(), nullptr, 16);
              //int eqloc = urlString.indexOf('=',amploc);
              //int line_length = 0; 
              //if (eqloc > -1) {
              //    line_length = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //}
               //Serial.println(address,HEX);
              //Begin the message
              //result->write("{\r\n"); 
              //result->write("\"module_address\": \"0x" + String(address,HEX) + "\",\r\n"); 
              //result->write("\"data\": \""); 
              //ringBuffer.flush();
              //switchToMaster(); //Send preset backup request to module
              //sendBackupPresets(address); 
              //switchToSlave();
              //bool done = false;
              //int bytes_written = 0;
              //unsigned long lastTime = millis();
              //String tmp;
              //while (!done){ 
              //    tmp = "";     
              //    int bytesQueued = ringBuffer.bytesQueued(); 
              //    if (bytesQueued > 0) {
              //        tmp = String(ringBuffer.read(),HEX);
              //    }
              //    if (tmp.length() == 1) tmp = "0" + tmp;
              //    if (tmp.length() > 0) {
              //        result->write(tmp);
              //        bytes_written++;
              //        if ((line_length != 0) && ((bytes_written % line_length) == 0) && (bytes_written != 0)){
              //            result->write("\r\n");
              //        }
              //        lastTime = millis();
              //    }
                  //Check to see if bytes are still arriving
              //    unsigned long now = millis();
              //    if ((now - lastTime) >= 1000) {
              //        //Serial.printlnf("%lu", now);
              //        done = true;
              //    }
              //}
              //Finish the message.
              //result->write("\"\r\n");
              //result->write("}\r\n");
            } else if (urlString.indexOf("/setpresets?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Set Presets Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int eqloc = urlString.indexOf('=');
              //int module_address = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
              //module_address = module_address & 0x7F;
              //if (module_address != 0x00) {
              //    read_counter = 0; //Initialize counter for subsequent I2C READs from master.
              //    switchToMaster(); //Send preset restore request to module
              //    sendRestorePresets(module_address); 
              //    switchToSlave(); //Switch to slave to receive the read requests.
              //}
            } else if (urlString.indexOf("/readmemory?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Read Memory Received"); 
              //cb(cbArg, 0, 200, "application/json", nullptr);
              //int len = urlString.length();
              //int amploc = urlString.indexOf('&');
              //if (amploc==-1) amploc=len;
              //int eqloc = urlString.indexOf('=');
              //int addr = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //eqloc = urlString.indexOf('=',amploc);
              //int length = 1; 
              //if (eqloc > -1) {
              //    length = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              //}
              //String dataString = "";
              //String tmp="";
              //int offset = 0;
              //result->write("[{\"addr\": \"" + String(addr,HEX) + "\",\"data\": \"");
              //for (int i = 0; i<length; i++){
              //    tmp = String(framRead(addr + offset),HEX);
              //    if (tmp.length() == 1) tmp = "0" + tmp;
              //    dataString = dataString + tmp;
              //    offset++; 
              //    if (i == (length - 1)) {
              //        result->write(dataString + "\"}]");
              //    } else if ( offset % 128 == 0 ) {
              //        addr = addr + offset;
              //        result->write(dataString + "\"},{\"addr\": \"" + String(addr,HEX) + "\",\"data\": \"");
              //        offset = 0;
              //        dataString = "";
              //    }
              //}
            } else if (urlString.indexOf("/writememory?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Write Memory Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //int len = urlString.length();
              //int amploc = urlString.indexOf('&');
              //if (amploc==-1) amploc=len;
              //int eqloc = urlString.indexOf('=');
              //int addr = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              //eqloc = urlString.indexOf('=',amploc);
              //String data_string = "00"; 
              //if (eqloc > -1) {
              //    data_string = urlString.substring(eqloc + 1, len);
              //}
              //framWriteHexString(addr,data_string);
            } else if (urlString.indexOf("/writememory") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Write Memory Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //fram_address = 0;
              //JsonToken* token = new JsonToken();
              //getJsonToken(token, body);
              //while (token->data != "") {
              //    if (token->data ==  "addr") 
              //    {
              //        //This parameter carries the memory address for the data.
              //        getJsonToken(token, body);
              //        fram_address = (int)strtol((token->data).c_str(), nullptr, 16);
              //    } else if (token->data ==  "data") {
              //        //This parameter carries the data that will be written to fram.
              //        String data_string = "";
              //        getJsonToken(token, body);
              //        data_string = token->data;
              //        if (data_string.length() > 0) {
              //            framWriteHexString(fram_address,data_string);
              //        }
              //    }
              //    getJsonToken(token, body);
              //} 
              //delete token;
            } else if (urlString.indexOf("/test") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              Serial.println("Test Received"); 
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //Used to determine the max POST size.
              //static uint8_t s_buffer[1] = {0};
              //int bread = 0;
              //do {
              //    bread = body->read(s_buffer, sizeof(s_buffer));
              //    if (bread > 0) {
              //        result->write(s_buffer, bread);
              //    }
              //} while(bread > 0);
            } else if (urlString.indexOf("/favicon.ico") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","image/x-icon");
              Serial.println("Favicon Received"); 
              //cb(cbArg, 0, 200, "image/x-icon", nullptr);
              //return URL icon here, if desired.
            } else if (urlString.indexOf("/generate_204") >= 0) {
              writeHeader(client,"HTTP/1.1 204 No Content","Content-type:text/plain");
              Serial.println("Generate 204 Received"); 
              //cb(cbArg, 0, 204, "text/plain", nullptr);
            }           
            else {
              Serial.println("Returning homepage");
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
              writeHomepage(client);       
            }
            // break out of the while loop:
            break;
          }
          else {      // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }
        else if (c != '\r') {    // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
        if ((currentLine.startsWith("GET") || (currentLine.startsWith("POST"))) && (c == '\r')){
          urlString = currentLine;
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
  if (triggerRemoteEnable) {
    Serial.println("Remote enable triggered");
    triggerRemoteEnable = false;
    switchToMaster();
    sendRemoteEnable();
    switchToSlave();
  } else if (triggerRemoteDisable) {
    Serial.println("Remote disable triggered");
    triggerRemoteDisable = false;
    switchToMaster();
    sendRemoteDisable();
    switchToSlave();
  }
}

void receiveEvent(int howMany) {

}
void requestEvent() {

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
    if (v2version) {
        Wire.write(0x14);
    } else {
        Wire.write(0x04);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x16);
        Wire.write(0xFF);
    }
    Wire.endTransmission();
}

void sendRemoteDisable() {
    //Remote Disable
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0x15);
    } else {
        Wire.write(0x04);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x17);
        Wire.write(0xFF);
    }
    Wire.endTransmission();
}

void writeHeader(WiFiClient client, String statusString, String contentString) {
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println(statusString);
  client.println(contentString);
  client.println();
}

void writeHomepage(WiFiClient client) {
  // the content of the HTTP response follows the header:
  client.print("Click <a href=\"/H\">here</a> turn the LED on<br>");
  client.print("Click <a href=\"/L\">here</a> turn the LED off<br>");
  
  // The HTTP response ends with another blank line:
  client.println();
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);

}

void printMacAddress(byte mac[]) {
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
    if (i > 0) {
      Serial.print(":");
    }
  }
  Serial.println();
}

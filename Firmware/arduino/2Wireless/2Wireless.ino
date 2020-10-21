 /*
 * Pin             |  PORT  | Label
 * ----------------+--------+-------
 *   2       D2    |  PA10  | "V2VERSION_PIN"
 *   3       D3    |  PA11  | "WIFI_OPEN_PIN"
 *   4       D4    |  PB10  | "SPI_CS"
 *   8       D8    |  PA16  | "MOSI"   
 *   9       D9    |  PA17  | "SCK"   
 *   10      D10   |  PA19  | "MISO"  
 *   11      D11   |  PA08  | "SDA"
 *   12      D12   |  PA09  | "SCL"
*/
#include <Wire.h>
#include <WiFi101.h>
#include <SPI.h>
//#include "arduino_secrets.h"

#define CARD_ADDRESS 0x00
#define BUFFER_SIZE 2000 //Ring buffer size for incoming I2C. 
#define FRAM_WREN 0x06 //FRAM WRITE ENABLE COMMAND
#define FRAM_WRITE 0x02 //FRAM WRITE COMMAND
#define FRAM_READ 0x03 //FRAM READ COMMAND
#define SPI_CS 4 //FRAM Chip Select
#define WIFI_OPEN_PIN 3
#define V2VERSION_PIN 2

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


class RingBuffer2 {
    private:
        volatile uint8_t buffer[BUFFER_SIZE];
        volatile uint32_t head;
        volatile uint32_t tail;  
    public:
        RingBuffer2();
        int bytesFree(void);
        int bytesQueued(void);
        uint8_t read(void);
        void write(uint8_t byte);
        void flush();
};

RingBuffer2::RingBuffer2() {
    this->head = 0;
    this->tail = 0;
    this->buffer[0] = 0;
}

int RingBuffer2::bytesQueued(void) {
     int result = 0;
    result = (unsigned int)(BUFFER_SIZE + this->head - this->tail) % BUFFER_SIZE;
    return result;
}

int RingBuffer2::bytesFree(void) {
    return BUFFER_SIZE - this->bytesQueued();
}

uint8_t RingBuffer2::read(void){
    uint8_t result = 0x00;
    if (this->bytesQueued() > 0){
        result = this->buffer[this->tail];
        this->tail = (uint32_t)(this->tail+1) % BUFFER_SIZE;
    }
    return result; //Note byte returned, so always call bytesQueued() first to check.
}
 
void RingBuffer2::write(uint8_t byte){
    if (this->bytesFree()>0) {
        this->buffer[this->head] = byte;
        this->head = (uint32_t)(this->head + 1) % BUFFER_SIZE;
    }
}

void RingBuffer2::flush() {
    this->tail = this->head;
}

RingBuffer2 ringBuffer = RingBuffer2(); //Used to buffer data between HTTP and I2C. 
volatile int read_counter=0; //Used to auto increment read addresses during I2C READ from master.
volatile int write_counter=0; //USed to auto increment write addresses during I2C WRITE from master.
volatile int fram_address=0; //This is global only because it must span OnRequest calls during I2C READ from master. 
volatile bool startup = true; //Used to free 206e when starting up.
volatile bool write_i2c_to_fram = false; //Cache incoming i2c to FRAM.
unsigned long readTime = 0; //used to detect idle to free up 206e at start.
bool v2version=false; //used to support pre PRIMO firmware.
SPISettings spiSettings(12000000, MSBFIRST, SPI_MODE0); //MKR1000 max is 12MHz. FRAM chip max is 40MHz.
String homepageString = "<html> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <style> .p { display: flex; flex-direction: column; background-color:lightgray; color:darkblue; font-family:Arial; font-weight:bold; letter-spacing: 2px; height: 218px; } .hf { font-size:12px; padding: 10px; border: 1px solid darkblue; } .r { font-style:italic; font-size:20px; padding: 10px; border-left: 1px solid darkblue; border-right: 1px solid darkblue; height: 50px; } .row { display: flex; flex-direction: row; justify-content: space-around; align-items: center; } .col { display: flex; flex-direction: column; justify-content: space-around; align-items: center; font-size:14px; } .screw { display: flex; justify-content: center; align-content: center; flex-direction: column; height: 18px; width: 18px; background-color: #999; border-radius: 50%; color: #444; font-size:30px; border: 1px solid black; } .b { border-radius: 50%; height:40px; width:40px; background-color: #777;  } .d { height: 50px; width: 225px; background-color: white; padding: 0px; margin: 0px; border: 1px solid black; } .dx { height: 50px; width: 200px; background-color: white; padding: 0px; margin: 0px; border: 0px; } .dc { font-family: Courier New; font-weight:bold; background-color: #cfc; height: 50px; border: 0px; } .hole { height: 10px; width: 10px; background-color: #000; border-radius: 50%; display: inline-block; } </style> </head> <body> <div class=\"p\"> <div class=\"hf row\"> <div class=\"screw\">+</div> <div>PRESET &nbsp; MANAGER</div> <div class=\"screw\">+</div> </div> <div class=\"r row\" style=\"border-bottom: 1px solid darkblue;\"> <div class=\"col\"> <button id=\"sB\" type=\"button\" class=\"b\" style=\"background-color: #36f;\" tabindex=\"1\"></button> <div style=\"height:2\"></div> <div>store</div> </div> <div class=\"d row\"> <div class=\"dx col\" > <input id=\"current_preset\" class=\"dc\" style=\"width:21px;background-color: #afa;\" disabled> <input class=\"dc\" disabled style=\"width:21px;background-color: #afa;\"> </div> <div class=\"dx col\" > <input id=\"current_name\" class=\"dc\" maxlength=\"20\" tabindex=\"2\"> <input id=\"recall_name\" class=\"dc\" disabled style=\"background-color: #afa;\"> </div> <div class=\"dx col\" > <input class=\"dc\" disabled style=\"width:40px;background-color: #afa;\"> <input id=\"recall_preset\" class=\"dc\" style=\"width:40px;\" type=\"number\" min=\"1\" max=\"30\" tabindex=\"3\"> </div> </div> <div class=\"col\"> <button id=\"rB\" type=\"button\" class=\"b\" style=\"background-color: #36f;\" tabindex=\"4\"></button> <div style=\"height:2\"></div> <div>recall</div> </div> </div> <div class=\"r row\"> <div class=\"row\" style=\"width:200px\"> <div class=\"col\"> <button id=\"lB\" type=\"button\" class=\"b\" tabindex=\"5\"></button> <div style=\"height:2\"></div> <div>last</div> </div> <div class=\"col\"> <button id=\"nB\" type=\"button\" class=\"b\" tabindex=\"6\"></button> <div style=\"height:2\"></div> <div>next</div> </div> </div> <div class=\"col\" style=\"align-items: flex-start;width:33%\"> <div class=\"row\" style=\"align-items: flex-start;\"> <input type=\"radio\" id=\"v3\" name=\"version\" value=\"v3\" tabindex=\"7\" hidden> <label for=\"v3\" hidden>primo</label> </div> <div class=\"row\" style=\"align-items: flex-start;\"> <input type=\"radio\" id=\"v2\" name=\"version\" value=\"v2\" tabindex=\"8\" hidden> <label for=\"v2\" hidden>v2    </label> </div> </div> <div class=\"col\" style=\"width:33%\"> <button id=\"remB\" type=\"button\" class=\"b\" tabindex=\"9\"></button> <div style=\"height:2\"></div> <div>remote</div> </div> </div> <div class=\"hf row\"> <div class=\"hole\"></div> <div>STUDIO H SOFTWARE</div> <div class=\"hole\"></div> </div> </div> </body> <script> var baseAddr = 0x02BD80; var rem = true; var req; var c_pset = document.getElementById(\"current_preset\"); var r_pset = document.getElementById(\"recall_preset\"); var c_name = document.getElementById(\"current_name\"); var r_name = document.getElementById(\"recall_name\"); var names = []; r_pset.onchange = rOnChange; c_name.onchange = cnameOnChange; document.getElementById(\"sB\").onclick = sBOnClick; document.getElementById(\"rB\").onclick = rBOnClick; document.getElementById(\"lB\").onclick = lBOnClick; document.getElementById(\"nB\").onclick = nBOnClick; document.getElementById(\"remB\").onclick = remBOnClick; document.getElementById(\"v3\").checked = true;  send(\"readmemory?addr=\" + baseAddr.toString(16) + \"&length=6\"); var data = JSON.parse(req.responseText)[0].data; if (data != \"425543484c41\") { for (var i=1; i<31; i++){ writeName(i,\"\"); names[i]=\"\"; } send(\"writememory?addr=\" + baseAddr.toString(16) + \"&data=425543484C41\"); writePreset(1); } else { for (var i=1; i<31; i++){ names[i]=readName(i); } }  c_pset.value = readPreset(); cOnChange(); r_pset.value = c_pset.value; rOnChange();   function sBOnClick() { send(\"savepreset?preset=\" + r_pset.value); names[r_pset.value] = names[c_pset.value]; writeName(r_pset.value,names[r_pset.value]); rOnChange(); }  function rBOnClick() { if (c_pset.value != r_pset.value) { c_pset.value = r_pset.value; cOnChange(); } }  function lBOnClick() { var pset = c_pset.value; pset = pset - 1; if (pset < 1) { pset = pset + 30; } pset = pset % 31; c_pset.value = pset; r_pset.value = pset; cOnChange(); rOnChange(); } function nBOnClick() { var pset = c_pset.value; pset = pset % 30 + 1; c_pset.value = pset; r_pset.value = pset; cOnChange(); rOnChange(); } function remBOnClick() { rem = !rem; if (rem){ send(\"remoteenable\"); } else { send(\"remotedisable\"); } }  function cOnChange(){ c_name.value = names[c_pset.value]; send(\"recallpreset?preset=\" + c_pset.value); writePreset(c_pset.value); }  function rOnChange(){ r_name.value = names[r_pset.value]; }  function cnameOnChange(){ names[c_pset.value] = c_name.value; writeName(c_pset.value,names[c_pset.value]); rOnChange(); }  function send(url) { var version = \"\"; if (document.getElementById('v2').checked) { version = \"v2/\"; } req = new XMLHttpRequest(); req.open(\"GET\", \"http://192.168.0.1/\" + version + url,false); req.send(null); }  function writePreset(pset){ var pStr = pset.toString(16); if (pStr.length == 1) pStr = \"0\" + pStr; send(\"writememory?addr=\" + (baseAddr + 6).toString(16) + \"&data=\" + pStr); }  function readPreset(){ send(\"readmemory?addr=\" + (baseAddr + 6).toString(16) + \"&length=1\"); return(parseInt(JSON.parse(req.responseText)[0].data)); }  function readName(pset){ send(\"readmemory?addr=\" + (baseAddr + 7 + ((pset - 1) * 21)).toString(16) + \"&length=21\"); return hexToText(JSON.parse(req.responseText)[0].data); }  function writeName(pset,name){ var addr = baseAddr + 7 + ((pset - 1) * 21); send(\"writememory?addr=\" + addr.toString(16) + \"&data=\" + textToHex(name)); }  function hexToText(hex) { var result = \"\"; if (hex != \"\"){ var code = parseInt(hex.substring(0,2),16); if (code == 0) { return result; } else if (code > 19) { result = String.fromCharCode(code) + hexToText(hex.substring(2,hex.length)); } } return result; }  function textToHex(text) { var result = \"\"; if (text != \"\"){ var i; for (i = 0; i < text.length; i++){ result = result + text[i].charCodeAt(0).toString(16); } } return result + \"00\"; } </script> </html>";
String setupPageString = "<html><head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head>Hello Setup Page</html>";

void framEnableWrite(){
    SPI.beginTransaction(spiSettings);
    digitalWrite(SPI_CS, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WREN); // send write enable OPCODE
    digitalWrite(SPI_CS, HIGH); //Set CS high to de-select
    SPI.endTransaction();
}

void framWrite(int addr, uint8_t value){
    //Address on the MB85RS2MTA is 18 bits total, requiring 3 bytes. 
    uint8_t addr_byte_upper = (0x3F0000 & addr) >>16;
    uint8_t addr_byte_middle = (0x00FF00 & addr) >>8;
    uint8_t addr_byte_lower = (0x0000FF & addr);
    framEnableWrite();
    SPI.beginTransaction(spiSettings);
    digitalWrite(SPI_CS, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_WRITE); // send write enable OPCODE
    SPI.transfer(addr_byte_upper); // send address upper byte
    SPI.transfer(addr_byte_middle); // send address middle byte
    SPI.transfer(addr_byte_lower); // send address lower byte
    SPI.transfer(value); // send value
    digitalWrite(SPI_CS, HIGH); //Set CS high to de-select
    SPI.endTransaction();
}

void framWriteHexString(int addr, String value){
    //Address on the MB85RS2MTA is 18 bits total, requiring 3 bytes. 
    uint8_t addr_byte_upper = (0x3F0000 & addr) >>16;
    uint8_t addr_byte_middle = (0x00FF00 & addr) >>8;
    uint8_t addr_byte_lower = (0x0000FF & addr);
    framEnableWrite();
    SPI.beginTransaction(spiSettings);
    digitalWrite(SPI_CS, LOW); //Set CS low to select chip
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
    digitalWrite(SPI_CS, HIGH); //Set CS high to de-select
    SPI.endTransaction();
}

uint8_t framRead(int addr){
    uint8_t result = 0x00;
    //Address on the MB85RS2MTA is 18 bits total, requiring 3 bytes. 
    uint8_t addr_byte_upper = (0x3F0000 & addr) >>16;
    uint8_t addr_byte_middle = (0x00FF00 & addr) >>8;
    uint8_t addr_byte_lower = (0x0000FF & addr);
    SPI.beginTransaction(spiSettings);
    digitalWrite(SPI_CS, LOW); //Set CS low to select chip
    SPI.transfer(FRAM_READ); // send write enable OPCODE
    SPI.transfer(addr_byte_upper); // send address upper byte
    SPI.transfer(addr_byte_middle); // send address middle byte
    SPI.transfer(addr_byte_lower); // send address lower byte
    result = SPI.transfer(0); // read byte
    digitalWrite(SPI_CS, HIGH); //Set CS high to de-select
    SPI.endTransaction();
    return result;
}



///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = "BUCHLA200E";        // your network SSID (name)
char pass[] = "BUCHLA200E";    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                // your network key Index number (needed only for WEP)

int led =  LED_BUILTIN;
int status = WL_IDLE_STATUS;
WiFiServer server(80);

void setup() {
  Wire.begin(0x50 | CARD_ADDRESS);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  //while (!Serial) {
  //  ; // wait for serial port to connect. Needed for native USB port only
  //}


  Serial.println("Access Point Web Server");

  pinMode(WIFI_OPEN_PIN,INPUT);
  pinMode(V2VERSION_PIN,INPUT);

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
  if (digitalRead(WIFI_OPEN_PIN)) {
    status = WiFi.beginAP(ssid);
  } else {
    status = WiFi.beginAP(ssid,pass);
  }
  
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

  pinMode(LED_BUILTIN,OUTPUT); //built in LED on MKR1000
  pinMode(SPI_CS, OUTPUT); //SPI CS
  SPI.begin(); //FRAM communications

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
            //Serial.println(""); 
            //Serial.println("URL is: " + urlString); 
            //Default to PRIMO
            v2version = false;
            //Look at version switch
            if (digitalRead(V2VERSION_PIN)){
                v2version = true;
            }
            //Just look for v2 anywhere in the URL
            if (urlString.indexOf("/v2") >= 0) {
                v2version = true;
            } 
            if (urlString.indexOf("/remoteenable") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("Remote Enable Received"); 
              switchToMaster();
              sendRemoteEnable();
              switchToSlave();
            } 
            else if (urlString.indexOf("/remotedisable") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("Remote Disable Received"); 
              switchToMaster();
              sendRemoteDisable();
              switchToSlave();
            } 
            else if (urlString.indexOf("/savepreset?preset") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("Save Preset Received"); 
              int len = urlString.indexOf(" HTTP");
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
            else if (urlString.indexOf("/recallpreset?preset") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("Recall Preset Received"); 
              int len = urlString.indexOf(" HTTP");
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
            else if (urlString.indexOf("/midinoteon?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Note On Received"); 
              int len = urlString.indexOf(" HTTP");
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
            else if (urlString.indexOf("/midinoteoff?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Note Off Received");         
              int len = urlString.indexOf(" HTTP");
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
            else if (urlString.indexOf("/midiclockstart") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Clock Start Received"); 
              switchToMaster();
              sendMidiClockStart();
              switchToSlave();
            }
            else if (urlString.indexOf("/midiclockstop") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Clock Stop Received");              
              switchToMaster();
              sendMidiClockStop();
              switchToSlave();
            }
            else if (urlString.indexOf("/midiclock") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Clock Received"); 
              switchToMaster();
              sendMidiClock();
              switchToSlave();
            }
            else if (urlString.indexOf("/midifinetune?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Fine Tune Received"); 
              int len = urlString.indexOf(" HTTP");
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
            else if (urlString.indexOf("/midibend?mask") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("MIDI Bend Received"); 
              int len = urlString.indexOf(" HTTP");
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
            else if (urlString.indexOf("/getpresets?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/json",-1); //-1 is chunked encoding
              //Serial.println("Get Presets Received"); 
              //Parse module address and optional length parameter (number of bytes in each line of the result data).
              int len = urlString.indexOf(" HTTP");
              int amploc = urlString.indexOf('&');
              if (amploc==-1) amploc=len;
              int address = (int)strtol(urlString.substring(amploc - 2, amploc).c_str(), nullptr, 16);
              int eqloc = urlString.indexOf('=',amploc);
              int line_length = 256; 
              if (eqloc > -1) {
                  line_length = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              }
              //Serial.println(address,HEX);
              ringBuffer.flush();
              write_i2c_to_fram = true; //Cache incoming i2c to FRAM 
              switchToMaster(); //Send preset backup request to module
              sendBackupPresets(address); 
              switchToSlave();
              bool done = false;
              unsigned long lastTime = millis();
              write_counter=0; //initialize fram write locations
              fram_address=0; //initialize fram write locations
              //Begin the message
              String resultString = "{\r\n";
              resultString = resultString + "\"module_address\": \"0x" + String(address,HEX) + "\",\r\n";
              resultString = resultString + "\"data\": \"";
              while (!done){  
                  while (Wire.available() > 0) {
                      lastTime = millis();
                  }
                  //Check to see if bytes are still arriving from Wire
                  unsigned long now = millis();
                  if ((now - lastTime) >= 1000) {
                      //Serial.printlnf("%lu", now);
                      done = true;
                  }
              }
              //Read data from FRAM cache.
              fram_address=0;
              while (write_counter > fram_address){
                char hexString[2];
                sprintf(hexString,"%02x",framRead(fram_address));
                resultString = resultString + String(hexString);
                if (resultString.length() >= line_length){
                    client.println(resultString.length(),HEX);
                    client.println(resultString);
                    resultString = "";
                }
                fram_address++;
              }
              //Finish the message.
              if (resultString.length() > 0) {
                client.println(resultString.length(),HEX);
                client.println(resultString);
              }
              resultString = "\"\r\n}";
              client.println(resultString.length(),HEX);
              client.println(resultString);
              client.println("0");
              client.println();
              write_i2c_to_fram = false; //Stop Caching incoming i2c to FRAM 
            } else if (urlString.indexOf("/setpresets?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("Set Presets Received"); 
              int len = urlString.indexOf(" HTTP");
              int eqloc = urlString.indexOf('=');
              int module_address = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
              module_address = module_address & 0x7F;
              ringBuffer.flush();
              write_i2c_to_fram = false;
              if (module_address != 0x00) {
                  read_counter = 0; //Initialize counter for subsequent I2C READs from master.
                  switchToMaster(); //Send preset restore request to module
                  sendRestorePresets(module_address); 
                  switchToSlave(); //Switch to slave to receive the read requests.
              }
            } else if (urlString.indexOf("/readmemory?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",-1); //-1 means chunked
              Serial.println("Read Memory Received"); 
              //cb(cbArg, 0, 200, "application/json", nullptr);
              int len = urlString.indexOf(" HTTP");
              int amploc = urlString.indexOf('&');
              if (amploc==-1) amploc=len;
              int eqloc = urlString.indexOf('=');
              int addr = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              eqloc = urlString.indexOf('=',amploc);
              int length = 1; 
              if (eqloc > -1) {
                  length = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
              }
              String dataString = "";
              String tmp="";
              int offset = 0;
              dataString = "[{\"addr\": \"" + String(addr,HEX) + "\",\"data\": \"";
              for (int i = 0; i<length; i++){
                tmp = String(framRead(addr + offset),HEX);
                if (tmp.length() == 1) tmp = "0" + tmp;
                dataString = dataString + tmp;
                offset++; 
                if (i == (length - 1)) {
                  dataString = dataString + "\"}]";
                  client.println(dataString.length(),HEX);
                  client.println(dataString);
                } else if ( offset % 128 == 0 ) {
                  addr = addr + offset;
                  dataString = dataString + "\"},{\"addr\": \"" + String(addr,HEX) + "\",\"data\": \"";
                  client.println(dataString.length(),HEX);
                  client.println(dataString);
                  offset = 0;
                  dataString = "";
                }
              }
              client.println("0");
              client.println();
            } else if (urlString.indexOf("/writememory?addr") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              Serial.println("Write Memory Received"); 
              int len = urlString.indexOf(" HTTP");
              int amploc = urlString.indexOf('&');
              if (amploc==-1) amploc=len;
              int eqloc = urlString.indexOf('=');
              int addr = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
              eqloc = urlString.indexOf('=',amploc);
              String data_string = "00"; 
              if (eqloc > -1) {
                  data_string = urlString.substring(eqloc + 1, len);
              }
              framWriteHexString(addr,data_string);
            } else if (urlString.indexOf("/writememory") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              //Serial.println("Write Memory Received");              
              fram_address = 0;
              JsonToken* token = new JsonToken();
              getJsonToken(token, client);
              while (token->data != "") {
                  if (token->data ==  "addr") 
                  {
                      //This parameter carries the memory address for the data.
                      getJsonToken(token, client);
                      fram_address = (int)strtol((token->data).c_str(), nullptr, 16);
                  } else if (token->data ==  "data") {
                      //This parameter carries the data that will be written to fram.
                      String data_string = "";
                      getJsonToken(token, client);
                      data_string = token->data;
                      if (data_string.length() > 0) {
                          framWriteHexString(fram_address,data_string);
                      }
                  }
                  getJsonToken(token, client);
              } 
              delete token;
            } else if (urlString.indexOf("/test") >= 0) {
              Serial.println("Test Received"); 
              int charcount=0;
              bool done = false;
              unsigned long lastTime = millis();
              while (!done){ 
                  if (client.available() > 0) {
                    char c = client.read(); 
                    charcount++;
                    lastTime = millis();
                  }
                  //Check to see if bytes are still arriving
                  unsigned long now = millis();
                  if ((now - lastTime) >= 100) {
                      done = true;
                  }
              }
              
              String responseString = String(charcount) + " bytes received.";
              Serial.println(responseString);
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
              client.println(responseString);
            } else if (urlString.indexOf("/favicon.ico") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","image/x-icon",0);
              Serial.println("Favicon Received"); 
              //cb(cbArg, 0, 200, "image/x-icon", nullptr);
              //return URL icon here, if desired.
            } else if (urlString.indexOf("/generate_204") >= 0) {
              writeHeader(client,"HTTP/1.1 204 No Content","Content-type:text/plain",0);
              Serial.println("Generate 204 Received"); 
              //cb(cbArg, 0, 204, "text/plain", nullptr);
            } else if (urlString.indexOf("/setup") >= 0) {
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html",setupPageString.length());
              //Serial.println("Returning setup page"); 
              //Write page in 1k chunks. 
              int i = 0;
              String tmp = setupPageString.substring(i,i+1000);
              while (tmp != ""){
                client.print(tmp);
                i = i + 1000;
                tmp = setupPageString.substring(i,i+1000);
              }
              client.println();   // The HTTP response ends with another blank line:
            } else {
              //Serial.println("Returning homepage");
              //Serial.println(homepageString);
              writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html",homepageString.length());
              //Write page in 1k chunks. 
              int i = 0;
              String tmp = homepageString.substring(i,i+1000);
              while (tmp != ""){
                client.print(tmp);
                i = i + 1000;
                tmp = homepageString.substring(i,i+1000);
              }
              client.println();   // The HTTP response ends with another blank line:
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
  digitalWrite(LED_BUILTIN, LOW); //used to show i2c activity
}

void receiveEvent(int howMany) {
    
    //Serial.print(Wire.available()); Serial.print(" "); Serial.println(ringBuffer.bytesFree());
    while (Wire.available() > 0) {       
        uint8_t data = Wire.read();
        Serial.println(data,HEX);
        if (write_i2c_to_fram) {
          //Cache to FRAM because MKR1000 is too slow to process data using a ring buffer.
          framWrite(write_counter,data);
          write_counter++;
        } else {
          //Assume room in buffer to avoid blocking.
          ringBuffer.write(data); //Save into ring buffer.
        }   
    }
}
void requestEvent() {
    //Wire.cpp modified so that this handler is called on every byte requested.
    //Module reads presets here during preset restore. The module first sends an EEPROM memory
    //address, then issues a (maybe large) number of byte read requests. 
    //The number of bytes requested varies by module. 292e requests 8 bytes per preset, and 291e
    //requests 253 bytes per preset. 251e is over 2k. When the module is done reading, it issues
    //a STOP and this handler will not be called until the next time a restore is requested.

    if (ringBuffer.bytesQueued() > 0){
        //This is the first read following address write from the module.
        //The master wrote two or three bytes for the memory address, and we now retrieve it
        //from the rxBuffer. MSB is sent first, LSB second.
        //Typically this happens once at the beginning of each preset.
        read_counter = 0;
        uint8_t addr_byte_upper = 0x00;
        uint8_t addr_byte_middle = 0x00;
        uint8_t addr_byte_lower = 0x00;

        while (ringBuffer.bytesQueued() > 3) {
            ringBuffer.read(); //Only three bytes supported. If buffer has more, clear it out.
        }
        if (ringBuffer.bytesQueued() == 1){
            addr_byte_lower = ringBuffer.read();
        } else if (ringBuffer.bytesQueued() == 2) {
            addr_byte_middle = ringBuffer.read();
            addr_byte_lower = ringBuffer.read();
        } else if (ringBuffer.bytesQueued() == 3) {
            addr_byte_upper = ringBuffer.read();
            addr_byte_middle = ringBuffer.read();
            addr_byte_lower = ringBuffer.read();
        } 
        fram_address = 0;
        fram_address = (addr_byte_upper) << 16;
        fram_address = fram_address | (addr_byte_middle) << 8;
        fram_address = fram_address | addr_byte_lower;
    } 
    //Bypass Wire buffer and write byte directly to I2C.
    sercom2.sendDataSlaveWIRE(framRead(fram_address + read_counter));
    //Wire.write(framRead(fram_address + read_counter));
    read_counter++;
    readTime = millis(); 
    digitalWrite(LED_BUILTIN, HIGH);
}

void switchToMaster(){
  delayMicroseconds(10);
  Wire.end();
  Wire.begin();
}

void switchToSlave(){
    delayMicroseconds(10);
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

void sendSavePreset(byte preset) {
    //Save Preset
    //Preset=0-29
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0x01);
        Wire.write(preset); 
    } else {
        Wire.write(0x04);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x02);
        Wire.write(preset);   
    }
    Wire.endTransmission();
}

void sendRecallPreset(byte preset) {
    //Recall Preset
    //Preset=0-29
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0x00);
        Wire.write(preset); 
    } else {
        Wire.write(0x04);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x01);
        Wire.write(preset);   
    }
    Wire.endTransmission();
}

void sendBackupPresets(byte address) {
    //Fetch presets from specified module
    //Address 0x44=291e.
    Wire.beginTransmission(0);  
    if (v2version) {
        Wire.write(0x2D);
        Wire.write(address); //Module address
        Wire.write(0x00); //Card memory address LSB
        Wire.write(0x00); //Card memory address MSB
        Wire.write(CARD_ADDRESS); //Card address lower byte. Upper byte is always 0x50. 
    } else {
        Wire.write(0x07);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x04);
        Wire.write(address); //Module address
        Wire.write(CARD_ADDRESS); //Card address lower byte. Upper byte is always 0x50. 
        Wire.write(0x00); //Card memory address LSB
        Wire.write(0x00); //Card memory address MSB
    }
    Wire.endTransmission();
}

void sendRestorePresets(byte address) {
    //Request specified module to restore presets.
    //Address 0x44=291e.
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0x2E);
        Wire.write(address); //Module address
        Wire.write(0x00); //Card memory address LSB
        Wire.write(0x00); //Card memory address MSB
        Wire.write(CARD_ADDRESS); //Card address lower byte. Upper byte is always 0x50. 
    } else {
        Wire.write(0x07);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x05);
        Wire.write(address); //Module address
        Wire.write(CARD_ADDRESS); //Card address lower byte. Upper byte is always 0x50. 
        Wire.write(0x00); //Card memory address LSB
        Wire.write(0x00); //Card memory address MSB    
    }
    Wire.endTransmission();
}

void sendMidiNoteOn(byte mask, byte note, byte velo){
    //Send MIDI note ON event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Note 0=C-1, 127=G9.
    //Velo=0-255.
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0x90 | mask);
        Wire.write(note);
        Wire.write(velo);
    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22); //addr
        Wire.write(0x0F);
        Wire.write(0x90 | mask);
        Wire.write(0x00);
        Wire.write(note);
        Wire.write(velo);
        Wire.write(0x00);   
    }
    Wire.endTransmission();
}

void sendMidiNoteOff(byte mask, byte note, byte velo){
    //Send MIDI note OFF event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Note 0=C-1, 127=G9.
    //Velo=0-255.
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0x80 | mask);
        Wire.write(note);
        Wire.write(velo);
    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22);//addr
        Wire.write(0x0F);
        Wire.write(0x80 | mask);
        Wire.write(0x00);
        Wire.write(note);
        Wire.write(velo);
        Wire.write(0x00);
    }
    Wire.endTransmission();
}

void sendMidiClockStart(){
    //Send MIDI clock start event.
    Wire.beginTransmission(0);
    if (v2version) {

    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22);//addr
        Wire.write(0x0F);
        Wire.write(0xFA);
        Wire.write(0x00);
        Wire.write(0x00); //not sure what this is
        Wire.write(0x00);
        Wire.write(0x00);      
    }
    Wire.endTransmission();
}

void sendMidiClockStop(){
    //Send MIDI clock stop event.
    Wire.beginTransmission(0);
    if (v2version) {

    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22);//addr
        Wire.write(0x0F);
        Wire.write(0xFC);
        Wire.write(0x00);
        Wire.write(0x00); //not sure what this is
        Wire.write(0x00);
        Wire.write(0x00);   
    }
    Wire.endTransmission();
}

void sendMidiClock(){
    //Send MIDI clock event. 24 per beat required?
    Wire.beginTransmission(0);
    if (v2version) {

    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22);//addr
        Wire.write(0x0F);
        Wire.write(0xF8);
        Wire.write(0x00);
        Wire.write(0x00); //not sure what this is
        Wire.write(0x00);
        Wire.write(0x00);   
    }
    Wire.endTransmission();
}

void sendMidiFineTune(byte mask, byte tune){
    //Send MIDI Fine Tune event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Tune 0x00=-49, 0x32=An, 0x63=49
    Wire.beginTransmission(0);
    if (v2version) {
        Wire.write(0xB0 | mask);
        Wire.write(0x1F);
        Wire.write(tune);
    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22);//addr
        Wire.write(0x0F);
        Wire.write(0xB0 | mask);
        Wire.write(0x00);
        Wire.write(0x1F);
        Wire.write(tune);
        Wire.write(0x00);    
    }
    Wire.endTransmission();
}

void sendMidiBend(byte mask, byte bend_lsb, byte bend_msb){
    //Send MIDI Bend.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //bend_msb 0x00=min bend, 0x40=no bend, 0x7F=max bend.
    //bend_lsb=0x00-0x7F fine tune of bend.
    Wire.beginTransmission(0); 
    if (v2version) {
        Wire.write(0xE0 | mask);
        Wire.write(bend_lsb);
        Wire.write(bend_msb);
    } else {
        Wire.write(0x08);
        Wire.write(0x00);
        Wire.write(0x22);//addr
        Wire.write(0x0F);
        Wire.write(0xE0 | mask);
        Wire.write(0x00);
        Wire.write(bend_lsb);
        Wire.write(bend_msb);
        Wire.write(0x00);   
    }
    Wire.endTransmission();
}

void getJsonToken(JsonToken* token, WiFiClient client) {
    bool done = false;
    char begin_char = '\"'; 
    char end_char = '\"';
    token->data = "";
    unsigned long lastTime = millis();
    while ((token->status == 0) && (!done)) {
      if (client.available()){
        char s = client.read();
        if (s == begin_char){
            token->status = 1;
        }
        lastTime = millis();
      }
      //Check to see if bytes are still arriving
      unsigned long now = millis();
      if ((now - lastTime) >= 100) {
          done = true;
      }
    } 
  
    while ((token->status == 1) && (!done) && ((token->data).length() < 256)) {
      if (client.available()){
        char s = client.read();
        if (s == end_char){
            token->status = 2;
        } else {
            token->data = token->data + s;
        }
        lastTime = millis();
      }

      //Check to see if bytes are still arriving
      unsigned long now = millis();
      if ((now - lastTime) >= 100) {
          done = true;
      }
    }
    token->status = 0; //ready for next time
};


void writeHeader(WiFiClient client, String statusString, String contentType, int contentLength) {
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println(statusString);
  client.println(contentType);
  client.println("Connection: Keep-Alive");
  client.println("Pragma: no-cache");
  client.println("Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0");
  client.println("Access-Control-Allow-Origin: *");
  if (contentLength>0) {
    client.print("Content-Length: ");
    client.println(contentLength);
  } else if (contentLength < 0) {
    client.println("Transfer-Encoding: chunked");
  }
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

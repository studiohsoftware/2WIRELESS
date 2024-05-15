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
#include <usbh_midi.h>
#include <usbhub.h>
#include <MIDIUSB.h>

//#define DEBUG
//#define DEBUG_HTTP
//#define DEBUG_MIDI
//#define DEBUG_PRESETS
//#define DEBUG_USB
#define CARD_ADDRESS 0x00 //This ends up meaning that our I2C address is 0x50.
#define BUFFER_SIZE 2000 //Ring buffer size for incoming I2C. 
#define FRAM_WREN 0x06 //FRAM WRITE ENABLE COMMAND
#define FRAM_WRITE 0x02 //FRAM WRITE COMMAND
#define FRAM_READ 0x03 //FRAM READ COMMAND
#define SPI_CS 4 //FRAM Chip Select
#define WIFI_OPEN_PIN 3 //Choose WPA or Open
#define V2VERSION_PIN 2 //Choose PRIMO or V2
#define SSID_DEFAULT "BUCHLA200E"
#define PASS_DEFAULT "BUCHLA200E"
#define USB_MODE_DEVICE 1
#define USB_MODE_HOST 0
//RESERVED FRAM ADDRESSES 0x02BD19-0x2C0FF
#define SSID_ADDR 0x02BD19 //32 byte locaton for SSID string
#define PASS_ADDR 0x02BD3A //64 byte location for WPA password string.
#define CURRENT_PRESET_ADDR 0x02BE86 //Byte in FRAM memory to track current preset number
#define PRESET_NAME_ADDR 0x02BE87 //Thirty 21 character preset names
#define MIDI_CFG_MASK_ADDR 0x02BE00 //16 bytes for bus masks
#define MIDI_CFG_POLY_ADDR 0x02BE10 //16 bytes for poly 
#define MIDI_CFG_FINE_ADDR 0x02BE20 //16 bytes for fine tune (0x32=An)
#define MIDI_CFG_TRAN_ADDR 0x02BE30 //16 bytes for transpose (0x32=No transpose)
#define MIDI_CFG_VELO_ADDR 0x02BE40 //2 bytes for 16 bools velo y/n.
#define USB_MODE_ADDR 0x02BE42 //1 byte for USB MODE 1=DEVICE 0=HOST
#define POLL_ON_START_ADDR 0x02BE4A //1 byte for POLL ON START 1=POLL 0=NO POLL
#define PROGRAM_CHANGE_ENABLE_ADDR 0x02BE4B //1 byte for enabling preset change on program change. 1=ENABLE 0=DISABLE
#define SEND_MIDI_TO_USB_ADDR 0x02BE4C //1 byte to enable send of MIDI from bus to USB out. 1=ENABLE 0=DISABLE
#define INIT_ADDR 0x02BE43 //six bytes set to DONALD once 1st config is done.
//END RESERVED FRAM ADDRESSES

//USB Host Convenience Functions
#define Is_uhd_in_received0(p)                    ((USB->HOST.HostPipe[p].PINTFLAG.reg&USB_HOST_PINTFLAG_TRCPT0) == USB_HOST_PINTFLAG_TRCPT0)
#define Is_uhd_in_received1(p)                    ((USB->HOST.HostPipe[p].PINTFLAG.reg&USB_HOST_PINTFLAG_TRCPT1) == USB_HOST_PINTFLAG_TRCPT1)
#define uhd_ack_in_received0(p)                   USB->HOST.HostPipe[p].PINTFLAG.reg = USB_HOST_PINTFLAG_TRCPT0
#define uhd_ack_in_received1(p)                   USB->HOST.HostPipe[p].PINTFLAG.reg = USB_HOST_PINTFLAG_TRCPT1
#define uhd_byte_count0(p)                        usb_pipe_table[p].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT
#define uhd_byte_count1(p)                        usb_pipe_table[p].HostDescBank[1].PCKSIZE.bit.BYTE_COUNT
#define Is_uhd_in_ready0(p)                       ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_BK0RDY) == USB_HOST_PSTATUS_BK0RDY)  
#define uhd_ack_in_ready0(p)                       USB->HOST.HostPipe[p].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY
#define Is_uhd_in_ready1(p)                       ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_BK1RDY) == USB_HOST_PSTATUS_BK1RDY)  
#define uhd_ack_in_ready1(p)                       USB->HOST.HostPipe[p].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK1RDY
#define uhd_current_bank(p)                       ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_CURBK) == USB_HOST_PSTATUS_CURBK)  

// for debugging
#ifdef DEBUG
#define DEBUG_PRINT(str) Serial1.print(str)
#define DEBUG_PRINTLN(str) Serial1.println(str)
#define DEBUG_PRINTHEX(str) Serial1.print(str, HEX)
#define DEBUG_PRINTHEXLN(str) Serial1.println(str, HEX)
#else
#define DEBUG_PRINT(str)
#define DEBUG_PRINTLN(str)
#define DEBUG_PRINTHEX(str)
#define DEBUG_PRINTHEXLN(str)
#endif

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

volatile int read_counter=0; //Used to auto increment read addresses during I2C READ from master.
volatile int write_counter=0; //USed to auto increment write addresses during I2C WRITE from master.
volatile int fram_address=0; //This is global only because it must span OnRequest calls during I2C READ from master. 
volatile bool startup = true; //Used to free 206e when starting up.
volatile bool write_i2c_to_fram = false; //Cache incoming i2c to FRAM.
unsigned long readTime = 0; //used to detect idle to free up 206e at start.

bool isUsbDevice = true; //Select between USB Device and USB Host
midiEventPacket_t midiMessage; //Convenience structure for carrying MIDI data
RingBuffer usbHostBuffer; //USB Host uses this to decouple interrupt from loop().
USBHost UsbH; //Host object in case we are running as USB Host
USBHub Hub(&UsbH); //Host object
USBH_MIDI  Midi(&UsbH); //Host object
//SAMD21 datasheet pg 836. ADDR location needs to be aligned. 
uint8_t bufBk0[64] __attribute__ ((aligned (4))); //Bank0 used by USB Host
uint8_t bufBk1[64] __attribute__ ((aligned (4))); //Bank1 used by USB Host
bool doPipeConfig = false; //Used to config USB Host pipe after enumeration.
bool usbConnected = false; //Used to throttle activity on USB Host.

int MIDI_EVENT = 0x0800000F; //Leading I2C bytes for 200e MIDI message.
const uint8_t  MIDI_CLK[4]    = {0x0F, 0xF8, 0, 0}; //used to send 252e clock to USB
const uint8_t  MIDI_CLK_START[4] = {0x0F, 0xFA, 0, 0};
const uint8_t  MIDI_CLK_CONT[4]  = {0x0F, 0xFB, 0, 0};
const uint8_t  MIDI_CLK_STOP[4]  = {0x0F, 0xFC, 0, 0};
uint8_t midiUsbData[4] = {0x00, 0x00, 0x00, 0x00}; //used to decouple incoming I2C from interrupt.

uint8_t mask[16] = {0xF,0xF,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //Channels 1 and 2 all buses by default
uint8_t tran[16] = {0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32}; //No tran default
uint8_t fine[4] = {0x32,0x32,0x32,0x32}; //An default per bus
uint8_t busMask[4] = {0x8,0x4,0x2,0x1}; //Mask for each bus used for fine tuning
uint8_t poly[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //no poly by default
uint32_t polyNotes[16] = {0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80}; //stores poly notes for note off events
uint8_t polyAvail[16] = {0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF};
bool velo[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}; //use velocity by default

bool v2version=false; //used to support pre PRIMO firmware.
SPISettings spiSettings(12000000, MSBFIRST, SPI_MODE0); //MKR1000 max is 12MHz. FRAM chip max is 40MHz.
String homepageString = "<html> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <style> .p { display: flex; flex-direction: column; background-color:lightgray; color:darkblue; font-family:Arial; font-weight:bold; letter-spacing: 2px; height: 218px; } .hf { font-size:12px; padding: 10px; border: 1px solid darkblue; } .r { font-style:italic; font-size:20px; padding: 10px; border-left: 1px solid darkblue; border-right: 1px solid darkblue; height: 50px; } .row { display: flex; flex-direction: row; justify-content: space-around; align-items: center; } .col { display: flex; flex-direction: column; justify-content: space-around; align-items: center; font-size:14px; padding-inline: 4px;} .screw { display: flex; justify-content: center; align-content: center; flex-direction: column; height: 18px; width: 18px; background-color: #999; border-radius: 50%; color: #444; font-size:30px; border: 1px solid black; } .b { border-radius: 50%; height:40px; width:40px; background-color: #777;  } .d { height: 50px; background-color: white; padding: 0px; margin: 0px; border: 1px solid black; } .dx { height: 50px; background-color: white; padding: 0px; margin: 0px; border: 0px; } .dc { font-family: Courier New; font-weight:bold; background-color: #cfc; height: 50px; border: 0px; } .hole { height: 10px; width: 10px; background-color: #000; border-radius: 50%; display: inline-block; } </style> </head> <body> <div class=\"p\"> <div class=\"hf row\"> <div class=\"screw\">+</div> <div>PRESET &nbsp; MANAGER</div> <div class=\"screw\">+</div> </div> <div class=\"r row\" style=\"border-bottom: 1px solid darkblue;\"> <div class=\"col\"> <button id=\"sB\" type=\"button\" class=\"b\" style=\"background-color: #36f;\" tabindex=\"1\"></button> <div style=\"height:2\"></div> <div>store</div> </div> <div class=\"d row\"> <div class=\"dx col\" > <input id=\"current_preset\" class=\"dc\" style=\"width:21px;background-color: #afa;\" disabled> <input class=\"dc\" disabled style=\"width:21px;background-color: #afa;\"> </div> <div class=\"dx col\" > <input id=\"current_name\" class=\"dc\" maxlength=\"20\" tabindex=\"2\"> <input id=\"recall_name\" class=\"dc\" disabled style=\"background-color: #afa;\"> </div> <div class=\"dx col\" > <input class=\"dc\" disabled style=\"width:40px;background-color: #afa;\"> <input id=\"recall_preset\" class=\"dc\" style=\"width:40px;\" type=\"number\" min=\"1\" max=\"30\" tabindex=\"3\"> </div> </div> <div class=\"col\"> <button id=\"rB\" type=\"button\" class=\"b\" style=\"background-color: #36f;\" tabindex=\"4\"></button> <div style=\"height:2\"></div> <div>recall</div> </div> </div> <div class=\"r row\"> <div class=\"row\" style=\"width:200px\"> <div class=\"col\"> <button id=\"lB\" type=\"button\" class=\"b\" tabindex=\"5\"></button> <div style=\"height:2\"></div> <div>last</div> </div> <div class=\"col\"> <button id=\"nB\" type=\"button\" class=\"b\" tabindex=\"6\"></button> <div style=\"height:2\"></div> <div>next</div> </div> </div> <div class=\"col\" style=\"align-items: flex-start;width:33%\"> <div class=\"row\" style=\"align-items: flex-start;\"> <input type=\"radio\" id=\"v3\" name=\"version\" value=\"v3\" tabindex=\"7\" hidden> <label for=\"v3\" hidden>primo</label> </div> <div class=\"row\" style=\"align-items: flex-start;\"> <input type=\"radio\" id=\"v2\" name=\"version\" value=\"v2\" tabindex=\"8\" hidden> <label for=\"v2\" hidden>v2    </label> </div> </div> <div class=\"col\" style=\"width:33%\"> <button id=\"remB\" type=\"button\" class=\"b\" tabindex=\"9\"></button> <div style=\"height:2\"></div> <div>remote</div> </div> </div> <div class=\"hf row\"> <div class=\"hole\"></div> <div>STUDIO H SOFTWARE</div> <div class=\"hole\"></div> </div> </div> </body> <script> var rem = true; var req; var c_pset = document.getElementById(\"current_preset\"); var r_pset = document.getElementById(\"recall_preset\"); var c_name = document.getElementById(\"current_name\"); var r_name = document.getElementById(\"recall_name\"); var names = []; r_pset.onchange = rOnChange; c_name.onchange = cnameOnChange; document.getElementById(\"sB\").onclick = sBOnClick; document.getElementById(\"rB\").onclick = rBOnClick; document.getElementById(\"lB\").onclick = lBOnClick; document.getElementById(\"nB\").onclick = nBOnClick; document.getElementById(\"remB\").onclick = remBOnClick; document.getElementById(\"v3\").checked = true;  readNames();  c_pset.value = readPreset(); cOnChange(); r_pset.value = c_pset.value; rOnChange();   function sBOnClick() { send(\"savepreset?preset=\" + r_pset.value); names[r_pset.value] = names[c_pset.value]; writeName(r_pset.value,names[r_pset.value]); rOnChange(); }  function rBOnClick() { c_pset.value = r_pset.value; cOnChange(); }  function lBOnClick() { var pset = c_pset.value; pset = pset - 1; if (pset < 1) { pset = pset + 30; } pset = pset % 31; c_pset.value = pset; r_pset.value = pset; cOnChange(); rOnChange(); } function nBOnClick() { var pset = c_pset.value; pset = pset % 30 + 1; c_pset.value = pset; r_pset.value = pset; cOnChange(); rOnChange(); } function remBOnClick() { rem = !rem; if (rem){ send(\"remoteenable\"); } else { send(\"remotedisable\"); } }  function cOnChange(){ c_name.value = names[c_pset.value]; send(\"recallpreset?preset=\" + c_pset.value); }  function rOnChange(){ r_name.value = names[r_pset.value]; }  function cnameOnChange(){ names[c_pset.value] = c_name.value; writeName(c_pset.value,names[c_pset.value]); rOnChange(); }  function send(url) { var version = \"\"; if (document.getElementById('v2').checked) { version = \"v2/\"; } req = new XMLHttpRequest(); req.open(\"GET\", \"http://192.168.0.1/\" + version + url,false); req.send(null); }  function readPreset(){ send(\"currentpreset\"); return(parseInt(JSON.parse(req.responseText).currentpreset)); }  function readNames(){ send(\"presetnames\"); var tmp = JSON.parse(req.responseText).presetnames; for (var i=0;i<30;i++){ names[i+1]=tmp[i]; }  }  function writeName(pset,name){ if (name != undefined) { send(\"presetname=\" + name + \"&preset=\" + pset.toString()); } } </script> </html>";
String setupPageString = "<html> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> </head> <style> table,th,td  { border:1px solid black; border-collapse: collapse; } th,td { padding: 1px; } input[type=number] { width: 45px; } </style> <body> <div style=\"display: flex;justify-content: space-between; max-width:245px\"> <label style=\"font-weight:bold;font-size:24px;\">Setup Page</label> <input type=\"button\" id=\"submit\" value=\"Submit\"> </div> <hr> <table id=\"MidiTable\" name=\"MidiTable\"> <tr> <th>Chan</th> <th>A</th> <th>B</th> <th>C</th> <th>D</th> <th>Tran</th> <th>Poly</th> <th>Velo</th> </tr> </table> <hr> <table id=\"BusTable\" name=\"BusTable\"> <tr> <th>Bus</th> <th>Fine</th> </tr> </table> <hr> <table id=\"MidiOptions\" name=\"MidiOptions\"> <tr> <th colspan=\"2\">MIDI Options</th> </tr> <tr> <td> <label for=\"PrgChEnbl\">Receive Program Change</label> </td> <td> <input type=\"checkbox\" id=\"PrgChEnbl\" name=\"PrgChEnbl\"> </td> </tr> <tr> <td> <label for=\"SendMidi\" id=\"SendMidiLabel\">Send 252e Midi Clock</label> </td> <td> <input type=\"checkbox\" id=\"SendMidi\" name=\"SendMidi\"> </td> </tr> </table> <hr> <label for=\"Device\"> <input type=\"radio\" id=\"Device\" name=\"usb\" value=\"1\" onclick=\"deviceOnClick()\">USB Device</label> <label for=\"Host\"> <input type=\"radio\" id=\"Host\" name=\"usb\" value=\"0\" onclick=\"hostOnClick()\">USB Host</label> <hr> <input type=\"checkbox\" id=\"Poll\" name=\"poll\"> <label for=\"Poll\">Poll modules on startup</label> <hr> <label for=\"ssid\">SSID:</label></br> <input type=\"text\" id=\"ssid\" name=\"ssid\" maxlength=\"32\" style=\"width:250;\"><br> <hr> <label for=\"pass\">Password (eight characters minimum):</label></br> <input type=\"text\" id=\"pass\" name=\"pass\" maxlength=\"63\" style=\"width:250;\"><br> <label for=\"pass2\">Re-enter Password:</label></br> <input type=\"text\" id=\"pass2\" name=\"pass2\" maxlength=\"63\" style=\"width:250;\"><br> <label for=\"submit\" id=\"errortext\" style=\"color:red;\"></label> <hr> Firmware v1.8 </body> <script> var req = new XMLHttpRequest(); var s = document.getElementById(\"ssid\"); var pwd = document.getElementById(\"pass\"); var pwd2 = document.getElementById(\"pass2\"); var submit = document.getElementById(\"submit\"); var errortext = document.getElementById(\"errortext\"); var busNames = [\"A\",\"B\",\"C\",\"D\"];  var busTable = document.getElementById(\"BusTable\"); for (var i = 0; i < 4; i++) { var tr = document.createElement('tr'); var td1 = document.createElement('td'); var td2 = document.createElement('td'); var f = document.createElement('input'); f.type = 'number'; f.name = \"f\" + i; f.id = \"f\" + i; f.setAttribute('max','49'); f.setAttribute('min','-49'); td1.appendChild(document.createTextNode(busNames[i])); td2.appendChild(f); tr.appendChild(td1); tr.appendChild(td2); busTable.appendChild(tr); } var fineA = document.getElementById(\"f0\"); var fineB = document.getElementById(\"f1\"); var fineC = document.getElementById(\"f2\"); var fineD = document.getElementById(\"f3\");  var table = document.getElementById(\"MidiTable\"); for (var i = 1; i < 17; i++) { var tr = document.createElement('tr'); var td1 = document.createElement('td'); var td2 = document.createElement('td'); var td3 = document.createElement('td'); var td4 = document.createElement('td'); var td5 = document.createElement('td'); var td6 = document.createElement('td'); var td7 = document.createElement('td'); var td8 = document.createElement('td'); var chan = document.createTextNode(i); var a = document.createElement('input'); a.type = \"checkbox\"; a.name = \"a\" + i; a.id = \"a\" + i; var b = document.createElement('input'); b.type = \"checkbox\"; b.name = \"b\" + i; b.id = \"b\" + i; var c = document.createElement('input'); c.type = \"checkbox\"; c.name = \"c\" + i; c.id = \"c\" + i; var d = document.createElement('input'); d.type = \"checkbox\"; d.name = \"d\" + i; d.id = \"d\" + i; var t = document.createElement('input'); t.type = 'number'; t.name = \"t\" + i; t.id = \"t\" + i; t.setAttribute('max','49'); t.setAttribute('min','-49'); var p = document.createElement('input'); p.type = \"checkbox\"; p.name = \"p\" + i; p.id = \"p\" + i; var v = document.createElement('input'); v.type = \"checkbox\"; v.name = \"v\" + i; v.id = \"v\" + i; td1.appendChild(chan); td2.appendChild(a); td3.appendChild(b); td4.appendChild(c); td5.appendChild(d); td6.appendChild(t); td7.appendChild(p); td8.appendChild(v); tr.appendChild(td1); tr.appendChild(td2); tr.appendChild(td3); tr.appendChild(td4); tr.appendChild(td5); tr.appendChild(td6); tr.appendChild(td7); tr.appendChild(td8); table.appendChild(tr); } submit.onclick = submitOnClick; send('getsetupdata','GET',null); var parsedJson = JSON.parse(req.responseText); s.value = parsedJson.ssid; pwd.value = parsedJson.password; pwd2.value = pwd.value; if (parseInt(parsedJson.poll)==1) { document.getElementById(\"Poll\").checked = true; } if (parseInt(parsedJson.prgChEnbl)==1) { document.getElementById(\"PrgChEnbl\").checked = true; } if (parseInt(parsedJson.sendMidi)==1) { document.getElementById(\"SendMidi\").checked = true; } if (parseInt(parsedJson.usbMode)==1){ document.getElementById(\"Device\").checked = true; disable252eClock(false); } else { document.getElementById(\"Host\").checked = true; disable252eClock(true); } for (var i=0; i < 4; i++){ document.getElementById(\"f\" + i).value = parseInt(parsedJson.buses[i].fine); } for (var i=0; i < 16; i++){ k = i + 1; var mask = parseInt(parsedJson.channels[i].mask); document.getElementById(\"a\" + k).checked = mask&0x8; document.getElementById(\"b\" + k).checked = mask&0x4; document.getElementById(\"c\" + k).checked = mask&0x2; document.getElementById(\"d\" + k).checked = mask&0x1; document.getElementById(\"t\" + k).value = parseInt(parsedJson.channels[i].tran); document.getElementById(\"p\" + k).checked = parseInt(parsedJson.channels[i].poly); document.getElementById(\"v\" + k).checked = parseInt(parsedJson.channels[i].velo); } function send(url,method,content) { req.open(method, \"http://192.168.0.1/\" + url,false); req.send(content); }  function deviceOnClick(){ disable252eClock(false); }  function hostOnClick(){ disable252eClock(true); }  function disable252eClock(disable){ if (disable){ document.getElementById(\"SendMidi\").disabled=true; document.getElementById(\"SendMidiLabel\").style.color = 'gray'; } else { document.getElementById(\"SendMidi\").disabled=false; document.getElementById(\"SendMidiLabel\").style.color = 'black'; } }  function submitOnClick(){ if (pwd.value != pwd2.value) { errortext.innerHTML = \"Passwords do not match.\"; } else if ((pwd.value.length < 8) || (pwd2.value.length < 8)){ errortext.innerHTML = \"Password too short.\"; } else { var usb = \"0\"; if (document.getElementById(\"Device\").checked) { usb = \"1\"; } var poll = \"0\"; if (document.getElementById(\"Poll\").checked) { poll = \"1\"; } var prgChEnbl = \"0\"; if (document.getElementById(\"PrgChEnbl\").checked) { prgChEnbl = \"1\"; } var sendMidi = \"0\"; if (document.getElementById(\"SendMidi\").checked) { sendMidi = \"1\"; } errortext.innerHTML = \"\"; var response = { ssid : s.value, password : pwd.value, usbMode : usb, poll : poll, prgChEnbl : prgChEnbl, sendMidi : sendMidi, buses : [], channels : [] }; var busA = { bus: \"A\", fine: fineA.value.toString() }; response.buses.push(busA); var busB = { bus: \"B\", fine: fineB.value.toString() }; response.buses.push(busB); var busC = { bus: \"C\", fine: fineC.value.toString() }; response.buses.push(busC); var busD = { bus: \"D\", fine: fineD.value.toString() }; response.buses.push(busD);  for (var i = 1; i<17; i++){ var m = 0; if (document.getElementById(\"a\" + i).checked) {m = 0x8;} if (document.getElementById(\"b\" + i).checked) {m = m | 0x4}; if (document.getElementById(\"c\" + i).checked) {m = m | 0x2}; if (document.getElementById(\"d\" + i).checked) {m = m | 0x1}; var pp = 0; if (document.getElementById(\"p\" + i).checked) {pp = 1;} var vv = 0; if (document.getElementById(\"v\" + i).checked) {vv = 1;} var channel = { chan : i.toString(), mask : \"0x\" + m.toString(16), tran : document.getElementById(\"t\" + i).value.toString(), poly : pp.toString(), velo : vv.toString() }; response.channels.push(channel); } var responseString = JSON.stringify(response,undefined,2); req = new XMLHttpRequest(); req.open(\"POST\", \"http://192.168.0.1/postsetupdata\",true); req.setRequestHeader(\"Content-Length\", responseString.length); req.setRequestHeader(\"Content-Type\", \"application/x-www-form-urlencoded\"); req.send(responseString); } }  </script> </html>";
char ssid[33];        // your network SSID (name) max 32 characters plus termination
char pass[64];    // your network password for WPA. Max is 63 plus termination
int keyIndex = 0;                // your network key Index number (needed only for WEP)

int status = WL_IDLE_STATUS;
WiFiServer server(80);

void setup() {
  //Set this stuff up first so we are ready for firmware requests from modules.
  pinMode(WIFI_OPEN_PIN,INPUT);
  pinMode(V2VERSION_PIN,INPUT);
  pinMode(LED_BUILTIN, OUTPUT);      // set the LED pin mode
  pinMode(LED_BUILTIN,OUTPUT); //built in LED on MKR1000
  pinMode(SPI_CS, OUTPUT); //SPI CS
  SPI.begin(); //FRAM communications
  Wire.begin(0x50 | CARD_ADDRESS,getSendMidiToUsb()); //Enable GeneralCall to receive MIDI from bus
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  #ifdef DEBUG
  //Initialize serial and wait for port to open:
  Serial1.begin(115200);
  DEBUG_PRINTLN("Access Point Web Server");
  #endif

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    DEBUG_PRINTLN("WiFi shield not present");
    // don't continue
    while (true);
  }

  // by default the local IP address of will be 192.168.1.1
  // you can override it with the following:
  WiFi.config(IPAddress(192, 168, 0, 1));

  initializeFram(); //Store or retrieve MIDI (and other) configuration via FRAM.

  //Read SSID from FRAM
  strcpy(ssid,getSSID().c_str());

  //Read Password from FRAM
  strcpy(pass,getPassword().c_str());

  // print the network name (SSID);
  DEBUG_PRINT("Creating access point named: ");
  DEBUG_PRINTLN(ssid);

  // Create open network. Change this line if you want to create an WEP network:
  //2WIRELESS added pass
  if (digitalRead(WIFI_OPEN_PIN) || (sizeof(pass) < 8)) {
    status = WiFi.beginAP(ssid);
  } else {
    status = WiFi.beginAP(ssid,pass);
  }
  
  if (status != WL_AP_LISTENING) {
    DEBUG_PRINTLN("Creating access point failed");
    // don't continue
    while (true);
  }

  // wait 10 seconds for connection:
  //delay(10000);

  // start the web server on port 80
  server.begin();

  // you're connected now, so print out the status
  printWiFiStatus();

  isUsbDevice = (getUsbMode() && USB_MODE_DEVICE);


  if (isUsbDevice) {
    DEBUG_PRINTLN("Configuring as USB Device");
    MidiUSB.attachInterrupt(onMidiUsbDeviceEvent); //Needed to turn off SOF interrupts in USBCore.cpp
  } else {
    DEBUG_PRINTLN("Configuring as USB Host");
    if (UsbH.Init()) {
      DEBUG_PRINTLN("USB host did not start");
      while (1); //halt
    }
    //USB_SetHandler(&CUSTOM_UHD_Handler);
    delay( 1000 );
  }
  if (digitalRead(V2VERSION_PIN)){
      v2version = true;
  }
  
 //Polling only supported on PRIMO
  if (getPollOnStart() && !v2version) {
    delay(2000);
    //Turns out module initialization only requires the polling complete message.
    sendPollingComplete();
    sendRemoteEnable();  
  }
}


void loop() {
  // compare the previous status to the current status
  if (status != WiFi.status()) {
    // it has changed update the variable
    status = WiFi.status();
    if (status == WL_AP_CONNECTED) {
      byte remoteMac[6];
      // a device has connected to the AP
      DEBUG_PRINT("Device connected to AP, MAC address: ");
      WiFi.APClientMacAddress(remoteMac);
      printMacAddress(remoteMac);
    } else {
      // a device has disconnected from the AP, and we are back in listening mode
      DEBUG_PRINTLN("Device disconnected from AP");
    }
  }
  
  WiFiClient client = server.available();   // listen for incoming clients
  //When web page is served, we return to this with no client. 
  //TO DO: If device was disconnected from AP, then it seems AP needs to be reconstructed here.
  if (client) {                             // if you get a client,
    DEBUG_PRINTLN("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    String urlString = "";                  // string to hold the request URL
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        //SerialDebug.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            DEBUG_PRINTLN(""); 
            DEBUG_PRINTLN("URL is: " + urlString);
            handleWifiRequest(client, urlString);
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
      pollUsbMidi(isUsbDevice);
      digitalWrite(LED_BUILTIN, LOW); //used to show i2c activity
    }
    // close the connection:
    client.stop();
    delayMicroseconds(1000); //stablizes when load is heavy
    DEBUG_PRINTLN("client disconnected");
  } else {
    DEBUG_PRINTLN("No client");
  }
  pollUsbMidi(isUsbDevice);
  digitalWrite(LED_BUILTIN, LOW); //used to show i2c activity
}

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

String getFramString(int addr, int len) {
  String result = "";
  int offset = 0;
  int tmp = framRead(addr + offset); 
  while ((tmp > 31) && (tmp < 0x7F) && (offset < len)) {
    result = result + char(tmp);
    offset++;
    tmp = framRead(addr + offset);
  }
  return result;
}

void setFramString(String value, int addr){
  for (int i=0;i<value.length();i++){
    char c = value.charAt(i);
    framWrite(addr + i,c);
  } 
}

void initializeFram() {
  //Check if already initialized.
  if (getFramString(INIT_ADDR,6) == "DONALD") {
    //Read values from FRAM
    for (int i=0;i<16;i++){
      mask[i] = framRead(MIDI_CFG_MASK_ADDR + i);
      poly[i] = framRead(MIDI_CFG_POLY_ADDR + i);
      tran[i] = framRead(MIDI_CFG_TRAN_ADDR + i);
      velo[i] = getVelo(i); //array of bool
    }  
    for (int i=0;i<4;i++){
      fine[i] = framRead(MIDI_CFG_FINE_ADDR + i);
    }
  } else {
    //Write default values to fram.
    uint16_t velocity = 0x0;
    for (int i=0;i<16;i++){
      framWrite(MIDI_CFG_MASK_ADDR + i,mask[i]);
      framWrite(MIDI_CFG_POLY_ADDR + i,poly[i]);
      framWrite(MIDI_CFG_TRAN_ADDR + i,tran[i]);
      setVelo(i,velo[i]);
    }
    for (int i=0;i<4;i++){
      framWrite(MIDI_CFG_FINE_ADDR + i,fine[i]);
    }
    setUsbMode(USB_MODE_DEVICE); 
    setPollOnStart(0);
    setProgramChangeEnable(0);
    setSendMidiToUsb(0);
    setCurrentPreset(1); //just to have something in there.
    //Mark as initialized for next time.
    setFramString("DONALD", INIT_ADDR);
  } 
}

void setMask(uint8_t chan, uint8_t val){
  //Channel is 0-15
  if (mask[chan] != val){
    mask[chan] = val;
    framWrite(MIDI_CFG_MASK_ADDR + chan,mask[chan]); 
  }
}

void setPoly(uint8_t chan, uint8_t val){
  //Channel is 0-15
  if (poly[chan] != val){
    poly[chan] = val;
    framWrite(MIDI_CFG_POLY_ADDR + chan,poly[chan]); 
  }
  if (val) {
    polyAvail[chan]=0xF; //init to all buses available
    polyNotes[chan] = 0xFFFFFFFF;
  }
}

void setFine(uint8_t bus, uint8_t val){
  //Bus is 0-3
  //Always send so that setup Submit after boot works
  fine[bus] = val;
  framWrite(MIDI_CFG_FINE_ADDR + bus,fine[bus]); 
  sendMidiFineTune(busMask[bus],fine[bus]);
}

void setTran(uint8_t chan, uint8_t val){
  //Channel is 0-15
  if (tran[chan] != val) {
    tran[chan] = val;
    framWrite(MIDI_CFG_TRAN_ADDR + chan,tran[chan]);
  }
}

bool getVelo(uint8_t chan){
  uint16_t velocity = framRead(MIDI_CFG_VELO_ADDR) & 0x00FF;
  velocity = velocity | (framRead(MIDI_CFG_VELO_ADDR + 1) << 8); 
  return velocity & (1<<chan);
}

void setVelo(uint8_t chan, bool val) {
  velo[chan] = val;
  uint16_t velocity = framRead(MIDI_CFG_VELO_ADDR) & 0x00FF;
  velocity = velocity | (framRead(MIDI_CFG_VELO_ADDR + 1) << 8);
  uint16_t shifter = 1 << chan;
  velocity = velocity & ~shifter; //clear the current bit.
  velocity = velocity | (val << chan); //Save the desired bit.
  framWrite(MIDI_CFG_VELO_ADDR,velocity & 0x00FF);
  framWrite(MIDI_CFG_VELO_ADDR + 1,(velocity & 0xFF00)>>8);
}

bool getUsbMode() {
  return (framRead(USB_MODE_ADDR) > 0);
}

void setUsbMode(bool mode){
  framWrite(USB_MODE_ADDR,mode);
}

bool getPollOnStart() {
  return (framRead(POLL_ON_START_ADDR) > 0);
}

void setPollOnStart(bool mode){
  framWrite(POLL_ON_START_ADDR,mode);
}

bool getProgramChangeEnable() {
  return (framRead(PROGRAM_CHANGE_ENABLE_ADDR) > 0);
}

void setProgramChangeEnable(bool mode){
  framWrite(PROGRAM_CHANGE_ENABLE_ADDR,mode);
}

bool getSendMidiToUsb() {
  //return false; //neuter 252e clock. General call locks up when we switch to master.
  return (framRead(SEND_MIDI_TO_USB_ADDR) > 0);
}

void setSendMidiToUsb(bool mode){
  framWrite(SEND_MIDI_TO_USB_ADDR,mode);
}

String getSSID(){
  String result = "";
  int tmp = framRead(SSID_ADDR);
  int offset = 0;
  while ((tmp > 31) && (tmp < 0x7F) && (offset < 32)) {
    result = result + char(tmp);
    offset++;
    tmp = framRead(SSID_ADDR + offset); 
  }
  if (offset == 0) {
    result = SSID_DEFAULT;
  }
  return result;
}

void setSSID(String ssid){
  int max_ssid_length = 32;
  int i = 0;
  while ((i < (ssid.length()) && (i < max_ssid_length))){
    char value = ssid.charAt(i);
    framWrite(SSID_ADDR + i,value);
    i++;
  } 
  framWrite(SSID_ADDR + i,0);
}

uint8_t getCurrentPreset() {
  uint8_t result = 1;
  result = framRead(CURRENT_PRESET_ADDR);
  if (result < 1) result = 1;
  if (result > 30) result = 30;
  return result;
}

void setCurrentPreset(uint8_t preset) {
  //preset is 1-30.
  if (preset < 1) preset = 1;
  if (preset > 30) preset = 30;
  framWrite(CURRENT_PRESET_ADDR,preset);
}

String getPresetName(uint8_t preset) {
  //preset is 1-30.
  if (preset < 1) preset = 1;
  if (preset > 30) preset = 30;
  String result = "";
  int addr = PRESET_NAME_ADDR + (21 * (preset - 1));
  int tmp = framRead(addr); 
  int offset = 0;
  while ((tmp > 31) && (tmp < 0x7F) && (offset < 21)) {
    result = result + char(tmp);
    offset++;
    tmp = framRead(addr + offset);  
  } 
  return result;
}

void setPresetName(uint8_t preset, String presetname){
  //preset is 1-30.
  if (preset < 1) preset = 1;
  if (preset > 30) preset = 30;
  int max_name_length = 21;
  int i = 0;
  int addr = PRESET_NAME_ADDR + (21 * (preset - 1));
  while ((i < (presetname.length()) && (i < max_name_length))){
    char value = presetname.charAt(i);
    framWrite(addr + i,value);
    i++;
  } 
  if (presetname.length() < 21) {
     framWrite(addr + presetname.length(),0x00); //null termination
  }
}

String getPassword(){
  String result = "";
  int tmp = framRead(PASS_ADDR);
  int offset = 0;
  while ((tmp > 31) && (tmp < 0x7F) && (offset < 63)) {
    result = result + char(tmp);
    offset++;
    tmp = framRead(PASS_ADDR + offset); 
  }
  if (offset == 0) {
    result = PASS_DEFAULT;
  }
  return result;
}

void setPassword(String password){
  int max_password_length = 63;
  if (password.length() < 8) {
    //Null out to force PASS_DEFAULT
    framWrite(PASS_ADDR,0);
  } else {
    int i = 0;
    while ((i < (password.length()) && (i < max_password_length))){
      char value = password.charAt(i);
      framWrite(PASS_ADDR + i,value);
      i++;
    } 
    //Add null termination
    framWrite(PASS_ADDR + i,0);
  }
}

void receiveEvent(int howMany) {
    DEBUG_PRINTLN("receiveEvent");
    if (write_i2c_to_fram) {
      while (Wire.available() > 0) { 
        uint8_t data = Wire.read();
        //Cache to FRAM because MKR1000 is too slow to process data using a ring buffer.
        framWrite(write_counter,data);
        write_counter++;
      }
    } else if (getSendMidiToUsb()) {
      //Only clock messages supported
      if (Wire.available() > 4) {
        int command = 0;
        command = command + (Wire.read() << 24);
        command = command + (Wire.read() << 16);
        command = command + (Wire.read() << 8);
        command = command + Wire.read();
        if ((command & MIDI_EVENT) == MIDI_EVENT){
          switch(Wire.read()) {
            //Endpoint is 2 found by trial and error
            case 0xF8  : memcpy(midiUsbData, MIDI_CLK, 4); break;
            case 0xFA  : memcpy(midiUsbData, MIDI_CLK_START, 4); break;
            case 0xFB  : memcpy(midiUsbData, MIDI_CLK_CONT, 4); break;
            case 0xFC  : memcpy(midiUsbData, MIDI_CLK_STOP, 4); break;
            //case 0xF8  : USB_Send(2, MIDI_CLK, 4);        MidiUSB.flush();break; 
            //case 0xFA  : USB_Send(2, MIDI_CLK_START, 4);  MidiUSB.flush();break;
            //case 0xFB  : USB_Send(2, MIDI_CLK_CONT, 4);   MidiUSB.flush();break;
            //case 0xFC  : USB_Send(2, MIDI_CLK_STOP, 4);   MidiUSB.flush();break;
          }
        } 
        Wire.flush();
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
    DEBUG_PRINTLN("requestEvent");
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
    //Bypass Wire buffer and write byte directly to I2C.
    int data = framRead(fram_address + read_counter);
    sercom2.sendDataSlaveWIRE(data);
    read_counter++;
    readTime = millis(); 
    digitalWrite(LED_BUILTIN, HIGH);
}

void switchToMaster(){
    delayMicroseconds(100);
    Wire.end();
    
  if (getSendMidiToUsb()) {
    //Delay here to avoid truncating an incoming I2C message (probably not working)
    delayMicroseconds(5000); 
  } 
  //if (getSendMidiToUsb()) SERCOM2->I2CS.ADDR.bit.GENCEN = 0; //Disable GeneralCall
  Wire.begin();
}

void switchToSlave(){
    delayMicroseconds(100);
    Wire.end();
    Wire.begin(0x50 | CARD_ADDRESS,getSendMidiToUsb());//Enable GeneralCall to receive MIDI from bus
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);
}

void sendRemoteEnable() {    
    //Remote Enable
    switchToMaster();
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
    switchToSlave();
}

void sendRemoteDisable() {
    //Remote Disable
    switchToMaster();
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
    switchToSlave();
}

void sendPollingComplete() {    
    //Remote Enable
    switchToMaster();
    Wire.beginTransmission(0); 
    if (v2version) {
        Wire.write(0x14);
    } else {
        Wire.write(0x04);
        Wire.write(0x00);
        Wire.write(0x22);
        Wire.write(0x14);
        Wire.write(0xFF);
    }
    Wire.endTransmission();
    switchToSlave();
}


void sendQuery(byte address) {
    //Query Module
    if (v2version) {
        //Nothing. Query is not supported
    } else {
        switchToMaster();
        Wire.beginTransmission(0);
        Wire.write(0x04);
        Wire.write(address);
        Wire.write(0x22);
        Wire.write(0x1A);
        Wire.write(0xFF);
        Wire.endTransmission();
        switchToSlave();
    }
}

void sendSavePreset(byte preset) {
    //Save Preset
    //Preset=0-29
    preset = preset & 0x1F;
    switchToMaster();
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
    switchToSlave();
}

void sendRecallPreset(byte preset) {
    DEBUG_PRINT("Recall preset:");
    DEBUG_PRINTHEXLN(preset);
    //Recall Preset
    //Preset sent on bus as 0-29
    preset = preset & 0x1F;
    setCurrentPreset(preset + 1); //Track current preset number (1-30)
    switchToMaster();
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
    switchToSlave();
}

void sendBackupPresets(byte address) {
    //Fetch presets from specified module
    //Address 0x44=291e.
    switchToMaster();
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
    switchToSlave();
}

void sendRestorePresets(byte address) {
    //Request specified module to restore presets.
    //Address 0x44=291e.
    switchToMaster();
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
    switchToSlave();
}

void sendMidiNoteOn(byte mask, byte note, byte velo){
    //Send MIDI note ON event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Note 0=C-1, 127=G9.
    //Velo=0-255.
    mask = mask & 0xf;
    if (mask == 0) return;
    switchToMaster();
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
    switchToSlave();
}

void sendMidiNoteOff(byte mask, byte note, byte velo){
    //Send MIDI note OFF event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Note 0=C-1, 127=G9.
    //Velo=0-255.
    mask = mask & 0xf;
    if (mask == 0) return;
    switchToMaster();
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
    switchToSlave();
}

void sendMidiClockStart(){
    //Send MIDI clock start event.
    switchToMaster();
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
    switchToSlave();
}

void sendMidiClockStop(){
    //Send MIDI clock stop event.
    switchToMaster();
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
    switchToSlave();
}

void sendMidiClock(){
    //Send MIDI clock event. 24 per beat required?
    switchToMaster();
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
    switchToSlave();
}

void sendMidiFineTune(byte mask, byte tune){
    //Send MIDI Fine Tune event.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //Tune 0x00=-49, 0x32=An, 0x63=49
    mask = mask & 0xf;
    if (mask == 0) return;
    switchToMaster();
    #ifdef DEBUG_MIDI
    DEBUG_PRINT("Sending Fine Tune. Mask=");
    DEBUG_PRINTHEX(mask);
    DEBUG_PRINT(" Tune=");
    DEBUG_PRINTHEXLN(tune);
    #endif
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
    switchToSlave();
}

void sendMidiBend(byte mask, byte bend_lsb, byte bend_msb){
    //Send MIDI Bend.
    //Mask 0x8=bus A, 0x4=bus B, 0xF=ALL
    //bend_msb 0x00=min bend, 0x40=no bend, 0x7F=max bend.
    //bend_lsb=0x00-0x7F fine tune of bend.
    mask = mask & 0xf;
    if (mask == 0) return;
    switchToMaster();
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
    switchToSlave();
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
      if ((now - lastTime) >= 500) {
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
      if ((now - lastTime) >= 500) {
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
  DEBUG_PRINT("SSID: ");
  DEBUG_PRINTLN(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  DEBUG_PRINT("IP Address: ");
  DEBUG_PRINTLN(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  DEBUG_PRINT("signal strength (RSSI):");
  DEBUG_PRINT(rssi);
  DEBUG_PRINTLN(" dBm");
  // print where to go in a browser:
  DEBUG_PRINT("To see this page in action, open a browser to http://");
  DEBUG_PRINTLN(ip);

}

void printMacAddress(byte mac[]) {
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      DEBUG_PRINT("0");
    }
    DEBUG_PRINTHEX(mac[i]);
    if (i > 0) {
      DEBUG_PRINT(":");
    }
  }
  DEBUG_PRINTLN();
}

void handleWifiRequest(WiFiClient client, String urlString){
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
    DEBUG_PRINTLN("Remote Enable Received"); 
    sendRemoteEnable();
  } 
  else if (urlString.indexOf("/remotedisable") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("Remote Disable Received"); 
    sendRemoteDisable();
  } 
  else if (urlString.indexOf("/savepreset?preset") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("Save Preset Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if ((preset >=1) && (preset <=30)) {
        preset = preset - 1; //0-29 internally.
        #ifdef DEBUG_PRESETS
        DEBUG_PRINTLN(preset);
        #endif
        sendSavePreset(preset);
    }
  }
  else if (urlString.indexOf("/recallpreset?preset") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("Recall Preset Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if ((preset >=1) && (preset <=30)) {
        preset = preset - 1; //0-29 internally.
        #ifdef DEBUG_PRESETS
        DEBUG_PRINTLN(preset);
        #endif
        sendRecallPreset(preset);
    }
  }
  else if (urlString.indexOf("/midinoteon?chan") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Note On Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int chan = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    amploc = urlString.indexOf('&',eqloc);
    int note = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    int velo = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
    chan = (chan - 1) & 0x0F;
    if (chan < 0) chan = 0;
    if (chan > 15) chan = 15;
    note = note & 0x7F;
    velo = velo & 0x7F;
    #ifdef DEBUG_MIDI
    DEBUG_PRINTHEXLN(note);
    DEBUG_PRINTHEXLN(velo);
    #endif
    processMidiNoteOn(chan, note, velo);
  }
  else if (urlString.indexOf("/midinoteoff?chan") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Note Off Received");         
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int chan = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    amploc = urlString.indexOf('&',eqloc);
    int note = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    int velo = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
    chan = (chan - 1) & 0x0F;
    if (chan < 0) chan = 0;
    if (chan > 15) chan = 15;
    note = note & 0x7F;
    velo = velo & 0x7F;
    #ifdef DEBUG_MIDI
    DEBUG_PRINTLN(note);
    DEBUG_PRINTLN(velo);
    #endif
    processMidiNoteOff(chan, note, velo);
  }
  else if (urlString.indexOf("/midiclockstart") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Clock Start Received"); 
    sendMidiClockStart();
  }
  else if (urlString.indexOf("/midiclockstop") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Clock Stop Received");              
    sendMidiClockStop();
  }
  else if (urlString.indexOf("/midiclock") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Clock Received"); 
    sendMidiClock();
  }
  else if (urlString.indexOf("/midifinetune?chan") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Fine Tune Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int chan = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    int tune = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
    chan = chan & 0x0F;
    if (chan < 1) chan = 1;
    int m = mask[chan - 1];
    tune = tune & 0x3F; //max value is 63 decimal.
    #ifdef DEBUG_MIDI
    DEBUG_PRINTLN(m);
    DEBUG_PRINTLN(tune);
    #endif
    sendMidiFineTune(m,tune);
  }
  else if (urlString.indexOf("/midibend?chan") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Bend Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int chan = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    amploc = urlString.indexOf('&',eqloc);
    int bend_lsb = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    int bend_msb = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
    chan = chan & 0x0F;
    if (chan < 1) chan = 1;
    int m = mask[chan - 1];
    bend_lsb = bend_lsb & 0x7F; //max value is 7F.
    bend_msb = bend_msb & 0x7F; //max value is 7F.
    #ifdef DEBUG_MIDI
    DEBUG_PRINTLN(m);
    DEBUG_PRINTLN(bend_lsb);
    DEBUG_PRINTLN(bend_msb);
    #endif
    sendMidiBend(m,bend_lsb,bend_msb);
  } else if (urlString.indexOf("/sendmidibytes") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("MIDI Bytes Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int byte1 = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    amploc = urlString.indexOf('&',eqloc);
    int byte2 = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    int byte3 = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
    midiEventPacket_t midiMessage;
    midiMessage.header =0x00; //not used
    midiMessage.byte1 = byte1 & 0xFF;
    midiMessage.byte2 = byte2 & 0x7F;
    midiMessage.byte3 = byte3 & 0x7F;
    #ifdef DEBUG_MIDI
    DEBUG_PRINTHEXLN(midiMessage.byte1);
    DEBUG_PRINTHEXLN(midiMessage.byte2);
    DEBUG_PRINTHEXLN(midiMessage.byte3);
    #endif
    processMidiMessage(midiMessage);
  }
  else if (urlString.indexOf("/getpresets?addr") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/json",-1); //-1 is chunked encoding
    DEBUG_PRINTLN("Get Presets Received"); 
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
    DEBUG_PRINTHEXLN(address);
    write_i2c_to_fram = true; //Cache incoming i2c to FRAM 
    sendBackupPresets(address); 
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
    DEBUG_PRINTLN("Set Presets Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int module_address = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 16);
    module_address = module_address & 0x7F;
    write_i2c_to_fram = false;
    if (module_address != 0x00) {
        read_counter = 0; //Initialize counter for subsequent I2C READs from master.
        sendRestorePresets(module_address); 
    }
  } else if (urlString.indexOf("/readmemory?addr") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",-1); //-1 means chunked
    DEBUG_PRINTLN("Read Memory Received"); 
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
    DEBUG_PRINTLN("Write Memory Received"); 
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
    DEBUG_PRINTLN("Write Memory Received");              
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
  } else if (urlString.indexOf("/velo=") >= 0) {
    DEBUG_PRINTLN("setVelo Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int velo = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    velo = !!velo; //always save as 1 or 0 enable/disable
    setVelo(chan,velo);
  } else if (urlString.indexOf("/velo") >= 0) {
    DEBUG_PRINTLN("getVelo Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    String result = "{\"chan\": \"" + String(chan + 1,DEC) + "\",\"velo\": \"" + String(velo[chan],DEC) + "\"}"; //one or zero
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/tran=") >= 0) {
    DEBUG_PRINTLN("setTran Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int tran = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    //Transpose is sent as 0x00-0x63 but displayed as -49 to 49 
    tran = tran + 0x32;
    if (tran < 0) tran = 0;
    if (tran > 99) tran = 99;
    setTran(chan,tran);
  } else if (urlString.indexOf("/tran") >= 0) {
    DEBUG_PRINTLN("getTran Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    String result = "{\"chan\": \"" + String(chan + 1,DEC) + "\",\"tran\": \"" + String(tran[chan] - 0x32,DEC) + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/fine=") >= 0) {
    DEBUG_PRINTLN("setFine Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int fine = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 10);
    eqloc = urlString.indexOf('=',amploc);
    String busName = urlString.substring(eqloc + 1, len);
    uint8_t bus;
    if (busName == "A") bus = 0;
    if (busName == "B") bus = 1;
    if (busName == "C") bus = 2;
    if (busName == "D") bus = 3;
    //Fine is sent as 0x00-0x63 but displayed as -49 to 49 with 0 meaning An
    fine = fine + 0x32;
    if (fine < 0) fine = 0;
    if (fine > 99) fine = 99;
    setFine(bus,fine);
  } else if (urlString.indexOf("/fine") >= 0) {
    DEBUG_PRINTLN("getFine Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    String busName = urlString.substring(eqloc + 1, len);
    uint8_t bus;
    if (busName == "A") bus = 0;
    if (busName == "B") bus = 1;
    if (busName == "C") bus = 2;
    if (busName == "D") bus = 3;
    String result = "{\"bus\": \"" + busName + "\",\"fine\": \"" + String(fine[bus] - 0x32,DEC) + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/poly=") >= 0) {
    DEBUG_PRINTLN("setPoly Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int poly = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    poly = !!poly; //always save as 1 or 0 although later it will store active poly bus ABC or D
    setPoly(chan,poly);
  } else if (urlString.indexOf("/poly") >= 0) {
    DEBUG_PRINTLN("getPoly Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    String result = "{\"chan\": \"" + String(chan + 1,DEC) + "\",\"poly\": \"" + String(!!poly[chan],HEX) + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/mask=") >= 0) {
    DEBUG_PRINTLN("setMask Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    int mask = (int)strtol(urlString.substring(eqloc + 1, amploc).c_str(), nullptr, 16);
    eqloc = urlString.indexOf('=',amploc);
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    mask = mask & 0xF;
    setMask(chan,mask);
  } else if (urlString.indexOf("/mask") >= 0) {
    DEBUG_PRINTLN("getMask Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int chan = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (chan < 1) chan = 1;
    if (chan > 16) chan = 16;
    chan = chan - 1;
    String result = "{\"chan\": \"" + String(chan + 1,DEC) + "\",\"mask\": \"0x" + String(mask[chan],HEX) + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/currentpreset") >= 0) {
    DEBUG_PRINTLN("getCurrentPreset Received"); 
    int preset = getCurrentPreset();
    String result = "{\"currentpreset\": \"" + String(preset,DEC) + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/presetnames") >= 0) {
    DEBUG_PRINTLN("getPresetNames Received"); 
    String result = "{\"presetnames\": [";
    for (int preset=1;preset<31;preset++){
      String pname = getPresetName(preset);
      result = result + "\"" + pname + "\"";
      if (preset < 30) result = result + ",";
    }
    result = result + "]}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/presetname=") >= 0) {
    DEBUG_PRINTLN("setPresetName Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int amploc = urlString.indexOf('&');
    String pname = urlString.substring(eqloc + 1, amploc);
    eqloc = urlString.indexOf('=',amploc);
    int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (preset < 1) preset = 1;
    if (preset > 30) preset = 30;
    pname.replace("%20"," ");
    setPresetName(preset,pname);
  } else if (urlString.indexOf("/presetname") >= 0) {
    DEBUG_PRINTLN("getPresetName Received"); 
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int preset = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    if (preset < 1) preset = 1;
    if (preset > 30) preset = 30;
    String pname = getPresetName(preset);
    String result = "{\"presetname\": \"" + pname + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/usbmode=") >= 0) {
    DEBUG_PRINTLN("setUsbMode Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    int mode = (int)strtol(urlString.substring(eqloc + 1, len).c_str(), nullptr, 10);
    mode = !!mode;
    setUsbMode(mode);
  } else if (urlString.indexOf("/usbMode") >= 0) {
    DEBUG_PRINTLN("getSSID Received"); 
    uint8_t mode = getUsbMode();
    String result = "{\"usbMode\": \"" + String(!!mode,DEC) + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/ssid=") >= 0) {
    DEBUG_PRINTLN("setSSID Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    String ssid = urlString.substring(eqloc + 1, len);
    setSSID(ssid);
  } else if (urlString.indexOf("/ssid") >= 0) {
    DEBUG_PRINTLN("getSSID Received"); 
    String ssid = getSSID();
    String result = "{\"ssid\": \"" + ssid + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/password=") >= 0) {
    DEBUG_PRINTLN("setPassword Received"); 
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",0);
    int len = urlString.indexOf(" HTTP");
    int eqloc = urlString.indexOf('=');
    String password = urlString.substring(eqloc + 1, len);
    setPassword(password);
  } else if (urlString.indexOf("/password") >= 0) {
    DEBUG_PRINTLN("getPassword Received"); 
    String password = getPassword();
    String result = "{\"password\": \"" + password + "\"}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/getsetupdata") >= 0) {
    DEBUG_PRINTLN("getSetupData Received"); 
    String ssid = getSSID();
    String password = getPassword();
    int pollonstart = getPollOnStart();
    int programChangeEnable = getProgramChangeEnable();
    int sendMidiToUsb = getSendMidiToUsb();
    int usbMode = getUsbMode();
    String result = "{";
    result = result + "\"ssid\": \"" + ssid + "\"";
    result = result + ",\"password\": \"" + password + "\"";
    result = result + ",\"poll\": \"" + pollonstart + "\"";
    result = result + ",\"prgChEnbl\": \"" + programChangeEnable + "\"";
    result = result + ",\"sendMidi\": \"" + sendMidiToUsb + "\"";
    result = result + ",\"usbMode\": \"" + usbMode + "\"";
    result = result + ",\"buses\":[";
    result = result + "{";
    result = result + "\"bus\": \"A\"";
    result = result + ",\"fine\": \"" + String(fine[0] - 0x32,DEC) + "\"";
    result = result + "}";
    result = result + ",{";
    result = result + "\"bus\": \"B\"";
    result = result + ",\"fine\": \"" + String(fine[1] - 0x32,DEC) + "\"";
    result = result + "}";
    result = result + ",{";
    result = result + "\"bus\": \"C\"";
    result = result + ",\"fine\": \"" + String(fine[2] - 0x32,DEC) + "\"";
    result = result + "}";
    result = result + ",{";
    result = result + "\"bus\": \"D\"";
    result = result + ",\"fine\": \"" + String(fine[3] - 0x32,DEC) + "\"";
    result = result + "}";
    result = result + "]";
    result = result + ",\"channels\":[";
    String comma = "";
    for (int i=0; i<16; i++){
      result = result + comma + "{";
      result = result + "\"chan\": \"" + String(i+1,10)+ "\"";
      result = result + ",\"mask\": \"0x" + String(mask[i],HEX) + "\"";
      result = result + ",\"tran\": \"" + String(tran[i] - 0x32,DEC) + "\"";
      result = result + ",\"poly\": \"" + String(!!poly[i],DEC) + "\"";
      result = result + ",\"velo\": \"" + String(velo[i],DEC) + "\"";
      result = result + "}";
      comma = ",";
    }
    result = result + "]";
    result = result + "}";
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",result.length());
    client.println(result);
  } else if (urlString.indexOf("/postsetupdata") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    DEBUG_PRINTLN("Post Setup Data Received");              
    fram_address = 0;
    JsonToken* token = new JsonToken();
    getJsonToken(token, client);
    uint8_t chan = 0;
    uint8_t bus = 0;
    while (token->data != "") {
        if (token->data ==  "ssid") {
          getJsonToken(token, client);
          setSSID(token->data);  
        } else if (token->data ==  "password") {  
          getJsonToken(token, client);
          setPassword(token->data);        
        } else if (token->data ==  "usbMode") {
          getJsonToken(token, client);
          setUsbMode((int)strtol((token->data).c_str(), nullptr, 10));
        } else if (token->data ==  "poll") {
          getJsonToken(token, client);
          setPollOnStart((int)strtol((token->data).c_str(), nullptr, 10));
        } else if (token->data ==  "prgChEnbl") {
          getJsonToken(token, client);
          setProgramChangeEnable((int)strtol((token->data).c_str(), nullptr, 10));
        } else if (token->data ==  "sendMidi") {
          getJsonToken(token, client);
          setSendMidiToUsb((int)strtol((token->data).c_str(), nullptr, 10));
        } else if (token->data ==  "bus") {
          getJsonToken(token, client);
          String busName = token->data;
          if (busName == "A") bus = 0;
          if (busName == "B") bus = 1;
          if (busName == "C") bus = 2;
          if (busName == "D") bus = 3;
        } else if (token->data ==  "fine") {
          getJsonToken(token, client);
          int fine = (int)strtol((token->data).c_str(), nullptr, 10);
          fine = fine + 0x32;
          if (fine < 0) fine = 0;
          if (fine > 99) fine = 99;
          setFine(bus,fine);
        } else if (token->data ==  "chan") {
          getJsonToken(token, client);
          chan = (int)strtol((token->data).c_str(), nullptr, 10) - 1;
        } else if (token->data ==  "mask") {
          getJsonToken(token, client);
          setMask(chan,(int)strtol((token->data).c_str(), nullptr, 16)); 
        } else if (token->data ==  "tran") {
          getJsonToken(token, client);
          setTran(chan,0x32 + (int)strtol((token->data).c_str(), nullptr, 10)); 
        } else if (token->data ==  "poly") {
          getJsonToken(token, client);
          setPoly(chan,(int)strtol((token->data).c_str(), nullptr, 10)); 
        } else if (token->data ==  "velo") {
          getJsonToken(token, client);
          setVelo(chan,(int)strtol((token->data).c_str(), nullptr, 10)); 
        }
        getJsonToken(token, client);
    } 
    delete token;
  } else if (urlString.indexOf("/test") >= 0) {
    DEBUG_PRINTLN("Test Received"); 
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
    DEBUG_PRINTLN(responseString);
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/plain",0);
    client.println(responseString);
  } else if (urlString.indexOf("/favicon.ico") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","image/x-icon",0);
    DEBUG_PRINTLN("Favicon Received"); 
    //cb(cbArg, 0, 200, "image/x-icon", nullptr);
    //return URL icon here, if desired.
  } else if (urlString.indexOf("/generate_204") >= 0) {
    writeHeader(client,"HTTP/1.1 204 No Content","Content-type:text/plain",0);
    DEBUG_PRINTLN("Generate 204 Received"); 
    //cb(cbArg, 0, 204, "text/plain", nullptr);
  } else if (urlString.indexOf("/setup") >= 0) {
    writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html",setupPageString.length());
    DEBUG_PRINTLN("Returning setup page"); 
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
    DEBUG_PRINTLN("Returning homepage");
    #ifdef DEBUG_HTTP
    DEBUG_PRINTLN(homepageString);
    #endif
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
}

void onMidiUsbDeviceEvent(int ep) {
    //This stops SOF (start of frame) interrupt from happening every 1ms.
    DEBUG_PRINTLN("MIDI Device Interrupt Received");
    USB->DEVICE.INTENCLR.bit.SOF = 1; //SAMD interrupts continuously without this.
}

void pollUsbMidi(bool isUsbDevice) {
  if (isUsbDevice && (USB->DEVICE.DADD.bit.DADD != 0)) {
    if (midiUsbData[0] != 0x00) {
      //Send MIDI clock to USB if data avail.
      USB_Send(2,midiUsbData, 4);
      MidiUSB.flush();
      midiUsbData[0] = 0x00;
    }
    do {
      midiMessage = MidiUSB.read();
      if (midiMessage.header != 0) {
        processMidiMessage(midiMessage);
      }
    } while (midiMessage.header != 0);
  } else if (isUsbDevice && (USB->DEVICE.DADD.bit.DADD == 0)) {
    //This happens when the USB cable has been disconnected and reconnected.
    //The SOF interrupt is needed to re-enumerate. 
    DEBUG_PRINTLN("MIDI Device Address is zero.");
    USB->DEVICE.INTENSET.bit.SOF = 1;
  } else if (!isUsbDevice) {
    //Note that Task() polls a hub if present, and we want to avoid polling.
    //So these conditions carry out enumeration only, and then stop running.
    //The idea is that except for enumeration (and release) this loop should 
    //be quiescent. 
    if (doPipeConfig || (!usbConnected && (UsbH.getUsbTaskState() != USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE))) {
      UsbH.Task();
    } else if (usbConnected && (UsbH.getUsbTaskState() != USB_STATE_RUNNING)){
      UsbH.Task();
    }
    
    if (usbConnected && (UsbH.getUsbTaskState() == USB_STATE_RUNNING) ) {
      if ( Midi && (Midi.GetAddress() != Hub.GetAddress()) && (Midi.GetAddress() != 0)) {
        if (doPipeConfig) {
          //There is a chance that a disconnect interrupt may happen in the middle of this
          //and result in instability. Various tests here on usbConnected to hopefully
          //reduce the chance of it.
          uint32_t epAddr = Midi.GetEpAddress();
          doPipeConfig = false;
          uint16_t rcvd;
          while (usbConnected && (USB->HOST.HostPipe[Midi.GetEpAddress()].PCFG.bit.PTYPE != 0x03)) {
            UsbH.Task(); 
            Midi.RecvData(&rcvd,  bufBk0);
          }
          USB->HOST.HostPipe[epAddr].BINTERVAL.reg = 0x01;//Zero here caused bus resets.
          usb_pipe_table[epAddr].HostDescBank[0].ADDR.reg = (uint32_t)bufBk0;
          usb_pipe_table[epAddr].HostDescBank[1].ADDR.reg = (uint32_t)bufBk1;
          USB->HOST.HostPipe[epAddr].PCFG.bit.PTOKEN = tokIN;
          USB->HOST.HostPipe[epAddr].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY; 
          uhd_unfreeze_pipe(epAddr); //launch the transfer
          USB->HOST.HostPipe[epAddr].PINTENSET.reg = 0x3; //Enable pipe interrupts
          #ifdef DEBUG_USB
          DEBUG_PRINTLN("Pipe Started");
          DEBUG_PRINT("Dump:");
          DEBUG_PRINT("ADDR0:");
          DEBUG_PRINTHEX(usb_pipe_table[epAddr].HostDescBank[0].ADDR.reg);
          DEBUG_PRINT(":");
          DEBUG_PRINT("ADDR1:");
          DEBUG_PRINTHEX(usb_pipe_table[epAddr].HostDescBank[1].ADDR.reg);
          DEBUG_PRINT(":");
          DEBUG_PRINTHEX(USB->HOST.INTFLAG.reg);
          DEBUG_PRINT(":");
          DEBUG_PRINTHEX(USB->HOST.HostPipe[epAddr].PINTFLAG.reg);
          DEBUG_PRINT(":");
          DEBUG_PRINTHEXLN(USB->HOST.HostPipe[epAddr].PSTATUS.reg);
          #endif
        } else {
          while(usbHostBuffer.available()>3) {
            midiMessage.header = usbHostBuffer.read_char();
            midiMessage.byte1 = usbHostBuffer.read_char();
            midiMessage.byte2 = usbHostBuffer.read_char();
            midiMessage.byte3 = usbHostBuffer.read_char();
            processMidiMessage(midiMessage);
          }
        }
      }
    } else {
      USB_SetHandler(&CUSTOM_UHD_Handler);
      USB->HOST.HostPipe[Midi.GetEpAddress()].PINTENCLR.reg = 0xFF; //Disable pipe interrupts
    }
  }
}

void CUSTOM_UHD_Handler(void)
{
  uint32_t epAddr = Midi.GetEpAddress();
  if (USB->HOST.INTFLAG.reg == USB_HOST_INTFLAG_DCONN) {
    DEBUG_PRINTLN("Connected");
    doPipeConfig = true;
    usbConnected = true;
  } else if (USB->HOST.INTFLAG.reg == USB_HOST_INTFLAG_DDISC) {
    DEBUG_PRINTLN("Disconnected");
    usbConnected = false;
    USB->HOST.HostPipe[epAddr].PINTENCLR.reg = 0xFF; //Disable pipe interrupts
  }
  UHD_Handler();
  uhd_freeze_pipe(epAddr);
  #ifdef DEBUG_USB
  DEBUG_PRINTHEX(USB->HOST.INTFLAG.reg);
  DEBUG_PRINT(":");
  DEBUG_PRINTHEX(USB->HOST.HostPipe[epAddr].PINTFLAG.reg);
  DEBUG_PRINT(":");
  DEBUG_PRINTHEX(USB->HOST.HostPipe[epAddr].PSTATUS.reg);
  DEBUG_PRINT(":");
  DEBUG_PRINT("|STATUS0:");
  DEBUG_PRINTHEX(usb_pipe_table[epAddr].HostDescBank[0].STATUS_PIPE.reg);
  DEBUG_PRINT("|STATUS1:");
  DEBUG_PRINTHEX(usb_pipe_table[epAddr].HostDescBank[1].STATUS_PIPE.reg);
  DEBUG_PRINT("|STATUS_BK0:");
  DEBUG_PRINTHEX(usb_pipe_table[epAddr].HostDescBank[0].STATUS_BK.reg);
  DEBUG_PRINT("|STATUS_BK1:");
  DEBUG_PRINTHEX(usb_pipe_table[epAddr].HostDescBank[1].STATUS_BK.reg);
  DEBUG_PRINT("|BYTECOUNT0:");
  DEBUG_PRINTHEX(uhd_byte_count0(epAddr));
  DEBUG_PRINT("|BYTECOUNT1:");
  DEBUG_PRINTHEX(uhd_byte_count1(epAddr));
  DEBUG_PRINT("|TRCPT0:");
  DEBUG_PRINT(Is_uhd_in_received0(epAddr));
  DEBUG_PRINT("|TRCPT1:");
  DEBUG_PRINTHEX(Is_uhd_in_received1(epAddr));
  DEBUG_PRINT("|READY0:");
  DEBUG_PRINTHEX(Is_uhd_in_ready0(epAddr));
  DEBUG_PRINT("|READY1:");
  DEBUG_PRINTHEX(Is_uhd_in_ready1(epAddr));
  DEBUG_PRINT("|CURRBK:");
  DEBUG_PRINTHEX(uhd_current_bank(epAddr));
  DEBUG_PRINT("|TOGGLE:");
  DEBUG_PRINTHEX(Is_uhd_toggle(epAddr));
  DEBUG_PRINT("|TOGGLE_ERROR0:");
  DEBUG_PRINTHEX(Is_uhd_toggle_error0(epAddr));
  DEBUG_PRINT("|TOGGLE_ERROR1:");
  DEBUG_PRINTHEX(Is_uhd_toggle_error1(epAddr));
  DEBUG_PRINT("|NAK:");
  DEBUG_PRINTHEX(Is_uhd_nak_received(epAddr));
  DEBUG_PRINT("|INTSUMMARY:");
  DEBUG_PRINTHEX(uhd_endpoint_interrupt());
  DEBUG_PRINT("|");
  #endif

  //Both banks full and bank1 is oldest, so process first. 
  if (Is_uhd_in_received0(epAddr) && Is_uhd_in_received1(epAddr) && uhd_current_bank(epAddr)) {
    handleBank1(epAddr);
  }
  if(Is_uhd_in_received0(epAddr)) {
    handleBank0(epAddr);
  } 
  if (Is_uhd_in_received1(epAddr)) {
    handleBank1(epAddr);
  }
  uhd_unfreeze_pipe(epAddr);    
}


void handleBank0(uint32_t epAddr){
  int rcvd = uhd_byte_count0(epAddr);
  for (int i = 0; i < rcvd; i++) {
    if ((bufBk0[i] > 0) && (usbHostBuffer.availableForStore() > 3)) {
      usbHostBuffer.store_char(bufBk0[i]);
      i++;
      usbHostBuffer.store_char(bufBk0[i]);
      i++;
      usbHostBuffer.store_char(bufBk0[i]);
      i++;
      usbHostBuffer.store_char(bufBk0[i]);
    }
  }      
  uhd_ack_in_received0(epAddr);
  uhd_ack_in_ready0(epAddr);
}

void handleBank1(uint32_t epAddr){
  int rcvd = uhd_byte_count1(epAddr);
  for (int i = 0; i < rcvd; i++) {
    if ((bufBk1[i] > 0) && (usbHostBuffer.availableForStore() > 3)) {
      usbHostBuffer.store_char(bufBk1[i]);
      i++;
      usbHostBuffer.store_char(bufBk1[i]);
      i++;
      usbHostBuffer.store_char(bufBk1[i]);
      i++;
      usbHostBuffer.store_char(bufBk1[i]);
    }
  } 
  uhd_ack_in_received1(epAddr);
  uhd_ack_in_ready1(epAddr);
}

void processMidiMessage(midiEventPacket_t midiMessage){
  #ifdef DEBUG_MIDI
  DEBUG_PRINT("Received: ");
  DEBUG_PRINTHEX(midiMessage.header);
  #endif
  uint8_t chan = midiMessage.byte1 & 0x0F;
  switch (midiMessage.byte1 & 0xF0) {
    case 0x90 : {processMidiNoteOn(chan, midiMessage.byte2, midiMessage.byte3);break;}
    case 0x80 : {processMidiNoteOff(chan, midiMessage.byte2, midiMessage.byte3);break;}
    case 0xF0 : {processClockMessage(midiMessage.byte1);break;}
    case 0xE0 : {sendMidiBend(mask[chan], midiMessage.byte2, midiMessage.byte3);break;}
    case 0xB0 : {processControlChange(chan, midiMessage.byte2, midiMessage.byte3);break;}
    case 0xC0 : {processProgramChange(midiMessage.byte2);break;}
    default   : {}
  }
  #ifdef DEBUG_MIDI
  DEBUG_PRINTHEX(midiMessage.byte1);
  DEBUG_PRINTHEX(midiMessage.byte2);
  DEBUG_PRINTHEXLN(midiMessage.byte3);
  #endif
}

void processMidiNoteOn(uint8_t chan, uint8_t note, uint8_t velo){
  //channel is 0-15
  uint8_t m = mask[chan]; //unison (non poly) mask
  note = note + (tran[chan] - 0x32); //transpose
  if (note < 0) note = 0;
  if (note > 0x7F) note = 0x7F;
  if ((poly[chan]>0) && (mask[chan]>0)) {
    if (getPolyMaskFromNote(chan,note) > 0) {
      return; //bail out if poly and the note is already playing
    }
    //Shift poly bit to next bus in mask and return as mask.
    poly[chan] = (poly[chan] <<1) & 0xF;
    if (poly[chan] == 0) poly[chan] = 1; //wrap
    uint8_t nextAvail = polyAvail[chan] & mask[chan] & 0xF;
    if (nextAvail == 0) nextAvail = mask[chan]; //steal if necessary.
    while ((poly[chan] & nextAvail) == 0) {
      poly[chan] = (poly[chan] <<1) & 0xF;
      if (poly[chan] == 0) poly[chan] = 1; //wrap
    }
    m = poly[chan]; //One of 0001/0010/0100/1000
    savePolyNote(chan,m,note); //save note and mask for note OFF
    polyAvail[chan] = polyAvail[chan] & mask[chan] & ~m & 0xF; //mark bus unavailable
  }
  sendMidiNoteOn(m, note, getVelocity(chan,velo));
}

void processMidiNoteOff(uint8_t chan, uint8_t note, uint8_t velo) {
  //Channel is 0-15
  uint8_t m = mask[chan]; //unison (non poly) mask
  note = note + (tran[chan] - 0x32); //transpose
  if (note < 0) note = 0;
  if (note > 0x7F) note = 0x7F;
  if ((poly[chan]>0) && (mask[chan]>0)){
    //Poly mode.
    m = getPolyMaskFromNote(chan,note); //retrieve associated mask for this note
    polyAvail[chan] = ((polyAvail[chan] & mask[chan]) | m) & 0xF; //mark bus available.
    savePolyNote(chan,m,0x80); //set bogus note for this bus to mark available
  }
  sendMidiNoteOff(m, note, getVelocity(chan,velo));
}


void savePolyNote(uint8_t chan,uint8_t mask, uint8_t note){
  //save current note ON value to match with note OFF event later.
  if (mask == 0) return; //This channel has no buses
  uint32_t shifter;
  uint32_t shiftedNote;
  switch (mask) {
    case 1: {shifter = 0xFFFFFF00; shiftedNote = note; break;}
    case 2: {shifter = 0xFFFF00FF; shiftedNote = note<<8; break;}
    case 4: {shifter = 0xFF00FFFF; shiftedNote = note<<16; break;}
    case 8: {shifter = 0x00FFFFFF; shiftedNote = note<<24; break;}
    default: return; //not a poly mask
  }
  polyNotes[chan] = polyNotes[chan] | ~shifter;
  polyNotes[chan] = polyNotes[chan] & (shiftedNote | shifter);
}

uint8_t getPolyMaskFromNote(uint8_t chan, uint8_t note){
  //See if the note was saved by note ON into polyNotes
  //and if found, return the corresponding mask.
  //Chan is 0-15
  uint8_t result = 0;
  if (note == (polyNotes[chan] & 0xFF)) result = 1;
  else if (note == (polyNotes[chan] & (0xFF << 8))>>8) result = 2;
  else if (note == (polyNotes[chan] & (0xFF << 16))>>16) result = 4;
  else if (note == (polyNotes[chan] & (0xFF << 24))>>24) result = 8;
  return result;  
}

uint8_t getVelocity(uint8_t chan,uint8_t velocity) {
  //Channel is 0-15
  if (velo[chan]) {
    return velocity;
  } else {
    return 0x7F;//ignore velocity and play note at full volume
  }
}

void processClockMessage(uint8_t clockMessage){
  switch(clockMessage){
    case 0xF8: {sendMidiClock();break;}
    case 0xFA: {sendMidiClockStart();break;}
    case 0xFC: {sendMidiClockStop();break;}
  }
}

void processControlChange(uint8_t chan, uint8_t byte1, uint8_t byte2){
  switch(byte1){
    case 0x1E: {sendRemoteEnable();break;}
    case 0x1D: {sendRemoteDisable();break;}
    case 0x1C: { //Save preset
                  uint8_t preset = byte2; 
                  preset = preset-1; 
                  if (preset < 0) preset = 0; //Map minimum to 0.
                  else if (preset > 29) preset = 29; //Map maximum to 29.
                  sendSavePreset(preset);
                  break;
               }
    case 0x1F: {sendMidiFineTune(mask[chan], byte2);break;} //0x1F is defined in the modules.
  }
}

void processProgramChange(int preset){
  //presets are 0-29, but display as 1-30. 
  //Let's have program change use 1-30.
  if (getProgramChangeEnable()){
    preset = preset - 1;
    if (preset < 0) preset = 0; //Map minimum to 0.
    else if (preset > 29) preset = 29; //Map maximum to 29.
    sendRecallPreset(preset);
  }
}

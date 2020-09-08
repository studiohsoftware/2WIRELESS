#include <WiFi101.h>
#include <SPI.h>
#include <WiFi101.h>
//#include "arduino_secrets.h"
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = "BUCHLA";        // your network SSID (name)
char pass[] = "BUCHLA200E";    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                // your network key Index number (needed only for WEP)

int led =  LED_BUILTIN;
int status = WL_IDLE_STATUS;
WiFiServer server(80);

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

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
  // WiFi.config(IPAddress(10, 0, 0, 1));

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
            writeHeader(client,"HTTP/1.1 200 OK","Content-type:text/html");
            Serial.println(""); 
            Serial.println("URL is: " + urlString); 
            // Check to see if the client request was "GET /H" or "GET /L":
            if (urlString.startsWith("GET /H")) {
              writeHomepage(client);
              digitalWrite(led, HIGH);               // GET /H turns the LED on
            }
            else if (urlString.startsWith("GET /L")) {
              writeHomepage(client);
              digitalWrite(led, LOW);                // GET /L turns the LED off
            } 
            else if (urlString.indexOf("/remoteenable") >= 0) {
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //switchToMaster();
              //sendRemoteEnable();
              //switchToSlave();
              Serial.println("Remote Enable Received"); 
            } 
            else if (urlString.indexOf("/remotedisable") >= 0) {
              //cb(cbArg, 0, 200, "text/plain", nullptr);
              //switchToMaster();
              //sendRemoteDisable();
              //switchToSlave();
              Serial.println("Remote Disable Received"); 
            } else {
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

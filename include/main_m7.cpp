#include <Arduino.h>
#include <SPI.h>
#include <RPC.h>
#include "RPC.h"

#define AP_MODE false
using namespace rtos;

Thread sensorThread;
#define WIFI true

#if WIFI
  #include <WiFi.h>
  #include <web_page.html>
  char ssid[] = "Analogue_Hyperlapse_Camera";        // your network SSID (name)
  char password[] = "CraterLab";    // your network password (use for WPA, or use as key for WEP)
  IPAddress ip(192, 168, 8, 3);
  IPAddress gateway(192, 168, 8, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 8, 1); //primaryDNS
  WiFiServer httpServer(80);
  // for ArduinoOSC
#endif

#define command Serial2

#define x_axis 0
#define y_axis 1
#define a_axis 2

#define PULSE_REV 400
#define REDUCTION_XY 5
#define REDUCTION_A 60

#define STOP 0
#define ZERO 1
#define CONTINUOUS 2
#define ANGLE_MODE 3

int mode = CONTINUOUS;

const char axis_c[] = {'x','y','a'};


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

void printWifiData() {
  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
  Serial.println(ip);

  // print your MAC address:
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC address: ");
  printMacAddress(mac);
}

void printCurrentNet() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print the MAC address of the router you're attached to:
  byte bssid[6];
  WiFi.BSSID(bssid);
  Serial.print("BSSID: ");
  printMacAddress(bssid);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.println(rssi);

  // print the encryption type:
  byte encryption = WiFi.encryptionType();
  Serial.print("Encryption Type:");
  Serial.println(encryption, HEX);
  Serial.println();
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  #if AP_MODE
    // print where to go in a browser:
    Serial.print("To see this page in action, open a browser to http://");
    Serial.println(ip);
  #else
    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
  #endif
}



int read_value[10];
int* decode_values(String inputString, int num_dec)
  {
    String str = inputString.substring(inputString.lastIndexOf(":") + 1);
    read_value[0] = str.substring(0, str.indexOf(',')).toInt();
    for(int i=1; i<num_dec; i++)
      {
        str = str.substring(str.indexOf(',')+1);
        read_value[i] = str.substring(0, str.indexOf(',')).toInt();
      }
    return read_value;
  }

char c;
String inputString;
void SerialRead()
  {
     ////////////////////////////////////////////////////////////
     /////// RUTINA TRATAMIENTO DE STRINGS DEL UART   ///////////
     ////////////////////////////////////////////////////////////
     if (command.available())
       {
         c = command.read();
         if ((c == '\r') || (c == '\n'))
         {
           if (inputString.startsWith("/axisA:")) RPC.println(inputString);
           else if (inputString.startsWith("/axisX:")) RPC.println(inputString);
           else if (inputString.startsWith("/axisY:")) RPC.println(inputString);
           inputString = String();
         }
         else
           inputString += c;
       }
  }

/*
This thread calls the sensorThread() function remotely
every second. Result is printed to the RPC1 stream.
*/

float Motor_axis[3] = {0,0,0};

void requestReading() {
  while (true) {
    delay(25);
    Motor_axis[x_axis] = RPC.call("AxisX").as<int>();
    Motor_axis[y_axis] = RPC.call("AxisY").as<int>();
    Motor_axis[a_axis] = RPC.call("AxisA").as<int>();
  }
}

int mode_Read() {
  int result = mode;
  return result;
}

void setup() {
  Serial.begin(1000000);
  command.begin(9600);   // set this as high as you can reliably run on your platform
  RPC.begin(); //boots M4
  #if WIFI  
    // check for the WiFi module:
    if (WiFi.status() == WL_NO_MODULE) {
      Serial.println("Communication with WiFi module failed!");
      // don't continue
      while (true);
    }

    #if AP_MODE
    int status = WL_IDLE_STATUS;
    // Create open network. Change this line if you want to create an WEP network:
    WiFi.config(ip, dns, gateway, subnet); 
    status = WiFi.beginAP(ssid, password);
    if (status != WL_AP_LISTENING) {
      Serial.println("Creating access point failed");
      // don't continue
      while (true)
        ;
    }
  #else
    WiFi.config(ip, dns, gateway, subnet); 
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(1000);
      Serial.print(".");
    }
  #endif
    httpServer.begin();
    // you're connected now, so print out the status:
    printWifiStatus();
  #else
    delay(1000);
  #endif
  
  /*
  Starts a new thread that loops the requestReading() function
  */
  sensorThread.start(requestReading);
}

void loop() {

  #if WIFI  
    SerialRead();

    String buffer = "";

    while (RPC.available()) {

      buffer += (char)RPC.read();  // Fill the buffer with characters

    }

    if (buffer.length() > 0) {

      Serial.print(buffer);

    }

    //Serial.println(WiFi.status());
    WiFiClient client = httpServer.available();

     if (client) {                             // if you get a client,
    //Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        //Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {

            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Connection: close");  // the connection will be closed after completion of the response
            client.println("Refresh: 0.025");  // refresh the page automatically every 5 sec
            client.println();
            client.println("<!DOCTYPE HTML>");
            client.println("<html>");
            // output the value of each analog input pin
           
            //create the buttons
            client.print("Click <a href=\"/L0\">here</a> to move lineal motor 0%<br>");
            client.print("Click <a href=\"/L50\">here</a> to move lineal motor 50%<br>");
            client.print("Click <a href=\"/L100\">here</a> to move lineal motor 100%<br><br>");

            client.print("Click <a href=\"/REC_START\">here</a> to record video<br>");
            client.print("Click <a href=\"/REC_STOP\">here</a> to stop record video<br><br>");

            client.print("Click <a href=\"/ZERO\">here</a> for initial position<br>");
            client.print("Click <a href=\"/PRESET\">here</a> for movement in automatic mode<br>");
            client.print("Click <a href=\"/AUTO\">here</a> for movement in automatic mode<br>");
            client.print("Click <a href=\"/STOP\">here</a> for stop movement in automatic mode<br><br>");
            
            for (int axis = 0; axis < 3; axis++) {
                int sensorReading = Motor_axis[axis];
                float reduction = REDUCTION_XY;
                if (axis==2) reduction = REDUCTION_A;
                client.print("Axis ");
                client.print(axis_c[axis]);
                client.print(" is ");
                client.print(360*sensorReading/(reduction*PULSE_REV));
                client.println("<br />");
              }
              client.println("</html>");
              break;
          }
          else {      // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }
        else if (c != '\r') {    // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        if (currentLine.endsWith("GET /L0")) {
          command.println("/p_motor:0,0"); //Motor number, % position    
        }
        if (currentLine.endsWith("GET /L50")) {
          command.println("/p_motor:0,50"); //Motor number, % position    
        }
        if (currentLine.endsWith("GET /L100")) {
          command.println("/p_motor:0,100"); //Motor number, % position     
        }
        if (currentLine.endsWith("GET /REC_START")) {
          //command.println("/stepper:1,1,24"); //Stepper number, mode(0:LEFT, 1:RIGHT, 2:ONE TURN LEFT, 3:ONE TURN RIGHT), fps     
          command.println("/s_motor:4,1,24"); //motor number, mode(0:LEFT, 1:RIGHT, 2:ONE TURN LEFT, 3:ONE TURN RIGHT), fps     
        }
        if (currentLine.endsWith("GET /REC_STOP")) {
          //command.println("/stepper:1,1,0"); //Stepper number, mode(0:LEFT, 1:RIGHT, 2:ONE TURN LEFT, 3:ONE TURN RIGHT,), fps     
          command.println("/s_motor:4,1,0"); //motor number, mode(0:LEFT, 1:RIGHT, 2:ONE TURN LEFT, 3:ONE TURN RIGHT), fps     
        }
        if (currentLine.endsWith("GET /ZERO")) {
          if (mode!=ZERO)
            {
              mode = ZERO;  
              RPC.println("/mode:"+ String(mode));   
            }
        }
        if (currentLine.endsWith("GET /PRESET")) {
          if (mode!=ANGLE_MODE)
            {
              mode = ANGLE_MODE;  
              RPC.println("/mode:"+ String(mode));   
            }
        }
        if (currentLine.endsWith("GET /AUTO")) {
          if (mode!=CONTINUOUS)
            {
              mode = CONTINUOUS;  
              RPC.println("/mode:"+ String(mode));   
            }
        }
        if (currentLine.endsWith("GET /STOP")) {
          if (mode!=STOP)
            {
              mode = STOP;  
              RPC.println("/mode:"+ String(mode));   
            }
        }

      }
    }
      // give the web browser time to receive the data
      //delay(1);

      // close the connection:
      client.stop();
      //Serial.println("client disconnected");
    }
  #endif
}
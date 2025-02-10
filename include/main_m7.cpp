#include <Arduino.h>
#include <SPI.h>
#include <RPC.h>
#include "RPC.h"
#include <ArduinoJson.h>
#include "interface.html"
#include <constants.h>

#define AP_MODE false

// Inicializando RTOS
using namespace rtos;
Thread sensorThread;

#define WIFI true

#if WIFI
  #include <WiFi.h>
  char ssid[] = "Analogue_Hyperlapse_Camera";        // your network SSID (name)
  char password[] = "CraterLab";    // your network password (use for WPA, or use as key for WEP)
  IPAddress ip(192, 168, 8, 3);
  IPAddress remote_ip(192, 168, 8, 4);
  IPAddress gateway(192, 168, 8, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(8, 8, 8, 8); //primaryDNS
  // Crear el servidor en el puerto 80
  WiFiServer server(80);
  char packetBuffer[255]; //buffer to hold incoming packet
  char  ReplyBuffer[] = "acknowledged";       // a string to send back
  WiFiUDP Udp;
  unsigned int localPort = 2390;      // local port to listen on
  unsigned int remotePort = 2392;      // local port to send
#endif

#define x1_axis 0
#define y0_axis 1
#define x0_axis 2

#define STOP 0
#define ZERO 1
#define CONTINUOUS 2
#define ANGLE_MODE 3

// Motor
int motorSpeedValue = 25;
bool motorIsSpeedActive = false;
int motorIntervalFrames = 1;
int motorIntervalSeconds = 1;
bool motorIsIntervalActive = false;
int motorDirection = 1;

// Shutter
int shutterFadePercent = 0;
int shutterFadeFrames = 50;
bool shutterSyncWithInterval = false;
bool shutterFadeInActive = false;
bool shutterFadeOutActive = false;

// Óptica
int zoomValue = 50;
int focusValue = 50;
float diaphragmValue = 1.8;
bool syncWithIntervalOptics = false;

// Dispositivo 360
int x0Degrees = 0;
int x0Duration = 0;
int x1Degrees = 0;
int x1Duration = 0;
int y0Degrees = 0;
int y0Duration = 0;
bool syncWithInterval360 = false;

//Sensores
float motorSpeedRead = 0;
float FadePercentRead = 0;
int zoomValueRead = 0;
int focusValueRead = 0;
float diaphragmValueRead = 0;
int x0DegreesRead = 0;
int x1DegreesRead = 0;
int y0DegreesRead = 0;

int mode = CONTINUOUS;

const char axis_c[] = {'x','y','a'};

float Motor_axis[3] = {0,0,0};

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

void handlePostRequest(String body) {
  // Parseamos el cuerpo del JSON
  StaticJsonDocument<1000> doc;
  deserializeJson(doc, body);
  int save = true;
  //DeserializationError error = deserializeJson(doc, body);
  /*if (error) {
    Serial.println("Error al parsear el JSON");
    return;
  }*/

  Serial.println("Procesando JSON...");
  String type = doc["type"];
  if((type=="test_360")||(type=="save_360"))
    {
      Serial.println(type);
      if (type=="save_360") save = true;
      else save = false;
      String axis = doc["motor"];
      if (axis=="x0")
        {
          x0Degrees = doc["degrees"];
          x0Duration = doc["duration"];
          syncWithInterval360 = doc["syncWithInterval"];
          RPC.println("/axisA:" + String(x0Degrees) + "," + String(x0Duration) + "," + String(syncWithInterval360) + "," + String(save));
        }
      else if (axis=="x1")  
        {
          x1Degrees = doc["degrees"];
          x1Duration = doc["duration"];
          syncWithInterval360 = doc["syncWithInterval"];
          RPC.println("/axisX:" + String(x1Degrees) + "," + String(x1Duration) + "," + String(syncWithInterval360) + "," + String(save));
        }
      else if (axis=="y0")
        {
          y0Degrees = doc["degrees"];
          y0Duration = doc["duration"];
          syncWithInterval360 = doc["syncWithInterval"];
          RPC.println("/axisY:" + String(y0Degrees) + "," + String(y0Duration) + "," + String(syncWithInterval360) + "," + String(save));
        }
    }
  else Serial.println("No reconocido");
}

/*
This thread calls the sensorThread() function remotely
every second. Result is printed to the RPC1 stream.
*/

void requestReading() {
  while (true) {
    delay(25);
    Motor_axis[x1_axis] = RPC.call("AxisX").as<int>();
    Motor_axis[y0_axis] = RPC.call("AxisY").as<int>();
    Motor_axis[x0_axis] = RPC.call("AxisA").as<int>();
    x0DegreesRead = 360*Motor_axis[x0_axis]/(REDUCTION_A*PULSE_REV);
    y0DegreesRead = 360*Motor_axis[y0_axis]/(REDUCTION_XY*PULSE_REV);
    x1DegreesRead = 360*Motor_axis[x1_axis]/(REDUCTION_XY*PULSE_REV); 

  }
}

int mode_Read() {
  int result = mode;
  return result;
}

void setup() {
  Serial.begin(1000000);
  RPC.begin(); //boots M4
  #if WIFI  
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
  Serial.println();
  Serial.println("Conectado a WiFi!");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
  #endif
  while (digitalRead(FC[x0_axis])||digitalRead(FC[x1_axis])||digitalRead(FC[y0_axis]));
  // Enable Server
  server.begin();
  // Enable UDP
  Udp.begin(localPort);
  //Starts a new thread that loops the requestReading() function
  sensorThread.start(requestReading);
}

void readUDP()
  {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
      #if DEBUG_M7
        Serial.print("Received packet of size ");
        Serial.println(packetSize);
        Serial.print("From ");
        IPAddress remoteIp = Udp.remoteIP();
        Serial.print(remoteIp);
        Serial.print(", port ");
        Serial.println(Udp.remotePort());
      #endif
      // read the packet into packetBufffer
      int len = Udp.read(packetBuffer, 255);
      if (len > 0) {
        packetBuffer[len] = 0;
      }
      RPC.println(packetBuffer);
      #if DEBUG_M7
        Serial.println("Contents:");
        Serial.println(packetBuffer);
      #endif
    }
  }

void loop() {
    WiFiClient client = server.available();
    if (client) {
      #if DEBUG_M7
        Serial.println("Nuevo cliente conectado");
      #endif
      String request = "";
      bool isPostRequest = false;

      while (client.connected()) {
        if (client.available()) {
          char c = client.read();
          request += c;

          // Identificar si es una solicitud POST
          if (request.indexOf("POST") >= 0) {
            isPostRequest = true;
          }

          // Si se encuentra el final de la solicitud
          if (c == '\n' && request.endsWith("\r\n\r\n")) {
            #if DEBUG_M7
              Serial.println("Solicitud recibida:");
              Serial.println(request);
            #endif

            if (isPostRequest) {
              String body = "";
              while (client.available()) {
                body += (char)client.read();
              }
              #if DEBUG_M7
                Serial.println("Cuerpo del mensaje recibido:");
                Serial.println(body);
              #endif
              // Llamamos a la función para procesar la petición POST
              handlePostRequest(body);
              // Respuesta al cliente
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/plain");
              client.println();
              client.println("Datos enviados correctamente");
            } else if (request.indexOf("GET /sensors") >= 0) { // actualiza fps en interface
              // Respuesta para la ruta /motorSpeed
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println();
              String response = "{\"sensors\":[" + String(x0DegreesRead) + "," + String(x1DegreesRead) + "," + String(y0DegreesRead) + "]}";
              client.print(response); 
            } else {
              // Respuesta para servir la interfaz HTML
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();
              client.print(htmlTemplate);
            }
            break;
          }
        }
      }
      client.stop();
      #if DEBUG_M7
        Serial.println("Cliente desconectado");
      #endif
    }
    readUDP();
    #if DEBUG_M7
      String buffer = "";

      while (RPC.available()) {

        buffer += (char)RPC.read();  // Fill the buffer with characters

      }

      if (buffer.length() > 0) {

        Serial.print(buffer);

      }
    #endif
}
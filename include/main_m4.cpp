#include <Arduino.h>
#include <SPI.h>
#include <RPC.h>

#define DEBUG_M4 true

using namespace rtos;

Thread sensorThread;

#define x_axis 0
#define y_axis 1
#define a_axis 2

#define RIGHT false
#define LEFT true
#define STOP 0
#define ZERO 1
#define CONTINUOUS 2
#define ANGLE_MODE 3

int axis_y_angle = 0;
unsigned long time_axis_y_angle = 10000;
int axis_x_angle = 0;
unsigned long time_axis_x_angle = 10000;
int axis_a_angle = 0;
unsigned long time_axis_a_angle = 10000;

int mode = STOP;


#if defined(ARDUINO_GIGA_M7)
      #define debug Serial 
#elif defined(ARDUINO_GIGA_M4) 
      #define debug RPC 
#endif


int EN[3] = {10, 7, 4};
int DIRP[3] = {9, 6, 3};
int PUL[3] = {8, 5, 2};
int FC[3] = {51, 53, 49}; 

#define PULSE_REV 400
#define REDUCTION_XY 5
#define REDUCTION_A 60

#define TIME_PULSE_XY 2500
#define TIME_PULSE_A 500

bool direction[3] = {RIGHT, RIGHT, RIGHT};

unsigned long time_fc_zero[3] = {millis(), millis(), millis()};
bool enable_zero[3] = {true, true, true};

void search_zero()
  {
    time_fc_zero[x_axis] = millis();
    while(digitalRead(FC[x_axis])||((millis()-time_fc_zero[x_axis])<10))
      {
        digitalWrite(PUL[x_axis], HIGH);
        delayMicroseconds(2000);
        digitalWrite(PUL[x_axis], LOW);
        delayMicroseconds(2000);
        if(digitalRead(FC[x_axis])) time_fc_zero[x_axis] = millis();
      }
    time_fc_zero[y_axis] = millis();
    while(digitalRead(FC[y_axis])||((millis()-time_fc_zero[y_axis])<10))
      {
        digitalWrite(PUL[y_axis], HIGH);
        delayMicroseconds(2000);
        digitalWrite(PUL[y_axis], LOW);
        delayMicroseconds(2000);
        if(digitalRead(FC[y_axis])) time_fc_zero[y_axis] = millis();
      };
    time_fc_zero[a_axis] = millis();
    while(digitalRead(FC[a_axis])||((millis()-time_fc_zero[a_axis])<10))
      {
        digitalWrite(PUL[a_axis], HIGH);
        delayMicroseconds(250);
        digitalWrite(PUL[a_axis], LOW);
        delayMicroseconds(250);
        if(digitalRead(FC[a_axis])) time_fc_zero[a_axis] = millis();
      }
  }

int angle_pulses[3] = {0, 0, 0}; //Varaible temporal de pulsos 
float axis_angle[3] = {0, 0, 0}; //Angulo actual

float angleRead(int axis)
  {
    int sensorReading = angle_pulses[axis];
    float reduction = REDUCTION_XY;
    if (axis==2) reduction = REDUCTION_A;
    return (360*sensorReading/(reduction*PULSE_REV));
  }

int axis_pulses[3] = {0, 0, 0}; //Pulsos necesarios para llegar al angulo indicado
int axis_direction[3] = {1, 1, 0}; //Direccion de giro
int angle_pulses_MAX[3] = {0, 0, 0}; //Pulsos necesarios para dar una vuelta completa
int angle_pulses_ant[3] = {0, 0, 0}; //Varaible temporal de pulsos 
bool flag_search[3] = {false, false, false};
bool flag_equal[3] = {true, true, true};
bool flag_refresh[3] = {false, false, false};

unsigned long time_axis[3] = { millis(), millis(), millis()};
unsigned long time_parts[3] = {10, 10, 10};
unsigned long time_pulse[3] = {TIME_PULSE_XY, TIME_PULSE_XY, TIME_PULSE_A};
int ant_angle[3] = {0, 0, 0};

void stop_all()
  {
    for(int i=0; i<3; i++) flag_refresh[i] = false;
  }

#define RESOLUTION_ANGLE 0.25
float axis_angle_ant[3] = {0,0,0};

void search_angle(int axis, int angle, unsigned long time_initial, unsigned long time_final)
  {
    axis_angle[axis] = angleRead(axis);
    if((axis_angle[axis]>((float)angle+RESOLUTION_ANGLE))||(axis_angle[axis]<((float)angle-RESOLUTION_ANGLE)))
      {
        if (axis_angle_ant[axis]!=axis_angle[axis])
          {
            RPC.print(axis_angle[axis]);
            RPC.print(" ");
            RPC.println(angle);
            axis_angle_ant[axis] = axis_angle[axis];
          }
        if (flag_search[axis])
          {
            flag_search[axis] = false;
            RPC.print(axis_angle[axis]);
            RPC.print(" ");
            RPC.print(angle);
            if (axis_angle[axis]<(angle + RESOLUTION_ANGLE)) direction[axis] = RIGHT;
            else direction[axis] = LEFT;
            if ((abs(axis_angle[axis])>360)||(abs(angle)>360))enable_zero[axis] = false;
            else enable_zero[axis] = true;
            if((axis==y_axis)||(axis==a_axis)) direction[axis]=!direction[axis];
            digitalWrite(DIRP[axis], direction[axis]);
            if (axis!=a_axis) axis_pulses[axis] = map(abs(angle-axis_angle[axis]), 0, 359, 0, PULSE_REV*REDUCTION_XY);
            else axis_pulses[axis] = map(abs(angle-axis_angle[axis]), 0, 359, 0, PULSE_REV*REDUCTION_A);
            time_pulse[axis] = ((1000/2)*(time_final-time_initial)/axis_pulses[axis]); //1000 es conversion de milisegundos a microsegundos entre dos porque es tiempo de 0 a 1
            if((time_pulse[axis]<TIME_PULSE_XY)&&(axis<=y_axis)) time_pulse[axis]=TIME_PULSE_XY;
            else if((time_pulse[axis]<TIME_PULSE_A)&&(axis==a_axis)) time_pulse[axis]=TIME_PULSE_A;
            RPC.print(" ");
            RPC.print(axis_pulses[axis]);
            RPC.print(" ");
            RPC.println(time_pulse[axis]);
            flag_refresh[axis] = true;
          }
      }
    else if((axis_angle[axis]<=((float)angle+RESOLUTION_ANGLE))&&(axis_angle[axis]>=((float)angle-RESOLUTION_ANGLE))) flag_refresh[axis] = false;
  }

unsigned long time_refresh_x = micros();
unsigned long time_refresh_y = micros();
unsigned long time_refresh_a = micros();
bool state_x = false;
bool state_y = false;
bool state_a = false;
void refresh_steppers()
  {
    if(((micros()-time_refresh_x)>=time_pulse[x_axis])&&(flag_refresh[x_axis]))
      {
        time_refresh_x = micros();
        digitalWrite(PUL[x_axis], state_x);
        if(state_x) 
          {
            if (direction[x_axis]==RIGHT) angle_pulses[x_axis]++;
            else angle_pulses[x_axis]--;
          }
        state_x = !state_x;      
      }
    if(((micros()-time_refresh_y)>=time_pulse[y_axis])&&(flag_refresh[y_axis]))
      {
        time_refresh_y = micros();
        digitalWrite(PUL[y_axis], state_y);
        if(state_y) 
          {
            if (direction[y_axis]==LEFT) angle_pulses[y_axis]++;
            else angle_pulses[y_axis]--;
          }
        state_y = !state_y;
      }
    if(((micros()-time_refresh_a)>=time_pulse[a_axis])&&(flag_refresh[a_axis]))
      {
        time_refresh_a = micros();
        digitalWrite(PUL[a_axis], state_a);
        if(state_a)
          {
            if (direction[a_axis]==LEFT) angle_pulses[a_axis]++;
            else angle_pulses[a_axis]--;
          } 
        state_a = !state_a;
      }
  }


long startTimex = 0;
long startTimey = 0;
long startTimea = 0;
const int timeThreshold = 500;

void f_x_axis(){ //Funcion de paso por cero de la interrupcion del eje x
   if (((millis() - startTimex) > timeThreshold)&&(enable_zero[x_axis]))
    {
      if(angle_pulses[x_axis]>0) angle_pulses_MAX[x_axis] = angle_pulses[x_axis]; //Varaible de pulsos maximos por vuelta
      angle_pulses[x_axis] = 0; //Varaible temporal de pulsos 
      startTimex = millis();
    }
  }

void f_y_axis(){ //Funcion de paso por cero de la interrupcion del eje y
   if (((millis() - startTimey) > timeThreshold)&&(enable_zero[y_axis]))
    {
      if(angle_pulses[y_axis]>0) angle_pulses_MAX[y_axis] = angle_pulses[y_axis]; //Varaible de pulsos maximos por vuelta
      angle_pulses[y_axis] = 0; //Varaible temporal de pulsos 
      startTimey = millis();
    }
  }

void f_a_axis(){ //Funcion de paso por cero de la interrupcion del eje a
   if (((millis() - startTimea) > timeThreshold)&&(enable_zero[a_axis]))
    {
      if(angle_pulses[a_axis]>0) angle_pulses_MAX[a_axis] = angle_pulses[a_axis]; //Varaible de pulsos maximos por vuelta
      angle_pulses[a_axis] = 0; //Varaible temporal de pulsos 
      startTimea = millis();
    }
  } 


/*
Functions on the M4 that returns axis
*/
int AxisA_Read() {
  int result = angle_pulses[a_axis];
  return result;
}

int AxisX_Read() {
  int result = angle_pulses[x_axis];
  return result;
}

int AxisY_Read() {
  int result = angle_pulses[y_axis];
  return result;
}

int read_value[10];
int* decode_values(String inputString_rpc, int num_dec)
  {
    String str = inputString_rpc.substring(inputString_rpc.lastIndexOf(":") + 1);
    read_value[0] = str.substring(0, str.indexOf(',')).toInt();
    for(int i=1; i<num_dec; i++)
      {
        str = str.substring(str.indexOf(',')+1);
        read_value[i] = str.substring(0, str.indexOf(',')).toInt();
      }
    return read_value;
  }

char c_rpc;
String inputString_rpc;
void RPCRead()
  {
     ////////////////////////////////////////////////////////////
     /////// RUTINA TRATAMIENTO DE STRINGS DEL UART   ///////////
     ////////////////////////////////////////////////////////////
     if (RPC.available())
       {
         c_rpc = RPC.read();
         if ((c_rpc == '\r') || (c_rpc == '\n'))
         {
          digitalWrite(LED_BUILTIN, LOW);
          if (inputString_rpc.startsWith("/axisA:")) //Interval Motor
            {
              int* value = decode_values(inputString_rpc, 3);
              #if DEBUG_M4
                RPC.print("Recibido axis M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<3; i++) RPC.println(value[i]);
              #endif
              axis_a_angle = value[0];
              time_axis_a_angle = value[1]*1000;
              mode = ANGLE_MODE;
              flag_search[a_axis] = true;
              time_axis[a_axis] = millis();
            }
          else if (inputString_rpc.startsWith("/axisX:")) //Interval Motor
            {
              int* value = decode_values(inputString_rpc, 3);
              #if DEBUG_M4
                RPC.print("Recibido axis M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<3; i++) RPC.println(value[i]);
              #endif
              axis_x_angle = value[0];
              time_axis_x_angle = value[1]*1000;
              mode = ANGLE_MODE;
              flag_search[x_axis] = true;
              time_axis[x_axis] = millis();
            }
          else if (inputString_rpc.startsWith("/axisY:")) //Interval Motor
            {
              int* value = decode_values(inputString_rpc, 3);
              #if DEBUG_M4
                RPC.print("Recibido axis M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<3; i++) RPC.println(value[i]);
              #endif
              axis_y_angle = value[0];
              time_axis_y_angle = value[1]*1000;;
              mode = ANGLE_MODE;
              flag_search[y_axis] = true;
              time_axis[y_axis] = millis();
            }
          inputString_rpc = String();
          digitalWrite(LED_BUILTIN, HIGH);
         }
         else
           inputString_rpc += c_rpc;
       }
  }

void requestReading() {
  while (true) {
    //delay(100);
    RPCRead();
  }
}

void setup() {
  RPC.begin();
  for(int i=0; i<3; i++)
    {
      pinMode(EN[i], OUTPUT);
      pinMode(DIRP[i], OUTPUT);
      pinMode(PUL[i], OUTPUT);
      digitalWrite(EN[i], LOW);
      digitalWrite(DIRP[i], HIGH);
      digitalWrite(PUL[i], HIGH);
    }
  for(int i=0; i<3; i++) pinMode(FC[i], INPUT_PULLUP);
  search_zero();
  delay(1000);
  attachInterrupt(digitalPinToInterrupt(FC[x_axis]), f_x_axis, FALLING);
  attachInterrupt(digitalPinToInterrupt(FC[y_axis]), f_y_axis, FALLING);
  attachInterrupt(digitalPinToInterrupt(FC[a_axis]), f_a_axis, FALLING);

  flag_refresh[y_axis]=false;
  flag_refresh[x_axis]=false;
  flag_refresh[a_axis]=false;

  //Bind the sensorRead() function on the M4
  RPC.bind("AxisA", AxisA_Read);
  RPC.bind("AxisX", AxisX_Read);
  RPC.bind("AxisY", AxisY_Read);
  /*
  Starts a new thread that loops the requestReading() function
  */
  sensorThread.start(requestReading);
}

void loop() {
  search_angle(a_axis, axis_a_angle, 0, time_axis_a_angle);
  search_angle(x_axis, axis_x_angle, 0, time_axis_x_angle);
  search_angle(y_axis, axis_y_angle, 0, time_axis_y_angle);
  refresh_steppers();
}
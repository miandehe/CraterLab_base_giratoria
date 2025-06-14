#include <Arduino.h>
#include <SPI.h>
#include <RPC.h>
#include <constants.h>

using namespace rtos;

Thread sensorThread;

#define x1_axis 0
#define y0_axis 1
#define x0_axis 2

#define RIGHT false
#define LEFT true
#define STOP 0
#define ZERO 1
#define CONTINUOUS 2
#define ANGLE_MODE 3


// Dispositivo 360
int x0Degrees = 0;
unsigned long x0Duration = 10000;
int x1Degrees = 0;
unsigned long x1Duration = 10000;
int y0Degrees = 0;
unsigned long y0Duration = 10000;
bool syncx0WithInterval360 = false;
bool syncx1WithInterval360 = false;
bool syncy0WithInterval360 = false;
bool isAction[3] = {false, false, false};
int mode = STOP;


#if defined(ARDUINO_GIGA_M7)
      #define debug Serial 
#elif defined(ARDUINO_GIGA_M4) 
      #define debug RPC 
#endif

#define TIME_PULSE_XY 2500
#define TIME_PULSE_A 500

bool direction[3] = {RIGHT, RIGHT, RIGHT};

unsigned long time_fc_zero[3] = {millis(), millis(), millis()};
bool enable_zero[3] = {true, true, true};

void search_zero()
  {
    time_fc_zero[x1_axis] = millis();
    while(digitalRead(FC[x1_axis])||((millis()-time_fc_zero[x1_axis])<10))
      {
        digitalWrite(PUL[x1_axis], HIGH);
        delayMicroseconds(2000);
        digitalWrite(PUL[x1_axis], LOW);
        delayMicroseconds(2000);
        if(digitalRead(FC[x1_axis])) time_fc_zero[x1_axis] = millis();
      }
    time_fc_zero[y0_axis] = millis();
    while(digitalRead(FC[y0_axis])||((millis()-time_fc_zero[y0_axis])<10))
      {
        digitalWrite(PUL[y0_axis], HIGH);
        delayMicroseconds(2000);
        digitalWrite(PUL[y0_axis], LOW);
        delayMicroseconds(2000);
        if(digitalRead(FC[y0_axis])) time_fc_zero[y0_axis] = millis();
      };
    time_fc_zero[x0_axis] = millis();
    while(digitalRead(FC[x0_axis])||((millis()-time_fc_zero[x0_axis])<10))
      {
        digitalWrite(PUL[x0_axis], HIGH);
        delayMicroseconds(250);
        digitalWrite(PUL[x0_axis], LOW);
        delayMicroseconds(250);
        if(digitalRead(FC[x0_axis])) time_fc_zero[x0_axis] = millis();
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
    if(isAction[axis])
      {
        axis_angle[axis] = angleRead(axis);
        if((axis_angle[axis]>((float)angle+RESOLUTION_ANGLE))||(axis_angle[axis]<((float)angle-RESOLUTION_ANGLE)))
          {
            if (axis_angle_ant[axis]!=axis_angle[axis])
              {
                #if DEBUG_M4
                  RPC.print(axis_angle[axis]);
                  RPC.print(" ");
                  RPC.println(angle);
                #endif
                axis_angle_ant[axis] = axis_angle[axis];
              }
            if (flag_search[axis])
              {
                flag_search[axis] = false;
                #if DEBUG_M4
                  RPC.print(axis_angle[axis]);
                  RPC.print(" ");
                  RPC.print(angle);
                #endif
                if (axis_angle[axis]<(angle + RESOLUTION_ANGLE)) direction[axis] = RIGHT;
                else direction[axis] = LEFT;
                if ((abs(axis_angle[axis])>360)||(abs(angle)>360))enable_zero[axis] = false;
                else enable_zero[axis] = true;
                if((axis==y0_axis)||(axis==x0_axis)) direction[axis]=!direction[axis];
                digitalWrite(DIRP[axis], direction[axis]);
                if (axis!=x0_axis) axis_pulses[axis] = map(abs(angle-axis_angle[axis]), 0, 359, 0, PULSE_REV*REDUCTION_XY);
                else axis_pulses[axis] = map(abs(angle-axis_angle[axis]), 0, 359, 0, PULSE_REV*REDUCTION_A);
                time_pulse[axis] = ((1000/2)*(time_final-time_initial)/axis_pulses[axis]); //1000 es conversion de milisegundos a microsegundos entre dos porque es tiempo de 0 a 1
                if((time_pulse[axis]<TIME_PULSE_XY)&&(axis<=y0_axis)) time_pulse[axis]=TIME_PULSE_XY;
                else if((time_pulse[axis]<TIME_PULSE_A)&&(axis==x0_axis)) time_pulse[axis]=TIME_PULSE_A;
                #if DEBUG_M4
                  RPC.print(" ");
                  RPC.print(axis_pulses[axis]);
                  RPC.print(" ");
                  RPC.println(time_pulse[axis]);
                #endif
                flag_refresh[axis] = true;
              }
          }
        else if((axis_angle[axis]<=((float)angle+RESOLUTION_ANGLE))&&(axis_angle[axis]>=((float)angle-RESOLUTION_ANGLE))) flag_refresh[axis] = false;
      }
  }

unsigned long time_refresh_x = micros();
unsigned long time_refresh_y = micros();
unsigned long time_refresh_a = micros();
bool state_x = false;
bool state_y = false;
bool state_a = false;
void refresh_steppers()
  {
    if(((micros()-time_refresh_x)>=time_pulse[x1_axis])&&(flag_refresh[x1_axis]))
      {
        time_refresh_x = micros();
        digitalWrite(PUL[x1_axis], state_x);
        if(state_x) 
          {
            if (direction[x1_axis]==RIGHT) angle_pulses[x1_axis]++;
            else angle_pulses[x1_axis]--;
          }
        state_x = !state_x;      
      }
    if(((micros()-time_refresh_y)>=time_pulse[y0_axis])&&(flag_refresh[y0_axis]))
      {
        time_refresh_y = micros();
        digitalWrite(PUL[y0_axis], state_y);
        if(state_y) 
          {
            if (direction[y0_axis]==LEFT) angle_pulses[y0_axis]++;
            else angle_pulses[y0_axis]--;
          }
        state_y = !state_y;
      }
    if(((micros()-time_refresh_a)>=time_pulse[x0_axis])&&(flag_refresh[x0_axis]))
      {
        time_refresh_a = micros();
        digitalWrite(PUL[x0_axis], state_a);
        if(state_a)
          {
            if (direction[x0_axis]==LEFT) angle_pulses[x0_axis]++;
            else angle_pulses[x0_axis]--;
          } 
        state_a = !state_a;
      }
  }


long startTimex = 0;
long startTimey = 0;
long startTimea = 0;
const int timeThreshold = 500;

void f_x1_axis(){ //Funcion de paso por cero de la interrupcion del eje x
   if (((millis() - startTimex) > timeThreshold)&&(enable_zero[x1_axis]))
    {
      if(angle_pulses[x1_axis]>0) angle_pulses_MAX[x1_axis] = angle_pulses[x1_axis]; //Varaible de pulsos maximos por vuelta
      angle_pulses[x1_axis] = 0; //Varaible temporal de pulsos 
      startTimex = millis();
    }
  }

void f_y0_axis(){ //Funcion de paso por cero de la interrupcion del eje y
   if (((millis() - startTimey) > timeThreshold)&&(enable_zero[y0_axis]))
    {
      if(angle_pulses[y0_axis]>0) angle_pulses_MAX[y0_axis] = angle_pulses[y0_axis]; //Varaible de pulsos maximos por vuelta
      angle_pulses[y0_axis] = 0; //Varaible temporal de pulsos 
      startTimey = millis();
    }
  }

void f_x0_axis(){ //Funcion de paso por cero de la interrupcion del eje a
   if (((millis() - startTimea) > timeThreshold)&&(enable_zero[x0_axis]))
    {
      if(angle_pulses[x0_axis]>0) angle_pulses_MAX[x0_axis] = angle_pulses[x0_axis]; //Varaible de pulsos maximos por vuelta
      angle_pulses[x0_axis] = 0; //Varaible temporal de pulsos 
      startTimea = millis();
    }
  } 


/*
Functions on the M4 that returns axis
*/
int AxisA_Read() {
  int result = angle_pulses[x0_axis];
  return result;
}

int AxisX_Read() {
  int result = angle_pulses[x1_axis];
  return result;
}

int AxisY_Read() {
  int result = angle_pulses[y0_axis];
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
              int* value = decode_values(inputString_rpc, 4);
              #if DEBUG_M4
                RPC.print("Recibido axis M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<4; i++) RPC.println(value[i]);
              #endif
              x0Degrees = value[0];
              x0Duration = value[1]*1000;
              syncx0WithInterval360 = value[2];
              mode = ANGLE_MODE;
              if(!value[3]) //Save or test
                {
                  isAction[x0_axis] = true;
                  flag_search[x0_axis] = true;
                  time_axis[x0_axis] = millis();
                }
            }
          else if (inputString_rpc.startsWith("/axisX:")) //Interval Motor
            {
              int* value = decode_values(inputString_rpc, 4);
              #if DEBUG_M4
                RPC.print("Recibido axis M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<4; i++) RPC.println(value[i]);
              #endif
              x1Degrees = value[0];
              x1Duration = value[1]*1000;
              syncx1WithInterval360 = value[2];
              mode = ANGLE_MODE;
              if(!value[3]) //Save or test
                {
                  isAction[x1_axis] = true;
                  flag_search[x1_axis] = true;
                  time_axis[x1_axis] = millis();
                }
            }
          else if (inputString_rpc.startsWith("/axisY:")) //Interval Motor
            {
              int* value = decode_values(inputString_rpc, 4);
              #if DEBUG_M4
                RPC.print("Recibido axis M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<4; i++) RPC.println(value[i]);
              #endif
              y0Degrees = value[0];
              y0Duration = value[1]*1000;
              syncy0WithInterval360 = value[2];
              mode = ANGLE_MODE;
              if(!value[3]) //Save or test
                {
                  isAction[y0_axis] = true;
                  flag_search[y0_axis] = true;
                  time_axis[y0_axis] = millis();
                }
            }
          else if (inputString_rpc.startsWith("/action:")) //Interval Motor
            {
              int* value = decode_values(inputString_rpc, 1);
              #if DEBUG_M4
                RPC.print("Recibido action M4: ");
                RPC.println(inputString_rpc);
                for(int i=0; i<1; i++) RPC.println(value[i]);
              #endif
              if (value[0])
              {
                for(int i=0; i<3; i++)
                {
                  isAction[i] = true;
                  flag_search[i] = true;
                  time_axis[i] = millis();
                }
              }
              else
                for(int i=0; i<3; i++) 
                  {
                    isAction[i] = false;
                    flag_refresh[i] = false;
                  }
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
  //delay(10000);
  attachInterrupt(digitalPinToInterrupt(FC[x1_axis]), f_x1_axis, FALLING);
  attachInterrupt(digitalPinToInterrupt(FC[y0_axis]), f_y0_axis, FALLING);
  attachInterrupt(digitalPinToInterrupt(FC[x0_axis]), f_x0_axis, FALLING);

  flag_refresh[y0_axis]=false;
  flag_refresh[x1_axis]=false;
  flag_refresh[x0_axis]=false;

  //Bind the sensorRead() function on the M4
  RPC.bind("AxisA", AxisA_Read);
  RPC.bind("AxisX", AxisX_Read);
  RPC.bind("AxisY", AxisY_Read);
  /*
  Starts a new thread that loops the requestReading() function
  */
  //sensorThread.start(requestReading);
}

void loop() {
  search_angle(x0_axis, x0Degrees, 0, x0Duration);
  search_angle(x1_axis, x1Degrees, 0, x1Duration);
  search_angle(y0_axis, y0Degrees, 0, y0Duration);
  refresh_steppers();
  RPCRead();
}
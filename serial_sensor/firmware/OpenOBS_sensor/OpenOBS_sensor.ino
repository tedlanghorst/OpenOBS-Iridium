#include <Wire.h>               //standard library
#include "src/libs/SerialTransfer/SerialTransfer.h"
#include "src/libs/Adafruit_VCNL4010/Adafruit_VCNL4010.h"
#include "src/libs/MS5803/MS5803.h" 

uint16_t SN = 206;

SerialTransfer myTransfer;

//sensors
Adafruit_VCNL4010 vcnl;
MS_5803 pressure_sensor = MS_5803(5, 0x76, 4096);

//data storage
typedef struct single_record_t {
  uint32_t logTime: 32;
  uint16_t tuBackground: 16;    //max 2 bytes
  uint16_t tuReading: 16;       //max 2 bytes
  uint32_t hydro_p: 21;         //max value 1400000 (14 bar / 10^-5)
  int16_t water_temp: 11;        //max value +/- 500 (50 C / 10^-1)
  int16_t baroAnomaly_p: 14;   //max value 10000 (0.1 bar / 10^-5; max deviation 100 hPa)
  int16_t air_temp: 10;         //max value +/- 500 (50 C / 10^-1)
  uint8_t battery_V: 8;          //max value 150 (15 V / 10^-1)
}; //127 bits, 16 bytes
single_record_t data;

uint16_t sendSize;
byte request;

bool turb_init;
bool pressure_init;
byte sensor_init;


void setup()
{
  Serial.begin(9600);
  myTransfer.begin(Serial);

//  initialize the light sensor
  turb_init = vcnl.begin();
  vcnl.setLEDcurrent(5);
  //initialize the pressure sensor
  pressure_init = pressure_sensor.initializeMS_5803();
}


void loop()
{
  //Wait for a data request.
  if(myTransfer.available()){
    myTransfer.rxObj(request);
  }

  if(request == 1){
    sensor_init = (turb_init << 1) + pressure_init;
    //Fill buffer with initializations and then send it
    sendSize = myTransfer.txObj(sensor_init, 0);
    sendSize = myTransfer.txObj(SN, sendSize);
    myTransfer.sendData(sendSize);
    request = 0;
  }
  else if(request == 2){
    //Replace our data fields with new sensor data.
    pressure_sensor.readSensor();
    data.hydro_p = pressure_sensor.getPressure(); //bar*10^-5
    int32_t input = pressure_sensor.getTemperature()/10;
    data.water_temp = constrain(input,-250,250); //C*10^-1
    data.tuBackground = vcnl.readAmbient();
    data.tuReading = vcnl.readProximity();
    
    //Fill buffer with a data struct and then send it.
    sendSize = myTransfer.txObj(data, 0);
    myTransfer.sendData(sendSize);
    request = 0;
  }
}

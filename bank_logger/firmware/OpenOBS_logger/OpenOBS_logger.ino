/* TODO
 * Read in serial number from sensor serial transfer.
 * 
 */

#include <Wire.h>               //standard library
#include <SPI.h>                //standard library
#include <EEPROM.h>             //standard library
#include <SdFat.h>              //Version 2.0.7 https://github.com/greiman/SdFat //uses 908 bytes of memory
#include "src/libs/LowPower/LowPower.h"           
#include "src/libs/SerialTransfer/SerialTransfer.h" 
#include "src/libs/DS3231/DS3231.h"          
#include "src/libs/IridiumSBD/IridiumSBD.h"       
#include "src/libs/MS5803/MS5803.h"  

// Likely variables to change
long sleepDuration_seconds = 0;
bool useIridium = false;
const char contactInfo[] PROGMEM = "If found, please contact pavelsky@unc.edu";
//

//firmware data
const DateTime uploadDT = DateTime((__DATE__),(__TIME__)); //saves compile time into progmem
const char dataColumnLabels[] PROGMEM = "time,ambient_light,backscatter,water_pressure,water_temp,air_pressure_anomaly,air_temp,battery_level";
uint16_t serialNumber; 

//connected pins
#define pChipSelect 53      
#define pRtcInterrupt 2
#define p3V3Power 41
#define pIridiumPower 3 //need to remap pin on PCB
#define pBatteryMonitor A7

//EEPROM addresses
#define SLEEP_ADDRESS 0
#define UPLOAD_TIME_ADDRESS 502

//gui communications vars
bool guiConnected = false;
const uint16_t COMMS_WAIT = 2000; //ms delay to try gui connection
const uint8_t COMMS_TRY = 3;    
const uint8_t MAX_CHAR = 60;            //max num character in messages
char messageBuffer[MAX_CHAR];       //buffer for sending and receiving comms
#define cacheSize 23

SerialTransfer myTransfer;

//barometer
MS_5803 pressure_sensor = MS_5803(2, 0x77, 4096);

//data storage variables
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
//max message is 340 bytes, but charged per 50 bytes.
#define N_RECORDS (int(50)/sizeof(single_record_t)) 
typedef union data_union_t{
  single_record_t records[N_RECORDS];
  byte serialPacket[sizeof(single_record_t)*N_RECORDS];
};
data_union_t data, data_cachedPacket[cacheSize];
bool cachedPacket[cacheSize];

uint8_t recordCount = 0;


int16_t batteryLevel;

//initialization variable
typedef struct module_t {
  bool sd:1;
  bool clk:1;
  bool iridium:1;
  bool baro:1;
  byte sensor:2;
};
typedef union startup_t {
  module_t module;
  byte b;
};
startup_t startup;

//time settings
long currentTime = 0;
long delayedStart_seconds = 0;
DateTime nextAlarm;
DS3231 RTC; //create RTC object

//serial transfer vars
uint32_t lastRequestTime = 0;
uint16_t sendSize;

//SD vars
#define SPI_SPEED SD_SCK_MHZ(50)
char filename[] = "YYYYMMDD.TXT"; 
SdFat sd;
SdFile file;

//Iridium
#define DIAGNOSTICS false // Change this to see diagnostics
IridiumSBD modem(Serial2); 
int modemErr;

void setup(){
  Serial.begin(250000);
  Serial.setTimeout(50);
  Wire.begin();
  pinMode(p3V3Power,OUTPUT);
  pinMode(pIridiumPower,OUTPUT);
  
  //Check connection with serial sensor.
  //Also sets serial number from sensor.
  sensorWake();

//  initialize the Iridium modem.
  if (useIridium){
    startup.module.iridium = startIridium();
    int signalQuality = getIridiumSignalQuality();
  }
  else {
     startup.module.iridium = true;
  }
  
/* With power switching between measurements, we need to know what kind of setup() this is.
 *  First, check if the firmware was updated.
 *  Next, check if the GUI connection forced a reset.
 *  If neither, we assume this is a power cycle during deployment and use stored settings.
 */
  //if firmware was updated, take those settings and time.
  uint32_t storedTime;
  EEPROM.get(UPLOAD_TIME_ADDRESS,storedTime);
  if(uploadDT.unixtime()!=storedTime){
    EEPROM.put(UPLOAD_TIME_ADDRESS,uploadDT.unixtime());
    EEPROM.put(SLEEP_ADDRESS,sleepDuration_seconds);
    Serial.println("Firmware updated");
    startup.module.clk = RTC.begin(); //reset the RTC
    RTC.adjust(uploadDT);
  }  
  //otherwise check if the GUI is connected
  //send a startup message and wait a bit for an echo from the gui
  else if (checkGuiConnection());
  else {
    //if no contact from GUI, read last stored value
    EEPROM.get(SLEEP_ADDRESS,sleepDuration_seconds);
    startup.module.clk = true; //assume true if logger woke up.
  }
  
  //Initialize & check all the modules.
  startup.module.sd = sd.begin(pChipSelect,SPI_SPEED);
  startup.module.baro = pressure_sensor.initializeMS_5803();
  
  if(!startup.module.sd) serialSend("SDINIT,0");
  if(!startup.module.clk) serialSend("CLKINIT,0");
  if(!startup.module.iridium) serialSend("IRIDIUMINIT,0");
  if(!startup.module.baro) serialSend("BAROINIT,0");
  if(startup.module.sensor != 3) serialSend("SENSORINIT,0");

  //if we had any errors turn off battery power and stop program.
  //set another alarm to try again- intermittent issues shouldnt end entire deploy.
  //RTC errors likely are fatal though. Will it even wake if RTC fails?
  while(startup.b != 0b111111){ 
    nextAlarm = DateTime(RTC.now().unixtime() + sleepDuration_seconds);
    loggerSleep(nextAlarm);
    sensorWake();
    //Initialize & check all the modules.
    Serial.println(F("rechecking modules...."));
    startup.module.sd = sd.begin(pChipSelect,SPI_SPEED);
    startup.module.baro = pressure_sensor.initializeMS_5803();
    if(!startup.module.sd) serialSend("SDINIT,0");
    if(!startup.module.clk) serialSend("CLKINIT,0");
    if(!startup.module.iridium) serialSend("IRIDIUMINIT,0");
    if(!startup.module.baro) serialSend("BAROINIT,0");
    if(startup.module.sensor != 3) serialSend("SENSORINIT,0");
    Serial.println(startup.module.sensor);
  }

  //if we have established a connection to the java gui, 
  //send a ready message and wait for a settings response.
  //otherwise, use the settings from EEPROM.
  if(guiConnected){
    receiveGuiSettings();
  }

  if(delayedStart_seconds>0){
    nextAlarm = DateTime(currentTime + delayedStart_seconds);
    loggerSleep(nextAlarm);
  }
}

void loop()
{ 
  nextAlarm = DateTime(RTC.now().unixtime() + sleepDuration_seconds);

  updateFilename();
  sprintf(messageBuffer,"FILE,OPEN,%s\0",filename);
  serialSend(messageBuffer);
  
  //Request data
  while(startup.module.sensor != 3) sensorWake();
  sensorRequest(2);
  pressure_sensor.readSensor();

 //wait for data to be available
  long tStart = millis();
  while (millis() - tStart < COMMS_WAIT) {
    if(myTransfer.available()){
      myTransfer.rxObj(data.records[recordCount]);
      //fill out the logger side of data
      data.records[recordCount].logTime = RTC.now().unixtime();
      int32_t input = pressure_sensor.getPressure()-100000;
      data.records[recordCount].baroAnomaly_p = constrain(input,-10000,10000); //bar*10^-5
      input = pressure_sensor.getTemperature()/10;
      data.records[recordCount].air_temp = constrain(input,-500,500); //C*10^-1
      data.records[recordCount].battery_V = uint32_t(analogRead(pBatteryMonitor))*10*11*5/1024; //10:1 resistor divider. scaled to 10^-1 V
      writeDataToSD(data.records[recordCount]);
      Serial.print("time:\t\t");
      Serial.println(data.records[recordCount].logTime);
      Serial.print("background:\t");
      Serial.println(data.records[recordCount].tuBackground);
      Serial.print("reading:\t");
      Serial.println(data.records[recordCount].tuReading);
      Serial.print("hydro p:\t");
      Serial.println(data.records[recordCount].hydro_p);
      Serial.print("water temp:\t");
      Serial.println(data.records[recordCount].water_temp); 
      Serial.print("air p:\t\t");
      Serial.println(data.records[recordCount].baroAnomaly_p);
      Serial.print("air temp:\t");
      Serial.println(data.records[recordCount].air_temp); 
      Serial.print("battery:\t");
      Serial.println(data.records[recordCount].battery_V); 
      Serial.println();
      Serial.flush();
      recordCount += 1;
      break;
    }
  }

  //if we have filled out transmit packet...
  if(recordCount == N_RECORDS){
    recordCount = 0; //reset our packet idx
    Serial.println("~~~Packet filled!~~~");

    char hexChar[2];
    for(int i=0; i<sizeof(data.serialPacket); i++){
      sprintf(hexChar,"%02X",data.serialPacket[i]);
      Serial.print(hexChar);
    }
    Serial.println();
    int cacheCount = 0;
    for (int i=0; i<cacheSize; i++){
      cacheCount += cachedPacket[i];
    }
    Serial.print(F("Cached packets: "));
    Serial.println(cacheCount);
    Serial.println();
    Serial.println();
    if (useIridium){
      int tries = 0;
      bool messageSent = false;
      digitalWrite(pIridiumPower,HIGH);
      while (!messageSent && tries<2){
        Serial.println(F("Trying to send the message.  This might take a minute."));
        //send the current data packet.
        modemErr = modem.sendSBDBinary(data.serialPacket,sizeof(data));
        tries++;
        
        if (modemErr != ISBD_SUCCESS){
          Serial.print(F("sendSBDText failed: error "));
          Serial.println(modemErr);
          if (modemErr == ISBD_SENDRECEIVE_TIMEOUT)
            Serial.println(F("Try again with a better view of the sky."));
        } 
        else {
          Serial.println(F("Message sent!"));
          messageSent = true;
        }

        //if we had success, try clearing the message cache.
        if (messageSent){
          for (int i=0; i<cacheSize; i++){
            //send the old data if we have some.
            if (cachedPacket[i]){
              modemErr = modem.sendSBDBinary(data_cachedPacket[i].serialPacket,sizeof(data_cachedPacket[i]));
              if (modemErr == ISBD_SUCCESS){
                Serial.println(F("Cached packet sent"));
                cachedPacket[i] = false;
              } 
            }
          }
        }

        // Clear the Mobile Originated message buffer
        modemErr = modem.clearBuffers(ISBD_CLEAR_MO); // Clear MO buffer
        if (modemErr != ISBD_SUCCESS){
          Serial.print(F("clearBuffers failed: error "));
          Serial.println(modemErr);
        }
      
      Serial.println();
      }
      if (!messageSent){
        for (int i=0; i<cacheSize; i++){
          if (!cachedPacket[i]){
            data_cachedPacket[i] = data; //store failed data, try next time.
            cachedPacket[i] = true;
            break;
          }
        }
      }
    }
  }

  //ensure a 5 second margin for the next alarm before shutting down.
  //if the alarm we set during this wake has already passed, the OBS will never wake up.
  long timeUntilAlarm = nextAlarm.unixtime()-RTC.now().unixtime();
  if(timeUntilAlarm > 5){
    loggerSleep(nextAlarm);
  }
}

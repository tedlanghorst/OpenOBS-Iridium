void sensorRequest(byte request){
  sendSize = myTransfer.txObj(request,0);
  myTransfer.sendData(sendSize);
  if (request == 2) Serial.println("Sensor Request: data");
}

void sensorWake(){
  digitalWrite(p3V3Power,HIGH); // turn on sensor.
  Serial1.begin(9600);
  myTransfer.begin(Serial1);
  
  sensorRequest(1);
  long tStart = millis();
  while(startup.module.sensor != 3 && millis()-tStart<COMMS_WAIT){
    if(myTransfer.available()){
      //For some reason we need to read the transfer into a tmp object this time. 
      //Maybe something incongruous with the strcuture. Works with data struct.
      uint16_t recSize = 0;
      byte tmp;
      recSize = myTransfer.rxObj(tmp, recSize);
      startup.module.sensor = tmp;
      recSize = myTransfer.rxObj(serialNumber, recSize);
      Serial.print("Sensor S/N: ");
      Serial.println(serialNumber);
    }
  }
}

void sensorSleep(){
  digitalWrite(p3V3Power,LOW); //turn off sensor.
  Serial1.end();
  startup.module.sensor = 0;
}


void loggerSleep(DateTime alarmTime){
  // digitalWrite(pIridiumPower,LOW);
  // RTC.clearAlarm(); //clear last alarm
  // RTC.enableAlarm(alarmTime);
  // serialSend("POWEROFF,1");
  // attachInterrupt(digitalPinToInterrupt(pRtcInterrupt), wake, LOW);
  // sensorSleep();
  // delay(500); //Extra time to ensure interrupt attach and sd file.
  // LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

  //Mimic sleeping. Power-hungry alternative but I can't figure out the power issues remotely.
 delay(sleepDuration_seconds*1000); 
}

void wake(){
  Serial.println("Alarm triggered!");
  detachInterrupt(digitalPinToInterrupt(pRtcInterrupt)); 
}


void writeDataToSD(single_record_t record){
  file.open(filename, O_WRITE | O_APPEND);
  file.print(record.logTime);
  file.print(',');
  file.print(record.tuBackground);
  file.print(',');
  file.print(record.tuReading);
  file.print(',');
  file.print(record.hydro_p);
  file.print(',');
  file.print(record.water_temp);
  file.print(',');
  file.print(record.baroAnomaly_p);
  file.print(',');
  file.print(record.air_temp);
  file.print(',');
  file.println(batteryLevel); //klugey use of global variable...
  file.close();
}


//Check if the daily file exists already. If not, create one and write headers.
void updateFilename(){
  DateTime now = RTC.now();
  snprintf(filename, 13, "%04u%02u%02u.TXT", now.year(), now.month(), now.date());

  SdFile::dateTimeCallback(dateTime_callback);
  //if we create a new file with this name, set header
  if (file.open(filename, O_CREAT | O_EXCL | O_WRITE)) {
    
    snprintf(messageBuffer, 11, "%04u/%02u/%02u", uploadDT.year(), uploadDT.month(), uploadDT.date());
    
    file.println((__FlashStringHelper*)contactInfo);
    file.print(F("Firmware updated: "));
    file.println(messageBuffer);
    file.print("OpenOBS SN:");
    file.println(serialNumber);
    file.println();
    file.println((__FlashStringHelper*)dataColumnLabels);
    }
}


//callback for SD file creation date.
void dateTime_callback(uint16_t* date, uint16_t* time) {
  DateTime now = RTC.now();
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.date());
  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

//enable alarm on battery power. Normally disabled
void setBBSQW(){
  uint8_t ctReg = RTC.readRegister(DS3231_CONTROL_REG);
  ctReg |= 0b01000000;
  RTC.writeRegister(DS3231_CONTROL_REG,ctReg); 
}

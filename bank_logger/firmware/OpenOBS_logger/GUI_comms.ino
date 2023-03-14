bool checkGuiConnection() {
  guiConnected = false;
  int try_count = 0;
  while (try_count < COMMS_TRY) {
    sprintf(messageBuffer, "OPENOBS,%u", serialNumber);
    serialSend(messageBuffer);
    delay(50);
    if (serialReceive(&messageBuffer[0])) {
      if (strncmp(messageBuffer, "$OPENOBS", 8) == 0) {
        startup.module.clk = RTC.begin(); //reset the RTC
        guiConnected = true;
        return true;
      }
    }
    try_count++;
  }
  return false;
}

void receiveGuiSettings() {
  serialSend("READY");
  //wait indefinitely while user picks settings and clicks 'send' button.
  while (true) {
    delay(100);
    if (serialReceive(&messageBuffer[0])) {
      //if we receive a message, start parsing the inividual words
      //hardcoded order of settings string.
      char *tmpbuf;
      tmpbuf = strtok(messageBuffer, ",");
      if (strcmp(tmpbuf, "$SET") != 0) break; //somehow received another message.
      tmpbuf = strtok(NULL, ",");
      currentTime = atol(tmpbuf);
      tmpbuf = strtok(NULL, ",");
      sleepDuration_seconds = atol(tmpbuf);
      tmpbuf = strtok(NULL, "*");
      delayedStart_seconds = atol(tmpbuf);

      RTC.adjust(DateTime(currentTime)); //set RTC
      EEPROM.put(SLEEP_ADDRESS, sleepDuration_seconds); //store the new value.
      serialSend("SET,SUCCESS");
      delay(100);
      break;
    }
  }
}



/*function reads in the available serial data, checks for an NMEA-style sentence,
  and verifies the checksum. The function returns the result of the checksum validation
  and the sentence is stored in the pointer argument for later parsing.
*/
bool serialReceive(char *sentence) {
  //look for a $, initiating NMEA-style string.
  int idx = 0;
  while (Serial.available() > 0) {
    if (Serial.read() == '$') {
      *sentence++ = '$';
      break;
    } else if (idx++ > MAX_CHAR) {
      //read a bunch of junk. return control to loop().
      return false;
    }
  }

  //look for NMEA-style string
  idx = 1; //if we get here, $ is at idx 0.
  int idxChk = MAX_CHAR - 2;
  while (Serial.available() > 0 && idx <= idxChk + 2) {
    char incoming = Serial.read();
    if (incoming == '*') {
      idxChk = idx;
    }
    *sentence++ = incoming;
    idx++;
  }
  *sentence = '\0'; //terminate

  //returns true if we received a valid sentence
  return testChecksum((sentence - idx));
}

//takes a sentence, formats it in NMEA-style, and prints to serial.
void serialSend(char sentence[]) {
  char checksum[2];
  const char* p = generateChecksum(&sentence[0], checksum);

  Serial.print('$');
  Serial.print(sentence);
  Serial.print('*');
  Serial.print(checksum[0]);
  Serial.println(checksum[1]);
  //why did I print each checksum char separately ?
  //can't remember why this was needed.

  Serial.flush();
}

//calculates and returns the 2 char XOR checksum from sentence
const char* generateChecksum(const char* s, char* checksum) {
  uint8_t c = 0;
  // Initial $ is omitted from checksum, if present ignore it.
  if (*s == '$')
    ++s;

  //iterate through with bitwise XOR
  while (*s != '\0' && *s != '*')
    c ^= *s++;

  if (checksum) {
    checksum[0] = toHex(c / 16);
    checksum[1] = toHex(c % 16);
  }
  return s;
}

//returns true if the checksum at end of sentence matches a calculated one.
bool testChecksum(const char* s) {
  char checksum[2];
  const char* p = generateChecksum(s, checksum);
  return *p == '*' && p[1] == checksum[0] && p[2] == checksum[1];
}

static char toHex(uint8_t nibble) {
  if (nibble >= 10)
    return nibble + 'A' - 10;
  else
    return nibble + '0';
}

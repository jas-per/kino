// #define DEBUG
#include <Arduino.h>

#ifdef DEBUG
  // inputs: digital switch & trigger for debug info
  #define INPUT_I 3
  #define INPUT_II 4
  #define INPUT_DBG 2
  #define Sprint(x)     Serial.print (x)
  #define Sprintdec(x)  Serial.print (x, DEC)
  #define Sprintln(x)   Serial.println (x)
  // serial port used for debug communication -> use software serial for beamer
  #include <SoftwareSerial.h>
  SoftwareSerial beamSerial(8, 7); // Arduino RX, Arduino TX
  //debouncing for debug btn in milliS
  const long debouncingTime = 15;
  volatile unsigned long lastPressed = 0;
#else
  // input: just digital switch
  #define INPUT_I 2
  #define INPUT_II 3
  #define Sprint(x)
  #define Sprintdec(x)
  #define Sprintln(x)
  // #define beamSerial Serial
#endif

// switch state
const byte STATE_OFF = 0;
const byte STATE_DISPLAY = 1;
const byte STATE_BEAMER = 2;
// transitions
const byte TRANS_NONE = 0;
const byte TRANS_OFF = 1;
const byte TRANS_BEAMER = 2;
// outputs: light while on & motor to shut door
const byte OUTPUT_DISPLAY = 12; //red
const byte OUTPUT_BEAMER = 11; //green
// beamer protocol
const String GET_POWER_STATUS = "*pow=?#";
const String SET_POWER_OFF = "*pow=off#";
const String SET_POWER_ON = "*pow=on#";
//globals
byte cur_state;
byte new_state;
byte transition_status;
String incMsg;

byte getSwitchState() {
  if (digitalRead(INPUT_I) == LOW) {
    return STATE_OFF;
  } else if (digitalRead(INPUT_II) == LOW) {
    return STATE_BEAMER;
  } else {
    return STATE_DISPLAY;
  }
}

String sendCmd( const String cmd ) {
  Sprintln("CMD:");
  Sprintln(cmd);

  String tmpMsg = "";
  // while (true) {
  //   // empty input buffer
  //   while(beamSerial.available()) {
  //     beamSerial.read();
  //   }
  //   beamSerial.write(0x0D);
  //   beamSerial.write(cmd.c_str());
  //   beamSerial.write(0x0D);
  //   beamSerial.flush(); // wait for send

  //   // discard echo
  //   tmpMsg = beamSerial.readStringUntil('\n');
  //   // read second part
  //   tmpMsg = beamSerial.readStringUntil('\n');
  //   tmpMsg.trim();
  //   if (tmpMsg != "") {
  //     Sprintln("ANSWER:");
  //     Sprintln(tmpMsg);
  //     return tmpMsg;
  //   } else {
  //     Sprintln("ERROR - empty message, retrying communication");
  //     delay(10);
  //   }
  // }
  return tmpMsg;
}

#ifdef DEBUG
  void debugISR(){
    if((long)(micros() - lastPressed) >= debouncingTime * 1000) {
      Sprintln("DEBUG STATE:");
      Sprint("T-STAT:");
      Sprintln(transition_status);
      Sprint("C-STAT:");
      Sprintln(cur_state);
      Sprint("N-STAT:");
      Sprintln(getSwitchState());
      lastPressed = micros();
    }
  }
#endif

bool checkBootStatus(bool powerOn) {
  // if (powerOn) {
  //   incMsg = sendCmd(GET_POWER_STATUS);
  //   if (incMsg == "*POW=ON#"){
  //     return true;
  //   } else {
  //     return false;
  //   }
  // } else {
  //   // while shutting down, the beamer already returns *POW=OFF# status
  //   // so check the answer to another power off cmd instead
  //   incMsg = sendCmd(SET_POWER_OFF);
  //   if (incMsg == "*Block item#"){
  //     return false;
  //   } else {
  //     return true;
  //   }   
  // }
  return true;
}

void setPowerOn(){
  bool booted = checkBootStatus(true);
  if (booted) {
    Sprintln("State: Powered On!");
    transition_status = TRANS_NONE;
  } else {
    sendCmd(SET_POWER_ON);
    transition_status = TRANS_BEAMER;
  }
}

void setPowerOff(){
  // while (true) {
  //   incMsg = sendCmd(SET_POWER_OFF);
  //   if (incMsg == "Illegal format") {
  //     // already off, do nothing
  //     transition_status = TRANS_OFF;
  //     break;
  //   } else if (incMsg == "*POW=OFF#") {
  //     // shutdown started
  //     transition_status = TRANS_OFF;
  //     break;
  //   } else {
  //     // blocked or already in transition -> retry
  //     // after startup, beamer needs a while to be ready to shutdown again
  //     delay(50);
  //   }
  // }
  incMsg = "";
  // digitalWrite(OUTPUT_BEAMER, LOW);
}

void setup() {
  #ifdef DEBUG
    //start serial connection
    Serial.begin(9600);
    //button for debug state print
    pinMode(INPUT_DBG, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INPUT_DBG), debugISR, LOW);
  #endif
  // serial port to beamer
  // beamSerial.begin(9600);
  //configure input-pins and enable the internal pull-up resistor
  pinMode(INPUT_I, INPUT_PULLUP);
  pinMode(INPUT_II, INPUT_PULLUP);
  // outputs
  pinMode(OUTPUT_DISPLAY, OUTPUT);
  pinMode(OUTPUT_BEAMER, OUTPUT);
  //initial state
  cur_state = STATE_OFF;
  digitalWrite(OUTPUT_DISPLAY, LOW);
  digitalWrite(OUTPUT_BEAMER, LOW);
  transition_status = TRANS_NONE;
}

// OUTPUT_MOTOR: an wenn [power off]
// OUTPUT_LIGHT: an wenn [power on]
// power an -> trans pon -> solange bis getPowerStatus == *POW=ON#
// power off -> trans poff -> solange bis setPowerOff == Illegal format

void loop() {
  if (transition_status == TRANS_NONE) {
    // no current transtion: read the input pins to detect state change
    new_state = getSwitchState();
    if (new_state != cur_state) {
      Sprintln("State has changed: ");
      switch (new_state) {
        case STATE_OFF:
          digitalWrite(OUTPUT_DISPLAY, LOW);
          digitalWrite(OUTPUT_BEAMER, LOW);
          break;
        case STATE_DISPLAY:
          digitalWrite(OUTPUT_DISPLAY, HIGH);
          digitalWrite(OUTPUT_BEAMER, LOW);
          setPowerOff();
          break;
        case STATE_BEAMER:
          digitalWrite(OUTPUT_DISPLAY, HIGH);
          digitalWrite(OUTPUT_BEAMER, HIGH);
          setPowerOn();
          break;
      }
      cur_state = new_state;
    }
  } else {
    switch (transition_status) {
      case TRANS_OFF:
        if (checkBootStatus(false)) {//turned off?
          Sprintln("State: TURNED OFF!");
          transition_status = TRANS_NONE;
        }
        break;
      case TRANS_BEAMER:
        setPowerOn(); break;
    }
  }
  delay(500);        // delay in between reads for stability
}
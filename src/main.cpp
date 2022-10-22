//#define DEBUG
#include <Arduino.h>

#ifdef DEBUG
  // inputs: digital switch & trigger for debug info
  #define INPUT_I 3
  #define INPUT_II 4
  #define INPUT_DBG 2
  #define Sprint(x)     Serial.print (x)
  #define Sprintdec(x)  Serial.print (x, DEC)
  #define Sprintln(x)   Serial.println (x)
  // serial port used for debug communication -> use software serial for projector
  #include <SoftwareSerial.h>
  SoftwareSerial projector_serial(7, 8); // Arduino RX, Arduino TX
  //debouncing for debug btn in milliS
  const long debouncing_time = 15;
  volatile unsigned long last_keypress = 0;
#else
  // input: just digital switch
  #define INPUT_I 3
  #define INPUT_II 4
  #define Sprint(x)
  #define Sprintdec(x)
  #define Sprintln(x)
  #define projector_serial Serial
#endif

// switch state
const byte STATE_OFF = 0;
const byte STATE_DISPLAY = 1;
const byte STATE_PROJECTOR = 2;
// transitions
const byte TRANS_NONE = 0;
const byte TRANS_OFF = 1;
const byte TRANS_PROJECTOR = 2;
// outputs: react to state by signal light, motor to shut door etc
const byte OUTPUT_DISPLAY = 12; // cur red light
const byte OUTPUT_PROJECTOR = 11; // cur green light
// relay for projector power supply
const byte OUTPUT_RELAY = 9;
// some useful cmds from the projector protocol
const String GET_POWER_STATUS = "*pow=?#";
const String SET_POWER_OFF = "*pow=off#";
const String SET_POWER_ON = "*pow=on#";
const String GET_ERROR_STATUS = "*error=report#";
const String GET_ERROR_ENABLE = "*error=enable#";
const String GET_LED_STATUS = "*led=?#";
//globals
byte cur_state;
byte new_state;
byte transition_status;
String inc_msg;
bool print_debug;
byte com_retry;


/* 3 way switch: turn on small status display only or projector as well */
byte getSwitchState() {
  if (digitalRead(INPUT_I) == LOW) {
    return STATE_OFF;
  } else if (digitalRead(INPUT_II) == LOW) {
    return STATE_PROJECTOR;
  } else {
    return STATE_DISPLAY;
  }
}


/* send command to projector via rs232 */
String sendCmd( const String cmd ) {
  Sprint("CMD: ");
  Sprintln(cmd);

  String tmp_msg = "";
  while (true) {
    // empty input buffer
    while(projector_serial.available()) {
      projector_serial.read();
    }
    projector_serial.write(0x0D);
    projector_serial.write(cmd.c_str());
    projector_serial.write(0x0D);
    projector_serial.flush(); // wait for send

    // discard echo
    tmp_msg = projector_serial.readStringUntil('\n');
    // read second part
    tmp_msg = projector_serial.readStringUntil('\n');
    tmp_msg.trim();

    if (tmp_msg != "") {
      Sprint("ANSWER: ");
      Sprintln(tmp_msg);
      com_retry = 0;
      return tmp_msg;
    } else {
      // under some conditions commands may fail
      // even if connections, baud rate etc are all setup fine
      // just retry a couple of times, if that doesn't work
      // the projector decided to err out but won't tell (yet)
      if (com_retry < 10) {
        Sprintln("ERROR - empty message, retrying communication");
        com_retry = com_retry + 1;
        delay(200);        
      } else {
        Sprintln("retried 10 times, communication failed:");
        com_retry = 0;
        return "";
      }
    }
  }
}


#ifdef DEBUG

  /* Interrupt service handler: just toggle flag and write debug output in main loop */
  void debugISR(){
    if((long)(micros() - last_keypress) >= debouncing_time * 1000) { 
      print_debug = true;
      last_keypress = micros();
    }
  }

  /* prints some projector state info for debug */
  void printDebug(){
    Sprintln("DEBUG STATE:");
    inc_msg = sendCmd(GET_POWER_STATUS);
    Sprintln("GET_POWER_STATUS");
    Sprintln(inc_msg);
    inc_msg = sendCmd(GET_ERROR_ENABLE);
    inc_msg = sendCmd(GET_ERROR_STATUS);
    Sprintln("GET_ERROR_STATUS");
    Sprintln(inc_msg);
    inc_msg = sendCmd(GET_LED_STATUS);
    Sprintln("GET_LED_STATUS");
    Sprintln(inc_msg);
    inc_msg = sendCmd(SET_POWER_ON);
    Sprintln("SET_POWER_ON");
    Sprintln(inc_msg);
    inc_msg = sendCmd(SET_POWER_OFF);
    Sprintln("SET_POWER_OFF");
    Sprintln(inc_msg);
    
    print_debug = false;
  }
#endif


/* startup sequence for projector:
   - uses relay to supply power to projector
   - if startup fails power is cut and the whole sequence is restarted
   - might take a while until the projector starts successfully
     (takes up to ten minutes with my borked TH683 from 2018)
*/
void setPowerOn(){
  // connect power if currently not connected
  if (digitalRead(OUTPUT_RELAY)){
    Sprintln("Projector: turn power on!");
    digitalWrite(OUTPUT_RELAY, LOW);
    // wait a bit until projector boots up
    delay(5000);
    // then tell it to turn on
    sendCmd(SET_POWER_ON);
    delay(5000);
  }
  // check if already powered on (takes ~20seconds)
  inc_msg = sendCmd(GET_POWER_STATUS);
  if (inc_msg == "*POW=ON#"){
    // if answer to GET_POWER_STATUS is *POW=ON# everything is fine!
    Sprintln("State: Powered On!");
    digitalWrite(OUTPUT_DISPLAY, HIGH);
    digitalWrite(OUTPUT_PROJECTOR, HIGH);
    transition_status = TRANS_NONE;
  } else {
    // periodically check if still starting up or startup failed
    inc_msg = sendCmd(SET_POWER_ON);
    if (inc_msg == "*Block item#"){
      Sprintln("Projector: turn power on blocked!");
      // keep waiting
      delay(200);
    } else if (inc_msg != "") {
      // if SET_POWER_ON returns anything other than *Block item there is a problem
      Sprintln("Projector: FAILED to startup !");
      // -> disconnect power, wait 60 seconds and try again
      digitalWrite(OUTPUT_RELAY, HIGH);
      delay(60000);
    }
    transition_status = TRANS_PROJECTOR;
  }
}


/* initialize shutdown sequence */
void setPowerOff(){
  // only need to init shutdown/disconnect if projector currently gets power
  if (!digitalRead(OUTPUT_RELAY)) {
    while (true) {
      inc_msg = sendCmd(SET_POWER_OFF);
      if (inc_msg == "Illegal format") {
        // already off, do nothing
        transition_status = TRANS_OFF;
        break;
      } else if (inc_msg == "*POW=OFF#") {
        // shutdown started
        transition_status = TRANS_OFF;
        break;
      } else {
        // blocked or already in transition -> retry
        // after startup, projector needs a while to be ready to shutdown again
        delay(500);
      }
    }
  }
}


/* setup input/output pins and serial connection */
void setup() {
  #ifdef DEBUG
    //start serial connection
    Serial.begin(9600);
    //button for debug state print
    pinMode(INPUT_DBG, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INPUT_DBG), debugISR, LOW);
  #endif
  // serial port to projector
  projector_serial.begin(9600);
  //configure input-pins and enable the internal pull-up resistor
  pinMode(INPUT_I, INPUT_PULLUP);
  pinMode(INPUT_II, INPUT_PULLUP);
  // outputs
  pinMode(OUTPUT_DISPLAY, OUTPUT);
  pinMode(OUTPUT_PROJECTOR, OUTPUT);
  pinMode(OUTPUT_RELAY, OUTPUT);
  //initial state
  cur_state = STATE_OFF;
  digitalWrite(OUTPUT_DISPLAY, LOW);
  digitalWrite(OUTPUT_PROJECTOR, LOW);
  digitalWrite(OUTPUT_RELAY, HIGH); // normally open relay, low trigger
  transition_status = TRANS_NONE;

  com_retry = 0;
  print_debug = false;
}


/* main loop */
void loop() {
  if (transition_status == TRANS_NONE) {
    // no current transtion: read the input pins to detect state changes
    new_state = getSwitchState();
    if (new_state != cur_state) {
      Sprint("State has changed: ");
      Sprintln(new_state);
      switch (new_state) {
        case STATE_OFF:
          digitalWrite(OUTPUT_DISPLAY, LOW);
          digitalWrite(OUTPUT_PROJECTOR, LOW);
          setPowerOff();
          break;
        case STATE_DISPLAY:
          digitalWrite(OUTPUT_DISPLAY, HIGH);
          digitalWrite(OUTPUT_PROJECTOR, LOW);
          setPowerOff();
          break;
        case STATE_PROJECTOR:
          digitalWrite(OUTPUT_DISPLAY, HIGH);
          // OUTPUT_PROJECTOR will turn on only AFTER the projector has booted
          setPowerOn();
          break;
      }
      cur_state = new_state;
    }
  } else {
    switch (transition_status) {
      case TRANS_OFF:
        // while shutting down, the projector already returns *POW=OFF# status
        // so check the answer to another power off cmd instead
        inc_msg = sendCmd(SET_POWER_OFF);
        // block item means currently shutting down, also ignore empty responses
        if (inc_msg != "" && inc_msg != "*Block item#") {
          Sprintln("Projector: turned off!");
          transition_status = TRANS_NONE;
          // disconnect power to projector if currently connected
          if (!digitalRead(OUTPUT_RELAY)){
            delay(2000);
            digitalWrite(OUTPUT_RELAY, HIGH);
            Sprintln("Projector: power disconnected");
            delay(20000);
          }
        }
        break;
      case TRANS_PROJECTOR:
        setPowerOn(); break;
    }
  }
  #ifdef DEBUG
    if (print_debug) {
      printDebug();
    }
  #endif
  delay(500);        // delay in between reads for stability
}

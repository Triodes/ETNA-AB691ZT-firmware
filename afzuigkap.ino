/**
 * Power colors:
 * BLACK: 5v, WHITE: ground
 * 
 * The original hood board uses single pins for both controlling the LEDs and reading the buttons
 * Our analog pins are connected to the negative side of the series resistor of the status leds and the 'floating' side of the button.
 * Our pins are in OUTPUT mode most of the time pulling them LOW to turn the LEDs on
 * They are toggled to INPUT_PULLUP 60 times per second and will then read close to max value (1024) when the button is unpressed
 * When the button is pressed the pin will be pulled to around 3v (600 analog value) through a 10k resistor.
 * Led/Button pin colors in order from 0 to 4:
 * RED: OFF, ORANGE: 1, GREY: 2, WHITE: 3+, BLACK: LIGHT
 *
 * The output control pins are connected to a ULN2003 Darlington array and switch relays for the fan pins and most likely a MOSFET for the LIGHT.
 * Fan and light pin wire colors:
 * PURPLE: fan1,  BLUE: fan2, YELLOW: fan3, GREEN: LIGHT
 */

const int blCount = 5;
/**
 * RED, ORANGE, GREY, WHITE, BLACK
 * connected to A4, A5, A1, A2, A3 in order
 * for buttons OFF, 1, 2, 3+, LIGHT in order
 */
const int buttonLedPins[] = {A4, A5, A1, A2, A3}; 
int statusLedStates[] = {HIGH, HIGH, HIGH, HIGH, HIGH}; // These are inverted, so all OFF by pulling them HIGH
int bCurrVals[blCount];
int bPrevVals[blCount];

const int lightPin = 3; // GREEN, output toggling LIGHT. HIGH == ON
bool lightState = false;

int fanState = 0; // FAN states, 0 == OFF, 4 == MAX aka 3+
const int fanPin1 = 5; // PURPLE
const int fanPin2 = 4; // BLUE
const int fanPin3 = 2; // YELLOW

/**
 * OUTPUT pin to ESP32, pseudo open collector, 
 * pulled LOW when LIGHT is OFF, FLOATING when light is on. So requires INPUT_PULLUP on ESP side.
 */
const int espLightStatePin = 9;

/**
 * INPUT pin to ESP32, Analog, Normally pulled LOW by ESP32
 * pulsed to 3.3v for 20ms to request LIGHT toggle
 * Reads anolog value of about 660 when pulsed
 */
const int espLightTogglePin = A7;

int espLightTogglePrev = 0;
int espLightToggleCurr = 0;

/**
 * Put all the pins in their starting pinMode
 * Then write the initial value of OFF to the pins by calling the respective update functions
 * LOW for all the outputs
 * HIGH for all the status LEDS
 * LOW to the ESP32 status pin
 */
void setup() {
  for (int i = 0; i < blCount; i++) {
    pinMode(buttonLedPins[i], OUTPUT);
  } 
  updateLeds();

  pinMode(lightPin, OUTPUT);

  pinMode(espLightStatePin, OUTPUT);
  delayMicroseconds(50);
  digitalWrite(espLightStatePin, LOW);

  pinMode(espLightTogglePin, INPUT);
  updateLight();

  pinMode(fanPin1, OUTPUT);
  pinMode(fanPin2, OUTPUT);
  pinMode(fanPin3, OUTPUT);
  updateFan();
}

unsigned long millisPrev = 0;

/**
 * Loop 60 times per second
 * This is enough to catch all human/ESP32 inputs
 * but leaves the status LED pins in OUTPUT mode for a larger amount of time
 * making the LEDs nice and bright.
 */
void loop() {
  unsigned long millisCurr = millis();
  if (millisCurr - millisPrev > 16) {
    millisPrev = millisCurr;
    loop60();
  }
}

int counter = 0;
void loop60() {
  // Read all the button values
  // Their analogRead values drop by around 350 when the button is pressed
  // So when the value in the current loop is at least 300 lower compared to the previous loop we detect it as a button press
  for (int i = 0; i < blCount; i++) {
    readButtonState(i);
    if (bPrevVals[i] - bCurrVals[i] > 300)
      buttonPressed(i);
  }
  
  //Check if the ESP32 requested a LIGHT toggle by pulsing the input pin to 3.3v (about 660 analog value)
  espLightTogglePrev = espLightToggleCurr;
  espLightToggleCurr = analogRead(espLightTogglePin);
  if (espLightToggleCurr - espLightTogglePrev > 600)
    toggleLight();
  
  // Simple loop being called 2 times per second
  // For blinking the 3+ LED when in state 4 aka 3+
  counter++;
  if (counter == 30) {
    counter = 0;
    if (fanState == 4) {
      statusLedStates[3] = !statusLedStates[3];
      updateLeds(3);
    }
  }
}

/**
 * Reads the button analog values by quickly switching to input mode
 * and then switching back to output mode and writing the last known value
 */
int readButtonState(int index) {
  int pin = buttonLedPins[index];
  bPrevVals[index] = bCurrVals[index];
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(50);
  bCurrVals[index] = analogRead(pin);
  delayMicroseconds(50);
  pinMode(pin, OUTPUT);
  delayMicroseconds(50);
  digitalWrite(pin, statusLedStates[index]);
}

/**
 * Handles button presses
 * Since the first 3 buttons have an index matching the corresponding FAN target state
 * we can simply set the fan state to the index and update the corresponding output pins
 * 
 * When button with index 3 (the 3+ button) is pressed we need to check the current state
 * and flip-flop between state 3 and 4 (aka 3 and 3+).
 * 
 * When the last button is pressed we simply toggle the LIGHT.
 */
void buttonPressed(int index) {
  switch (index) {
    case 0:
    case 1:
    case 2:
      fanState = index;
      updateFan();
      break;
    case 3:
      if (fanState == 3)
        fanState = 4;
      else
        fanState = 3;
      updateFan();
      break;
    case 4:
      toggleLight();
      break;
  }
}

// Toggles the LIGHT state and updates pins accordingly
void toggleLight() {
  lightState = !lightState;
  updateLight();
}

void updateLight() {
  digitalWrite(lightPin, lightState); //HIGH to turn on the LIGHT;
  
  if (lightState) {
    //Output HIGH to ESP if LIGHT is on by making pin floating. Pullup on ESP will pull pin HIGH.
    pinMode(espLightStatePin, INPUT);
  }
  else {
    //Output LOW to ESP if LIGHT is off by setting pin to OUTPUT mode and pulling the pin LOW. Pullup on ESP will be overriden by this.
    pinMode(espLightStatePin, OUTPUT);
    delayMicroseconds(50);
    digitalWrite(espLightStatePin, LOW); 
  }

  //LOW to turn on the corresponding status LED when the LIGHT is on
  statusLedStates[4] = !lightState;
  updateLeds(4);
}

int fanPinStates[5][7] {
  { LOW,  LOW,  LOW,  /* <- Fan pins | LED pins -> */ HIGH, HIGH, HIGH, HIGH }, // OFF                All pins OFF          All fan status LEDs off
  { HIGH, LOW,  LOW,  /* <- Fan pins | LED pins -> */ LOW,  LOW,  HIGH, HIGH }, // state 1            fan pin 1 HIGH        LEDs OFF and 1 ON
  { LOW,  HIGH, LOW,  /* <- Fan pins | LED pins -> */ LOW,  HIGH, LOW,  HIGH }, // state 2            fan pin 2 HIGH        LEDs OFF and 2 ON
  { HIGH, HIGH, LOW,  /* <- Fan pins | LED pins -> */ LOW,  HIGH, HIGH, LOW  }, // state 3            fan pins 1 & 2 HIGH   LEDs OFF and 3+ ON
  { HIGH, LOW,  HIGH, /* <- Fan pins | LED pins -> */ LOW,  HIGH, HIGH, LOW  }  // state 4 (aka 3+)   fan pins 1 & 3 HIGH   LED OFF ON and 3+ BLINKING 
};

/**
 * Writes the corresponding values for the current fan state to the fan pins
 * Then sets the status LED states and writes the values
 */
void updateFan() {
  digitalWrite(fanPin1, fanPinStates[fanState][0]);
  digitalWrite(fanPin2, fanPinStates[fanState][1]);
  digitalWrite(fanPin3, fanPinStates[fanState][2]);

  statusLedStates[0] = fanPinStates[fanState][3];
  statusLedStates[1] = fanPinStates[fanState][4];
  statusLedStates[2] = fanPinStates[fanState][5];
  statusLedStates[3] = fanPinStates[fanState][6];

  updateLeds();
}

// Write the actual status LED values to the corresponding pins for all status LEDs
void updateLeds() {
  updateLeds(-1);
}

/**
 * Writes the actual status of a specific status LED to its corresponding pin
 * Or does this for all status LEDs when index == -1
 */
void updateLeds(int index) {
  if (index == -1) {
    for (int i = 0; i < blCount; i++) {
      digitalWrite(buttonLedPins[i], statusLedStates[i]);
    }
    return;
  }
  
  digitalWrite(buttonLedPins[index], statusLedStates[index]);
}

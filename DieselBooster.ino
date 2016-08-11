#include <EEPROM.h>
#include "BufferedSerial.h"
#include "ByteBuffer.h"

#define CURVE_POINTS 15
//const char CONFIG_VERSION[4] = {'d', 'b', '2', '\0'};
#define CONFIG_VERSION "db2"
#define CONFIG_START 32
#define ADC_IN A0
#define ADC2_IN A1
#define DAC_OUT 9
#define LED 13
#define RELAY_OUT 8
#define DEFAULT_ON_SWITCH 7

#define OFFSET_MIDPOINT 128
#define VALUE_0V5 103
#define VALUE_5V0 1024
// how many configurations should we be allowed to store in EEPROM
#define MAX_CONFIGURATIONS 10

const char BOOT_HEADER[] = "DieselBooster build:" __DATE__ " " __TIME__ " version:" CONFIG_VERSION;

struct SettingsStorage {
  char version[4];
  uint16_t minRange, maxRange;
  byte curve[CURVE_POINTS]; // 100 means 100%
  byte globalGain; // applies to all values to correct output impedance; 100 means 100%
  byte globalOffset; // applies to all values to correct output impedance; 0 means -128 (OFFSET_MIDPOINT), 255 means +127
} settings = {
  CONFIG_VERSION,
  VALUE_5V0, VALUE_5V0,
  {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100},
  100,
  OFFSET_MIDPOINT
};

struct SettingsStorage temp_settings;


int lastReadValue,
    lastReadValue2,
    lastWrittenValue,
    lastCurvePoint;
boolean moduleEnabled = false,
        calibrationTriggered = false,
        moduleEnabledSwitch = false;
int calibrationInValueBefore, calibrationOutValueBefore, calibrationInValueAfter, calibrationOutValueAfter;
BufferedSerial serial = BufferedSerial(256, 256);
ByteBuffer send_buffer;
FILE serial_stdout;

int serial_putchar(char c, FILE* f) {
  if (c == '\n') serial_putchar('\r', f);
  return send_buffer.put(c);
}

void forceFlush() {
  serial.sendRawSerial(&send_buffer);// force flush
}

boolean loadConfig(int slot) {
  int OFFSET = CONFIG_START + sizeof(settings) * slot;
  // To make sure there are settings, and they are YOURS!
  // If nothing is found it will use the default settings.
  if (EEPROM.read(OFFSET + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(OFFSET + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(OFFSET + 2) == CONFIG_VERSION[2])
  { // reads settings from EEPROM
    for (unsigned int t = 0; t < sizeof(settings); t++)
      *((byte*)&temp_settings + t) = EEPROM.read(OFFSET + t);
    return true;
  } else {
    // settings aren't valid! will overwrite with default settings
    //saveConfig();
    return false;
  }
}

boolean saveConfig(int slot) {
  int OFFSET = CONFIG_START + sizeof(settings) * slot;
  boolean result = true;
  for (unsigned int t = 0; t < sizeof(settings); t++)
  { // writes to EEPROM
    EEPROM.update(OFFSET + t, *((byte*)&settings + t));
    // and verifies the data
  }

  // display saved and current config
  for (unsigned int t = 0; t < sizeof(settings); t++) {
    printf("%% %d\d%d\t%d\n", t, EEPROM.read(OFFSET + t), *((byte*)&settings + t));
    if ((EEPROM.read(OFFSET + t) != *((byte*)&settings + t))) {
      // error writing to EEPROM
      result = false;
    }
  }

  return result;
}

/**
   Displays the current user-adjustable settings
*/
void outputSettings(SettingsStorage settings) {
  printf("{sX");
  printf(CONFIG_VERSION);
  printf("m%dM%dG%dF%dC", settings.minRange, settings.maxRange, settings.globalGain, settings.globalOffset);
  for (unsigned int t = 0; t < CURVE_POINTS; t++) {
    printf("%d", settings.curve[t]);
    if (t < (CURVE_POINTS - 1)) printf(",");
  }
  printf("}\n");
}

void displayCalibrationValues() {
  printf("%% InValueBefore:%d  InValueAfter:%d  offset(-128):%d gain(x100):%d", calibrationInValueBefore, calibrationInValueAfter, calibrationInValueBefore - calibrationInValueAfter, (uint16_t)calibrationInValueBefore * 100 / calibrationInValueAfter);
  printf(" OutValueBefore:%d  OutValueAfter:%d  offset(-128):%d gain(x100):%d\n", calibrationOutValueBefore, calibrationOutValueAfter, calibrationOutValueBefore - calibrationOutValueAfter, (uint16_t)calibrationOutValueBefore * 100 / calibrationOutValueAfter);
}

/**
   Check the serial input for commands and processes them.
*/
void readTelemetry(ByteBuffer* packet) {
  char cmd, cmd2, point;
  int eeprom_slot = 0;

  if (packet -> getSize() > 0) {
    char cmd = (char)packet -> get();
    printf("<%c\n", cmd);
    switch (cmd) {
      // WARNING: NO MORE THAN 255 CHARS CAN BE SENT VIA SERIAL AT ONCE
      case 'h':
        printf("%% e:enable | d:disable | rm:show min | rM:show max | rc:show all\n");
        printf("%% sm[XXX]:set min | sM[XXX]:set max | sC[X][XXX]:set curve point\n");
        forceFlush();
        printf("%% sG[XXX]:set global gain | sF[XXX] set global offset (-128)\n");
        printf("%% m: take calibration measurement | c: display last cal values\n");
        forceFlush();
        printf("%% sX[...]:set all |S[X]:save to eeprom | L[X]:load from eeprom\n");
        printf("%% ~:soft reset | h:help\n");
        break;
      case 'e':
        moduleEnabled = true;
        printf(">OK\n");
        break;
      case 'm':
        moduleEnabled = false;
        calibrationTriggered = true;
        printf(">OK\n");
        break;
      case 'c':
        displayCalibrationValues();
        break;
      case 'd':
        moduleEnabled = false;
        printf(">OK\n");
        break;
      case 'r':
        cmd2 = (char)packet -> get();
        switch (cmd2) {
          case 'm':
            printf("%d\n", settings.minRange);
            break;
          case 'M':
            printf("%d\n", settings.maxRange);
            break;
          case 'c':
            outputSettings(settings);
            break;
          default:
            printf(">r?\n");
        }
        break;
      case 's':
        cmd2 = (char)packet -> get();
        switch (cmd2) {
          case 'm':
            settings.minRange = packet -> parseInt();
            printf(">OK\n");
            break;
          case 'M':
            settings.maxRange = packet -> parseInt();
            printf(">OK\n");
            break;
          case 'G':
            settings.globalGain = packet -> parseInt();
            printf(">OK\n");
            break;
          case 'F':
            settings.globalOffset = packet -> parseInt();
            printf(">OK\n");
            break;
          case 'C':
            point = (char)packet->get() - 48; //WARNING! only 0-9 are accepted here
            if (point < CURVE_POINTS) {
              settings.curve[point] = packet -> parseInt();
              printf(">OK\n");
            } else {
              printf(">sC?\n");
            }
            break;
          case 'X':
            if (packet->get() == CONFIG_VERSION[0] && packet->get() == CONFIG_VERSION[1] && packet->get() == CONFIG_VERSION[2]) { // compare read settings with the version (e.g. "db2")
              // TODO: make a fallback and/or checksum
              packet->get();//m
              settings.minRange = packet -> parseInt();
              //packet->get();//M
              settings.maxRange = packet -> parseInt();
              //packet->get();//G
              settings.globalGain = packet -> parseInt();
              //packet->get();//F
              settings.globalOffset = packet -> parseInt();
              for (char i = 0; i < CURVE_POINTS; i++) {
                settings.curve[i] = packet -> parseInt();
              }
              printf(">OK\n");
            } else {
              printf(">sX?\n");
            }
            break;
          default:
            printf(">s?\n");
        }
        break;
      case 'S':
        if (packet->getSize() > 0) {
          // was a saving slot given? if no, default to 0
          eeprom_slot = packet->parseInt();
        }
        if (eeprom_slot >= 0 && eeprom_slot <= MAX_CONFIGURATIONS && saveConfig(eeprom_slot)) {
          printf(">OK\n");
        } else {
          printf(">ERR\n");
        }
        break;
      case 'L':
        if (packet->getSize() > 0) {
          // was a saving slot given? if no, default to 0
          eeprom_slot = packet -> parseInt();
        }
        if (eeprom_slot >= 0 && eeprom_slot <= MAX_CONFIGURATIONS && loadConfig(eeprom_slot)) {
          settings = temp_settings;
          printf(">OK\n");
        } else {
          printf(">ERR\n");
        }
        break;
      case 'O':
        if (packet->getSize() > 0) {
          // was a saving slot given? if no, default to 0
          eeprom_slot = packet -> parseInt();
        }
        if (eeprom_slot >= 0 && eeprom_slot <= MAX_CONFIGURATIONS && loadConfig(eeprom_slot)) {
          outputSettings(temp_settings);
        } else {
          printf(">ERR\n");
        }
        break;
      case '~':
        asm volatile ("  jmp 0");
        break;
      default:
        printf(">?\n");
    }
  }
  // empty buffer
  packet -> clear();
}

/**
   Given an input value determines the index of adjustment curve point.
   This is done by splitting the [min,max] range into equal bins and seeing into which bin the value fits.
   If no such bin is found or there are errors, -1 will be returned
*/
int getCurvePoint(int inputValue) {
  int result = -1;
  if ((inputValue > settings.minRange) && (inputValue < settings.maxRange)) {
    int binSize = (settings.maxRange - settings.minRange) / CURVE_POINTS;
    if (binSize > 0) {
      int point = (int)((inputValue - settings.minRange - binSize / 2) / binSize);
      if (point < CURVE_POINTS) {
        result = point;
      }
    }
  }
  return result;
}

/**
   Given an input value (from ADC) it determines the adjustment curve point position and applies the selected gain.
   Afterwards it applies the global gain and offset to the value.
*/
int getAdjustedValue(int inputValue) {
  int curvePoint = getCurvePoint(inputValue);
  lastCurvePoint = curvePoint;
  float result = inputValue; // do all calculations in float

  // do not alter value if no valid curve point was found
  if (curvePoint >= 0) {
    result = (int)((float)settings.curve[curvePoint] * inputValue / 100 );
    if (result == 0) { // sanity check
      result = inputValue;
    }
  }
  result = result * settings.globalGain / 100 + (settings.globalOffset - OFFSET_MIDPOINT);
  return (int)result;
}

/**
   The high priority loop which processes the ADC input and sets the PWM (DAC) output.
   No slow operations (e.g. serial) should be performed here
*/
void processADC() {
  if (moduleEnabled) {
    digitalWrite(LED, 1);
    digitalWrite(RELAY_OUT, 0);
  } else {
    digitalWrite(RELAY_OUT, 1);
  }

  lastReadValue2 = analogRead(ADC2_IN);
  lastReadValue = analogRead(ADC_IN);
  if (moduleEnabled) {
    lastWrittenValue = getAdjustedValue(lastReadValue);
  } else {
    lastWrittenValue = lastReadValue;
  }
  analogWrite(DAC_OUT, lastWrittenValue >> 2);
  digitalWrite(LED, 0);
}

void outputCurrentValues() {
  printf("*i:%d i2:%d o:%d p:%d en:%d\n", lastReadValue, lastReadValue2, lastWrittenValue, lastCurvePoint, moduleEnabled);
}

void handlePacket(ByteBuffer* packet) {
  readTelemetry(packet);
}

void highPrioFunction() {
  processADC();

  serial.update();

  // send more bytes at once, there is a 64B buffer anyway
  for (byte i = 0; i < 16; i++) {
    if ( send_buffer.getSize() > 0 ) {
      serial.sendSerialByte( send_buffer.get() );
    }
  }
}

void lowPrioFunction() {
  digitalWrite(LED, 1);

  // allow user to force toggle the on/off state via the DEFAULT_ON switch
  if (digitalRead(DEFAULT_ON_SWITCH) == moduleEnabledSwitch) {
    moduleEnabled = !moduleEnabled;
    moduleEnabledSwitch = !moduleEnabledSwitch;
  }

  // display last runtime values
  outputCurrentValues();

  // process serial inputs
  //readTelemetry();

  // runs a calibration procedure which disables the output, takes measurements, enables the output and takes another set of measurements
  // initially the module will be set to disabled by the telemetry function
  if (calibrationTriggered) {
    // TODO: make a running average
    if (!moduleEnabled) {
      calibrationInValueBefore = lastReadValue;
      calibrationOutValueBefore = lastReadValue2;
      // enable back the module
      moduleEnabled = true;
    } else {
      // we have run a calibration with the module enabled, now store the new measurements and disable the module
      calibrationInValueAfter = lastReadValue;
      calibrationOutValueAfter = lastReadValue2;
      moduleEnabled = false;
      calibrationTriggered = false;
      displayCalibrationValues();
    }
  }
  // if the led is stuck on high brightness for too long then it means something is eating time from the high priority loop
  digitalWrite(LED, 0);
}

int delayScale;
void setup() {
  //Serial.begin(9600);
  //Serial.setTimeout(100);

  // initialize the serial communication:
  serial.init(0, 9600);
  serial.setPacketHandler(handlePacket);
  fdev_setup_stream(&serial_stdout, serial_putchar, NULL, _FDEV_SETUP_WRITE);
  stdout = &serial_stdout;

  // Initialize the send buffer that we will use to send data
  send_buffer.init(255);

  //  pinMode(LED, OUTPUT);
  //pinMode(ADC_IN, INPUT_PULLUP); // use this if you need a weak pullup

  // TODO: figure out why the relay is on for ~100ms after setting pin to output
  analogWrite(DAC_OUT, VALUE_0V5); // write 0.5V to the PWM output in case something bad happens
  // set relay out to off before setting it to output
  digitalWrite(RELAY_OUT, 0);
  pinMode(RELAY_OUT, OUTPUT);
  digitalWrite(RELAY_OUT, 0);
  //delay(500);

  printf("%s\n", BOOT_HEADER);

  if (loadConfig(0)) {
    printf("%% Load settings: OK\n");
    settings = temp_settings;
  } else {
    printf("%% Load settings: FAIL\n");
  }
  forceFlush();
  outputSettings(settings);

  pinMode(DEFAULT_ON_SWITCH, INPUT_PULLUP);
  if (digitalRead(DEFAULT_ON_SWITCH) == 0) {
    moduleEnabled = true;
    moduleEnabledSwitch = true;
  }

  // see http://playground.arduino.cc/Main/TimerPWMCheatsheet
  TCCR1B = (TCCR1B & 0b11111000) | 0x01; // pins 9 & 10; default is 0x03, 0x01 sets pwm frequency to ~31kHz
  TCCR0B = (TCCR0B & 0b11111000) | 0x01; // pins 5 & 6; default is 0x03, 0x01 sets pwm frequency to ~62kHz
  // if you mess with the above, set the delay scale appropriately
  delayScale = 64;
}

int smallLoops;
void loop() {
  highPrioFunction();

  if (smallLoops >= 200) {
    lowPrioFunction();
    smallLoops = 0;
  }
  smallLoops++;
  delay(5 * delayScale);
}


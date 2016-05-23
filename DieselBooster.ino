#include <EEPROM.h>

#define CURVE_POINTS 10
//const char CONFIG_VERSION[4] = {'d', 'b', '1', '\0'};
#define CONFIG_VERSION "db1"
#define CONFIG_START 32
#define ADC_IN A0
#define DAC_OUT 9
// 9 could be output, 13 is the led pin
#define LED 13

struct SettingsStorage {
  char version[4];
  uint16_t minRange, maxRange;
  byte curve[CURVE_POINTS];
} settings {
  CONFIG_VERSION,
  1024, 1024,
  {100, 100, 100, 100, 100, 100, 100, 100, 100, 100}
};

int lastReadValue;
int lastWrittenValue;
boolean moduleEnabled = false;

boolean loadConfig() {
  // To make sure there are settings, and they are YOURS!
  // If nothing is found it will use the default settings.
  if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2])
  { // reads settings from EEPROM
    for (unsigned int t = 0; t < sizeof(settings); t++)
      *((byte*)&settings + t) = EEPROM.read(CONFIG_START + t);
    return true;
  } else {
    // settings aren't valid! will overwrite with default settings
    //saveConfig();
    return false;
  }
}

boolean saveConfig() {
  boolean result = true;
  for (unsigned int t = 0; t < sizeof(settings); t++)
  { // writes to EEPROM
    EEPROM.update(CONFIG_START + t, *((byte*)&settings + t));
    // and verifies the data
  }

  // display saved and current config
  for (unsigned int t = 0; t < sizeof(settings); t++) {
    Serial.print(t);
    Serial.print('\t');
    Serial.print(EEPROM.read(CONFIG_START + t), DEC);
    Serial.print('\t');
    Serial.println(*((byte*)&settings + t), DEC);
    if ((EEPROM.read(CONFIG_START + t) != *((byte*)&settings + t))) {
      // error writing to EEPROM
      result = false;
    }
  }

  return result;
}

void outputSettings() {
  Serial.print("v:");
  Serial.println(settings.version);
  Serial.print("m:");
  Serial.println(settings.minRange);
  Serial.print("M:");
  Serial.println(settings.maxRange);
  for (unsigned int t = 0; t < CURVE_POINTS; t++) {
    Serial.print("C[");
    Serial.print(t);
    Serial.print("]=");
    Serial.print(settings.curve[t]);
    Serial.print(" ");
  }
  Serial.println();
}

void readTelemetry() {
  char cmd, cmd2, point;
  if (Serial.available()) {
    char cmd = (char)Serial.read();
    switch (cmd) {
      case 'h':
        Serial.println("e:enable | d:disable | rm:show min | rM:show max | rc:show all");
        Serial.println("sm[XXX]:set min | sM[XXX]:set max | sC[X][XXX]:set curve point");
        Serial.println("sX[...]:set all |S:save to eeprom | L:load from eeprom | h:help");
        break;
      case 'e':
        moduleEnabled = true;
        Serial.println("OK");
        break;
      case 'd':
        moduleEnabled = false;
        Serial.println("OK");
        break;
      case 'r':
        cmd2 = (char)Serial.read();
        switch (cmd2) {
          case 'm':
            Serial.println(settings.minRange);
            break;
          case 'M':
            Serial.println(settings.maxRange);
            break;
          case 'c':
            outputSettings();
            break;
          default:
            Serial.println("??");
        }
        break;
      case 's':
        cmd2 = (char)Serial.read();
        switch (cmd2) {
          case 'm':
            settings.minRange = Serial.parseInt();
            Serial.println("OK");
            break;
          case 'M':
            settings.maxRange = Serial.parseInt();
            Serial.println("OK");
            break;
          case 'C':
            point = (char)Serial.read() - 48; //WARNING! only 0-9 are accepted here
            if (point < CURVE_POINTS) {
              settings.curve[point] = Serial.parseInt();
              Serial.println("OK");
            } else {
              Serial.println("???");
            }
            break;
          case 'X':
            Serial.read();//m
            settings.minRange = Serial.parseInt();
            Serial.read();//M
            settings.maxRange = Serial.parseInt();
            for (char i = 0; i < CURVE_POINTS; i++) {
              settings.curve[i] = Serial.parseInt();
            }
            Serial.println("OK");
            break;
          default:
            Serial.println("??");
        }
        break;
      case 'S':
        if (saveConfig()) {
          Serial.println("OK");
        } else {
          Serial.println("ERR");
        }
        break;
      case 'L':
        if (loadConfig()) {
          Serial.println("OK");
        } else {
          Serial.println("ERR");
        }
        break;
      default:
        Serial.println('?');
    }
  }
  // empty buffer
  while (Serial.available()) {
    Serial.read();
  }
}

int applyOffset(int inputValue) {
  int result = inputValue;
  if ((inputValue > settings.minRange) && (inputValue < settings.maxRange)) {
    int binSize = (settings.maxRange - settings.minRange) / CURVE_POINTS;
    if (binSize > 0) {
      int point = (int)((inputValue - settings.minRange - binSize / 2) / binSize);
      if (point < CURVE_POINTS) {
        result = (int)((float)settings.curve[point] / 100 * inputValue);
        if (result == 0) { // sanity check
          result = inputValue;
        }
      }
    }
  }
  return result;
}

void applyOffset() {
  if (moduleEnabled) {
    digitalWrite(LED, 1);
  }
  lastReadValue = analogRead(ADC_IN);
  if (moduleEnabled) {
    lastWrittenValue = applyOffset(lastReadValue);
  } else {
    lastWrittenValue = lastReadValue;
  }
  analogWrite(DAC_OUT, lastWrittenValue >> 2);
  digitalWrite(LED, 0);
}

void outputCurrentValues() {
  Serial.print("i:");
  Serial.print(lastReadValue);
  Serial.print(" o:");
  Serial.println(lastWrittenValue);
}

void highPrioFunction() {
  applyOffset();
}

void lowPrioFunction() {
  digitalWrite(LED, 1);
  outputCurrentValues();
  readTelemetry();
  digitalWrite(LED, 0);
}

int delayScale;
void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100);

  //  pinMode(LED, OUTPUT);
  //pinMode(ADC_IN, INPUT_PULLUP); // use this if you need a weak pullup

  Serial.println("Load settings:" + loadConfig() ? "OK" : "FAIL");
  outputSettings();

  // see http://playground.arduino.cc/Main/TimerPWMCheatsheet
  TCCR1B = (TCCR1B & 0b11111000) | 0x01; // pins 9 & 10; default is 0x03, 0x01 sets pwm frequency to ~31kHz
  TCCR0B = (TCCR0B & 0b11111000) | 0x01; // pins 5 & 6; default is 0x03, 0x01 sets pwm frequency to ~62kHz
  // if you mess with the above, set the delay scale appropriately
  delayScale = 64;
}

int smallLoops;
void loop() {
  highPrioFunction();
  if (smallLoops >= 100) {
    lowPrioFunction();
    smallLoops = 0;
  }
  smallLoops++;
  delay(10 * delayScale);
}


#include "Arduino.h"

// Includes
#include <Structs.h>
#include <config.h>

// Public libs
#include <LiquidCrystal.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>

#include <EEPROM.h>
#include <ESPmDNS.h>
//#include <time.h>

// Private libs
#include "SettingsManager.h"
#include "WebHandler.h"
#include "LCDController.h"
#include "RaceHandler.h"
#include "LightsController.h"
//#include "SystemManager.h"
//#include "SlaveHandler.h"
//#include "WifiManager.h"

// Set simulate to true to enable simulator class (see Simulator.cpp/h)
#if Simulate
#include "Simulator.h"
#endif

// Function prototypes
void IRAM_ATTR Sensor1Wrapper();
void IRAM_ATTR Sensor2Wrapper();
void ResetRace();
void StartStopRace();
void StartRaceMain();
void StopRaceMain();
void mdnsServerSetup();
void serialEvent();
void HandleSerialCommands();
void Core1Race(void *parameter);
void Core1Lights(void *parameter);
void Core1LCD(void *parameter);
String GetButtonString(uint8_t _iActiveBit);
void WiFiEvent(arduino_event_id_t event);

// Photoelectric sensors
const uint8_t iS1Pin = 25; // S1 (handler side) photoelectric sensors
const uint8_t iS2Pin = 26; // S2 (box side) photoelectric sensors

// 40x4 LCD
const uint8_t iLCDE1Pin = 2;    // E1 pin of virtual LCD1 (raw 1 & 2 of 40x4 LCD)
const uint8_t iLCDE2Pin = 5;    // E1 pin of virtual LCD2 (raw 3 & 4 of 40x4 LCD)
const uint8_t iLCDData4Pin = 13; // Data4
const uint8_t iLCDData5Pin = 10; // Data5
const uint8_t iLCDData6Pin = 9; // Data6
const uint8_t iLCDData7Pin = 27; // Data7
const uint8_t iLCDRSPin = 17;    // RS pin

// control pins for 74HC595 (lights)
const uint8_t iLatchPin = 23;
const uint8_t iClockPin = 18;
const uint8_t iDataPin = 19; 

// Other
const uint8_t iBatterySensorPin = 36; // Battery sensor (voltage divider)

// Not in use
// 1: free/TX
// 3: free/RX

// Global variables
bool bCheckWsClinetStatus = false; // flag to check if WS client should be disconnected
IPAddress ipTocheck;               // IP address of disconnected WiFi user

unsigned int uiLastProgress = 0; // last % OTA progress value

// variables for handling 74HC166
unsigned long long llLastDebounceTime = 0;
unsigned long long llPressedTime[8] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long long llReleasedTime[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t iLastActiveBit = 0;
byte byDataIn = 0;
byte byLastStadyState = 0;
byte byLastFlickerableState = 0;
const uint16_t DEBOUNCE_DELAY = 30;    // in ms
const uint16_t SHORT_PRESS_TIME = 700; // in ms

// String for serial comms storage
String strSerialData;
byte bySerialIndex = 0;
bool bSerialStringComplete = false;

TaskHandle_t taskRace;
TaskHandle_t taskLights;
TaskHandle_t taskLCD;
// file:	main.cpp summary: FlyballETS-Software by simonttp78 forked from Alex Goris
//
// Flyball ETS (Electronic Training System) is an open source project which is designed to help
// teams who practice flyball (a dog sport). Read about original project, including extensive
// information on first prototype version of Flyball ETS, on the following link: https://
// sparkydevices.wordpress.com/tag/flyball-ets/
//
// This part of the project (FlyballETS-Software) contains the ESP32 source code for the ESP32
// LoLin32 which controls all components in the Flyball ETS These sources are originally
// distributed from: https://github.com/vyruz1986/FlyballETS-Software.
//
// Copyright (C) 2019 Alex Goris
// This file is part of FlyballETS-Software
// FlyballETS-Software is free software : you can redistribute it and / or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program.If not,
// see <http://www.gnu.org/licenses/>
#include "main.h"

// Declare 40x4 LCD by 2 virtual LCDes
LiquidCrystal lcd(iLCDRSPin, iLCDE1Pin, iLCDData4Pin, iLCDData5Pin, iLCDData6Pin, iLCDData7Pin);  // this will be line 1&2 of 40x4 LCD
LiquidCrystal lcd2(iLCDRSPin, iLCDE2Pin, iLCDData4Pin, iLCDData5Pin, iLCDData6Pin, iLCDData7Pin); // this will be line 3&4 of 40x4 LCD

// IP addresses declaration
IPAddress IPGateway(192, 168, 20, 1);
IPAddress IPNetwork(192, 168, 20, 0);
IPAddress IPSubnet(255, 255, 255, 0);

// Statically allocate and initialize the spinlock
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

void setup()
{
   EEPROM.begin(EEPROM_SIZE);
   Serial.begin(115200);
   SettingsManager.init();

   // Configure sensors pins
   pinMode(iS1Pin, INPUT_PULLDOWN); // ESP32 has no pull-down resistor on pin 34, but it's pulled-down anyway by 1kohm resistor in voltage leveler circuit
   pinMode(iS2Pin, INPUT_PULLDOWN);

   // Initialize lights
   pinMode(iDataPin, OUTPUT);

   // Configure pins for 74HC166
   pinMode(iLatchPin, OUTPUT);
   pinMode(iClockPin, OUTPUT);

   // Configure LCD pins
   pinMode(iLCDData4Pin, OUTPUT);
   pinMode(iLCDData5Pin, OUTPUT);
   pinMode(iLCDData6Pin, OUTPUT);
   pinMode(iLCDData7Pin, OUTPUT);
   pinMode(iLCDE1Pin, OUTPUT);
   pinMode(iLCDE2Pin, OUTPUT);
   pinMode(iLCDRSPin, OUTPUT);

   // Set ISR's with wrapper functions
#if !Simulate
   attachInterrupt(digitalPinToInterrupt(iS1Pin), Sensor1Wrapper, CHANGE);
   attachInterrupt(digitalPinToInterrupt(iS2Pin), Sensor2Wrapper, CHANGE);
#endif

   // Print SW version
   Serial.printf("Firmware version: %s\r\n", FW_VER);

   // Initialize LightsController class
   xTaskCreatePinnedToCore(
      Core1Lights,
      "Lights",
      8192,
      NULL,
      1,
      &taskLights,
      1);

   // Initialize LCDController class with lcd1 and lcd2 objects
   LCDController.init(&lcd, &lcd2);
   /*xTaskCreatePinnedToCore(
      Core1LCD,
      "LCD",
      8192,
      NULL,
      1,
      &taskLCD,
      1);*/

   strSerialData[0] = 0;

   // Initialize RaceHandler class with S1 and S2 pins
   xTaskCreatePinnedToCore(
      Core1Race,
      "Race",
      16384,
      NULL,
      1,
      &taskRace,
      1);

   // Setup AP
   WiFi.onEvent(WiFiEvent);
   WiFi.mode(WIFI_AP);
   String strAPName = SettingsManager.getSetting("APName");
   String strAPPass = SettingsManager.getSetting("APPass");
   if (!WiFi.softAP(strAPName.c_str(), strAPPass.c_str()))
      log_e("Error initializing softAP!");
   else
      log_i("Wifi started successfully, AP name: %s, pass: %s", strAPName.c_str(), strAPPass.c_str());
   WiFi.softAPConfig(IPGateway, IPGateway, IPSubnet);

   // configure webserver
   WebHandler.init(80);

   // OTA setup
   ArduinoOTA.setPassword(strAPPass.c_str());
   ArduinoOTA.setPort(3232);
   ArduinoOTA.onStart([](){
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
         type = "Firmware";
      else // U_SPIFFS
         type = "Filesystem";
      Serial.println("\n" + type + " update initiated.");
      LCDController.FirmwareUpdateInit(); });
   ArduinoOTA.onEnd([](){ 
      Serial.println("\nUpdate completed.\r\n");
      LCDController.FirmwareUpdateSuccess(); });
   ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
      uint16_t iProgressPercentage = (progress / (total / 100));
      if (uiLastProgress != iProgressPercentage)
      {
         Serial.printf("Progress: %u%%\r", iProgressPercentage);
         String sProgressPercentage = String(iProgressPercentage);
         while (sProgressPercentage.length() < 3)
            sProgressPercentage = " " + sProgressPercentage;
         LCDController.FirmwareUpdateProgress(sProgressPercentage);
         uiLastProgress = iProgressPercentage;
      } });
   ArduinoOTA.onError([](ota_error_t error){
      Serial.printf("Error[%u]: ", error);
      LCDController.FirmwareUpdateError();
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
   ArduinoOTA.begin();
   mdnsServerSetup();
   // log_i("Setup running on core %d", xPortGetCoreID());

   log_w("ESP log level %i", CORE_DEBUG_LEVEL);
}

void loop()
{
   // Exclude handling of those services in loop while race is running
   if (RaceHandler.RaceState == RaceHandler.STOPPED || RaceHandler.RaceState == RaceHandler.RESET)
   {
      // Handle settings manager loop
      SettingsManager.loop();

      // Handle OTA update if incoming
      ArduinoOTA.handle();
   }

   // Check for serial events
   serialEvent();
   // Handle serial console commands

   if (bSerialStringComplete)
      HandleSerialCommands();

   // Handle LCD processing
   LCDController.Main();

   // Handle WebSocket server
   WebHandler.loop();
}

void serialEvent()
{
   // Listen on serial port
   while (Serial.available() > 0)
   {
      char cInChar = Serial.read(); // Read a character
                                    // Check if buffer contains complete serial message, terminated by newline (\n)
      if (cInChar == '\n')
      {
         // Serial message in buffer is complete, null terminate it and store it for further handling
         bSerialStringComplete = true;
         log_d("SERIAL received: '%s'", strSerialData.c_str());
         strSerialData += '\0'; // Null terminate the string
         break;
      }
      strSerialData += cInChar; // Store it
   }
}

/// <summary>
///   These are wrapper functions which are necessary because it's not allowed to use a class member function directly as an ISR
/// </summary>
void IRAM_ATTR Sensor1Wrapper()
{
   RaceHandler.TriggerSensor1(&spinlock);
}

/// <summary>
///   These are wrapper functions which are necessary because it's not allowed to use a class member function directly as an ISR
/// </summary>
void IRAM_ATTR Sensor2Wrapper()
{
   RaceHandler.TriggerSensor2(&spinlock);
}

/// <summary>
///   Start a race.
/// </summary>
void StartRaceMain()
{
   if (RaceHandler.RaceState != RaceHandler.RESET)
      return;
   LightsController.InitiateStartSequence();
}

/// <summary>
///   Stop a race.
/// </summary>
void StopRaceMain()
{
   if (RaceHandler.RaceState == RaceHandler.STOPPED || RaceHandler.RaceState == RaceHandler.RESET)
      return;
   RaceHandler.bExecuteStopRace = true;
   LightsController.DeleteSchedules();
}

/// <summary>
///   Starts (if stopped) or stops (if started) a race. Start is only allowed if race is stopped and reset.
/// </summary>
void StartStopRace()
{
   if (RaceHandler.RaceState == RaceHandler.RESET)
      StartRaceMain();
   else // If race state is running or starting, we should stop it
      StopRaceMain();
}

/// <summary>
///   Reset race so new one can be started, reset is only allowed when race is stopped
/// </summary>
void ResetRace()
{
   if (RaceHandler.RaceState != RaceHandler.STOPPED) // Only allow reset when race is stopped first
      return;
   RaceHandler.bExecuteResetRace = true;
   LightsController.bExecuteResetLights = true;
}

void WiFiEvent(arduino_event_id_t event)
{
   // Serial.printf("Wifi event %i\r\n", event);
   switch (event)
   {
   case ARDUINO_EVENT_WIFI_AP_START:
      // log_i("AP Started");
      WiFi.softAPConfig(IPGateway, IPGateway, IPSubnet);
      if (WiFi.softAPIP() != IPGateway)
      {
         log_e("I am not running on the correct IP (%s instead of %s), rebooting!", WiFi.softAPIP().toString().c_str(), IPGateway.toString().c_str());
         ESP.restart();
      }
      log_i("Ready on IP: %s, v%s", WiFi.softAPIP().toString().c_str(), APP_VER);
      break;

   case ARDUINO_EVENT_WIFI_AP_STOP:
      // log_i("AP Stopped");
      break;

   case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      // log_i("IP assigned to new client");
      break;

   case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      // bCheckWsClinetStatus = true;
      // ipTocheck = IPAddress (192,168,20,2);
      // log_i("IP to check: %s", ipTocheck.toString().c_str());
      break;

   default:
      break;
   }
}

void ToggleWifi()
{
   if (WiFi.getMode() == WIFI_MODE_AP)
   {
      WiFi.mode(WIFI_OFF);
      LCDController.UpdateField(LCDController.WifiState, " ");
      LCDController.bExecuteLCDUpdate = true;
      log_i("WiFi OFF");
   }
   else
   {
      WiFi.mode(WIFI_AP);
      LCDController.UpdateField(LCDController.WifiState, "W");
      LCDController.bExecuteLCDUpdate = true;
      log_i("WiFi ON");
   }
}

void mdnsServerSetup()
{
   MDNS.addService("http", "tcp", 80);
   MDNS.addServiceTxt("arduino", "tcp", "app_version", APP_VER);
   MDNS.begin("flyballets");
}

void HandleSerialCommands()
{
   // Race start
   if (strSerialData == "start")
      StartRaceMain();
   // Race stop
   if (strSerialData == "stop")
      StopRaceMain();
   // Race reset button
   if (strSerialData == "reset")
      ResetRace();
   // Print time
   if (strSerialData == "time")
      log_i("System time:  %s", GPSHandler.GetLocalTimestamp());
   // Print uptime
   if (strSerialData == "uptime")
   {
      uint32_t t = (uint32_t)(millis() / 1000);
      uint8_t s = t % 60;
      t = (t - s) / 60;
      uint8_t m = t % 60;
      t = (t - m) / 60;
      uint16_t h = t;
      log_i("Up time: %i:%i:%i", h, m, s);
   }

   // Reboot ESP32
   if (strSerialData == "reboot")
      ESP.restart();
   // Prepare for automatic tests (used by testETS.py script)
   if (strSerialData == "preparefortesting")
      if (!Simulate)
         log_e("FAILED - Firmware's not compiled in Simulation mode");
      else
      {
         if (SettingsManager.getSetting("Accuracy3digits").equals("0"))
            RaceHandler.ToggleAccuracy();
         if (SettingsManager.getSetting("RunDirectionInverted").equals("1"))
            RaceHandler.ToggleRunDirection();
         log_i("DONE - Simulation mode active. Accuracy set to 3. Run direction: normal.");
      }
#if Simulate
   // Change Race ID (only serial command), e.g. race 1 or race 2
   if (strSerialData.startsWith("race"))
   {
      strSerialData.remove(0, 5);
      Simulator.iSimulatedRaceID = strSerialData.toInt();
      if (Simulator.iSimulatedRaceID < 0 || Simulator.iSimulatedRaceID >= NumSimulatedRaces)
      {
         Simulator.iSimulatedRaceID = 0;
      }
      // Simulator.ChangeSimulatedRaceID(iSimulatedRaceID);
      Simulator.bExecuteSimRaceChange = true;
   }
#endif
   // Dog 1 fault
   if (strSerialData == "d1f")
      RaceHandler.SetDogFault(0);
   // Dog 2 fault
   if (strSerialData == "d2f")
      RaceHandler.SetDogFault(1);
   // Dog 3 fault
   if (strSerialData == "d3f")
      RaceHandler.SetDogFault(2);
   // Dog 4 fault
   if (strSerialData == "d4f")
      RaceHandler.SetDogFault(3);
   // Toggle race direction
   if (strSerialData == "direction")
      RaceHandler.ToggleRunDirection();
   // Set explicitly number of racing dogs
   if (strSerialData.startsWith("setdogs") && RaceHandler.RaceState == RaceHandler.RESET)
   {
      strSerialData.remove(0, 8);
      uint8_t iNumberofRacingDogs = strSerialData.toInt();
      if (iNumberofRacingDogs < 1 || iNumberofRacingDogs > 4)
      {
         iNumberofRacingDogs = 4;
      }
      RaceHandler.SetNumberOfDogs(iNumberofRacingDogs);
   }
   // Toggle accuracy
   if (strSerialData == "accuracy")
      RaceHandler.ToggleAccuracy();
   // Toggle decimal separator in CSV
   // Reruns off
   if (strSerialData == "reruns off")
      RaceHandler.ToggleRerunsOffOn(1);
   // Reruns on
   if (strSerialData == "reruns on")
      RaceHandler.ToggleRerunsOffOn(0);
   // Toggle wifi on/off
   if (strSerialData == "wifi")
      ToggleWifi();
   // Toggle wifi on/off
   if (strSerialData == "fwver")
      Serial.printf("Firmware version: %s\r\n", FW_VER);

   // Make sure this stays last in the function!
   if (strSerialData.length() > 0)
   {
      strSerialData = "";
      bSerialStringComplete = false;
   }
}

/// <summary>
///   Gets pressed button string for consol printing.
/// </summary>
String GetButtonString(uint8_t _iActiveBit)
{
   String strButton;
   switch (_iActiveBit)
   {
   case 0:
      strButton = "Mode button";
      break;
   case 1:
      strButton = "Remote 1: start/stop";
      break;
   case 2:
      strButton = "Remote 2: reset";
      break;
   case 3:
      strButton = "Remote 3: dog 1 fault";
      break;
   case 6:
      strButton = "Remote 6: dog 4 fault";
      break;
   case 5:
      strButton = "Remote 5: dog 3 fault";
      break;
   case 4:
      strButton = "Remote 4: dog 2 fault";
      break;
   case 7:
      strButton = "Laser trigger";
      break;
   default:
      strButton = "Unknown --> Ingored";
      break;
   }

   return strButton;
}

void Core1Race(void *parameter)
{
#if Simulate
   Simulator.init();
#endif
   RaceHandler.init(iS1Pin, iS2Pin);
   for (;;)
   {
   #if Simulate
      Simulator.Main();
   #endif
      RaceHandler.Main();
      vTaskDelay(1 / portTICK_PERIOD_MS);
   }
}

void Core1Lights(void *parameter)
{
   LightsController.init(iLatchPin, iClockPin, iDataPin);
   for (;;)
   {
      LightsController.Main();
      vTaskDelay(1 / portTICK_PERIOD_MS);
   }
}

/*void Core1LCD(void *parameter)
{
   LCDController.init(&lcd, &lcd2);
   for (;;)
   {
      LCDController.Main();
      vTaskDelay(1 / portTICK_PERIOD_MS);
   }
}*/
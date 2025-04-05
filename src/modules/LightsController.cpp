#include "LightsController.h"
#include "LCDController.h"
#include "RaceHandler.h"
#include "config.h"
#include "Structs.h"
#include "WebHandler.h"

/// <summary>
///   Initialises this object. This function needs to be passed the pin numbers for the shift
///   register which is used to control the lights.
/// </summary>
///
/// <param name="iLatchPin">  Zero-based index of the latch pin. </param>
/// <param name="iClockPin">  Zero-based index of the clock pin. </param>
/// <param name="iDataPin">   Zero-based index of the data pin. </param>

void LightsControllerClass::init(uint8_t iLatchPin, uint8_t iClockPin, uint8_t iDataPin)
{

      //Initialize pins for shift register
   _iLatchPin = iLatchPin;
   _iClockPin = iClockPin;
   _iDataPin = iDataPin;
   pinMode(_iLatchPin, OUTPUT);
   pinMode(_iClockPin, OUTPUT);
   pinMode(_iDataPin, OUTPUT);

   //Write 0 to shift register to turn off all lights
   digitalWrite(_iLatchPin, LOW);
   shiftOut(_iDataPin, _iClockPin, MSBFIRST, 0);
   digitalWrite(_iLatchPin, HIGH);
}

/// <summary>
///   Main entry-point for this application. It contains the main processing required for the
///   lights and should be called every time in the main loop of the project.
/// </summary>
void LightsControllerClass::Main()
{
   if (bS1ExecuteRaceReadyFaultON)
   {
      if (!_bS2RaceReadyFaultActive)
         ReaceReadyFault(LightsController.ON);
      bS1ExecuteRaceReadyFaultON = false;
      _bS1RaceReadyFaultActive = true;
   }

   if (bS2ExecuteRaceReadyFaultON)
   {
      if (!_bS1RaceReadyFaultActive)
         ReaceReadyFault(LightsController.ON);
      bS2ExecuteRaceReadyFaultON = false;
      _bS2RaceReadyFaultActive = true;
   }

   if (bS1ExecuteRaceReadyFaultOFF)
   {
      if (!_bS2RaceReadyFaultActive)
         ReaceReadyFault(LightsController.OFF);
      bS1ExecuteRaceReadyFaultOFF = false;
      _bS1RaceReadyFaultActive = false;
   }

   if (bS2ExecuteRaceReadyFaultOFF)
   {
      if (!_bS1RaceReadyFaultActive)
         ReaceReadyFault(LightsController.OFF);
      bS2ExecuteRaceReadyFaultOFF = false;
      _bS2RaceReadyFaultActive = false;
   }

   if (bExecuteResetLights)
   {
      ResetLights();
      bExecuteResetLights = false;
   }

   // Check if we have to toggle any lights
   for (int i = 0; i < 6; i++)
   {
      if (millis() > _lLightsOnSchedule[i] && _lLightsOnSchedule[i] != 0)
      {
         //Serial.printf("ToggleLightState %i On\r\n", i);
         ToggleLightState(_byLightsArray[i], ON);
         _lLightsOnSchedule[i] = 0; // Delete schedule
      }
      if (millis() > _lLightsOutSchedule[i] && _lLightsOutSchedule[i] != 0)
      {
         //Serial.printf("ToggleLightState %i Off\r\n", i);
         ToggleLightState(_byLightsArray[i], OFF);
         _lLightsOutSchedule[i] = 0; // Delete schedule

         if (i < 2)
            {
               WebHandler.bUpdateLights = true;
               //log_d("UpdateLights i<2");
            }
      }
   }

   if (_byCurrentLightsState != _byNewLightsState)
   {
      //Serial.printf("New light states: %i\r\n", _byNewLightsState);
      _byCurrentLightsState = _byNewLightsState;
      digitalWrite(_iLatchPin, LOW);
      shiftOut(_iDataPin, _iClockPin, MSBFIRST, _byCurrentLightsState);
      digitalWrite(_iLatchPin, HIGH);
      WebHandler.bUpdateLights = true;
   }
}

/// <summary>
///   Initiate start sequence, should be called if starting lights sequence should be initiated.
/// </summary>
void LightsControllerClass::InitiateStartSequence()
{
   // Set schedule for RED light
   _lLightsOnSchedule[1] = millis();         // Turn on NOW
   _lLightsOutSchedule[1] = millis() + 1000; // keep on for 1 second

   // Set schedule for YELLOW1 light
   _lLightsOnSchedule[2] = millis() + 1000;  // Turn on after 1 second
   _lLightsOutSchedule[2] = millis() + 2000; // Turn off after 2 seconds

   // Set schedule for YELLOW2 light
   _lLightsOnSchedule[4] = millis() + 2000;  // Turn on after 2 seconds
   _lLightsOutSchedule[4] = millis() + 3000; // Turn off after 3 seconds

   // Set schedule for GREEN light
   _lLightsOnSchedule[5] = millis() + 3000;  // Turn on after 3 seconds
   _lLightsOutSchedule[5] = millis() + 4000; // Turn off after 4 seconds
   byOverallState = INITIATED;
}

/// <summary>
///   Resets the lights (turn everything OFF).
/// </summary>
void LightsControllerClass::ResetLights()
{
   byOverallState = RESET;
   _byNewLightsState = 0;
   digitalWrite(_iLatchPin, LOW);
   shiftOut(_iDataPin, _iClockPin, MSBFIRST, _byNewLightsState);
   digitalWrite(_iLatchPin, HIGH);
   DeleteSchedules();
}

/// <summary>
///   Deletes any scheduled light timings.
/// </summary>
void LightsControllerClass::DeleteSchedules()
{
   //Delete any set schedules
   for (int i = 0; i < 6; i++)
   {
      _lLightsOnSchedule[i] = 0; //Delete schedule
      _lLightsOutSchedule[i] = 0; //Delete schedule
   }
}

/// <summary>
///   Toggle a given light to a given state.
/// </summary>
///
/// <param name="byLight">       The by light. </param>
/// <param name="byLightState">  State of the by light. </param>
void LightsControllerClass::ToggleLightState(Lights byLight, LightStates byLightState)
{
   bool byCurrentLightState = CheckLightState(byLight);

   if (byLightState == OFF)
   {
      if (byOverallState == WARNING && byLight == WHITE)
         InitiateStartSequence();
   }
   else
   {
      if (byOverallState == INITIATED && byLight == RED )
      {
         //Serial.printf("Light State INITIATED %i\r\n", byLightState);
         RaceHandler.bExecuteStartRaceTimer = true;
         byOverallState = STARTING;
      }
      else if (byOverallState == STARTING && byLight == GREEN )
      {
         //Serial.printf("Light State STARTING %i\r\n", byLightState);
         byOverallState = STARTED;
      }
   }
   
   if (byCurrentLightState != byLightState)
   {

      if (byLightState == ON)
         _byNewLightsState = _byNewLightsState + byLight;
      else
         _byNewLightsState = _byNewLightsState - byLight;
   }
}

/// <summary>
///   Toggle fault light for a given dog number. This function will take a dog number and a light
///   state, and determine by itself which light should be set to the given state.
/// </summary>
///
/// <param name="DogNumber">     Zero-indexed dog number. </param>
/// <param name="byLightState">  State of the by light. </param>
void LightsControllerClass::ToggleFaultLight(uint8_t DogNumber, LightStates byLightState)
{
   //Get error light for dog number from array
   Lights byLight = _byDogErrorLigths[DogNumber];
   if (byLightState == ON)
   {
      //If a fault lamp is turned on we have to light the white light for 1 sec
      //Set schedule for WHITE light
      _lLightsOnSchedule[0] = millis(); //Turn on NOW
      _lLightsOutSchedule[0] = millis() + 1000; //keep on for 1 second
   }
   ToggleLightState(byLight, byLightState);
}

void LightsControllerClass::ReaceReadyFault(LightStates byLightState)
{
   Lights byLight = _byLightsArray[0];
   ToggleLightState(byLight, byLightState);
}

stLightsState LightsControllerClass::GetLightsState()
{
   stLightsState CurrentLightsState;

   CurrentLightsState.State[0] = CheckLightState(WHITE) == 1 ? 1 : 0;
   CurrentLightsState.State[1] = CheckLightState(RED) == 1 ? 1 : 0;
   CurrentLightsState.State[2] = CheckLightState(YELLOW1) == 1 ? 1 : 0;
   CurrentLightsState.State[3] = CheckLightState(BLUE) == 1 ? 1 : 0;
   CurrentLightsState.State[4] = CheckLightState(YELLOW2) == 1 ? 1 : 0;
   CurrentLightsState.State[5] = CheckLightState(GREEN) == 1 ? 1 : 0;

   return CurrentLightsState;
}

/// <summary>
///   Check light state for a given light.
/// </summary>
///
/// <param name="byLight"> The light for which the state should be returned. </param>
///
/// <returns>
///   The LightsControllerClass::LightStates state for the given light number.
/// </returns>
LightsControllerClass::LightStates LightsControllerClass::CheckLightState(Lights byLight)
{
   if ((byLight & _byNewLightsState) == byLight)
      return ON;
   else
      return OFF;
}

/// <summary>
///   The lights controller.
/// </summary>
LightsControllerClass LightsController;

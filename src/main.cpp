#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <WiFiManager.h>
#define WEBSERVER_H // Fix for webserver wifi portal https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_MAX31855.h>
#include <PID_v2.h>
#include <RBDdimmer.h>
#include <math.h>

// Custom libraries
#include "PinDefinitions.h"
#include "Version.h"
#include "Constants.h"
#include <PressureProfileService.h>
#include <Configuration.h>
#include <Display.h>
#include <Stats.h>
#include <Api.h>

const char *ssid = "";
const char *password = "";

// Globals
long long machineOnTime = 0;
bool screenSet = false;
bool pumpSwitchOn;
int currentCycleTime = 0;
int screenState = 0;
//  refactor this to be calculated and stored when new profile uploaded
int shotLength = 0;
int systemState = 0;
bool steamModeOn = false;
bool boilerEnabled = true;
bool fullPowerSet = false;
bool temperatureLimitChanged = false;
int runningCycle = 0;
// refactor this to be set when new profile uploaded
bool runPumpCalibration = false;

// Third party
Adafruit_MAX31855 boilerThermo(TEMP_SENSOR_ONE_CS_PIN);
Adafruit_MAX31855 steamThermo(TEMP_SENSOR_TWO_CS_PIN);
dimmerLamp pump(PUMP_OUTPUT_PIN, ZERO_CROSS_PIN);
dimmerLamp boiler(BOILER_OUTPUT_PIN, ZERO_CROSS_PIN);
PID_v2 *heaterPID;

// Custom
PressureProfileService *pressureProfileService;
Configuration *configuration;
Display *display;
Stats *machineStats;
Api *api;

void IRAM_ATTR isr()
{
  temperatureLimitChanged = true;
  int pinState = digitalRead(STEAM_SWITCH_PIN);
  if (pinState == LOW)
  {
    steamModeOn = true;
  }
  else
  {
    steamModeOn = false;
  }
}

void IRAM_ATTR pump_isr()
{
  int pinState = digitalRead(PUMP_SWITCH_PIN);
  if (pinState == LOW)
  {
    pumpSwitchOn = true;
  }
  else
  {
    pumpSwitchOn = false;
    currentCycleTime = 0;
    boilerEnabled = true;
  }
  screenSet = false;
}

void updateBoilerPower(double boilerTemp)
{
  // over heat protection
  if (boilerTemp >= 155)
  {
    boiler.setPower(0);
    return;
  }

  double output = heaterPID->Run(boilerTemp);
  boiler.setPower(output);
}

void updateSteamPower(double steamTemp)
{
  // over heat protection
  if (steamTemp >= 155)
  {
    boiler.setPower(0);
    return;
  }

  if (steamTemp < 130)
  {
    boiler.setPower(0);
    digitalWrite(DIMMER_BYPASS_PIN, HIGH);
    fullPowerSet = true;
  }
  else if (steamTemp >= 130)
  {
    digitalWrite(DIMMER_BYPASS_PIN, LOW);
    double output = heaterPID->Run(steamTemp);
    boiler.setPower(output);
    fullPowerSet = false;
  }
}

int getBoilerTemp()
{
  double boilerTempOut = boilerThermo.readCelsius();
  double internalTempOut = boilerThermo.readInternal();
  int error = boilerThermo.readError();

  // todo: need error code mappings
  switch (error)
  {
  case 1:
    return -100;
  case 2:
    return -200;
  case 4:
    return -400;
  default:
    break;
  }

  if (isnan(boilerTempOut) || boilerTempOut > 9000)
  {
    return -500;
  }

  if (!steamModeOn)
  {
    updateBoilerPower(boilerTempOut);
  }

  return boilerTempOut;
}

int getSteamTemp()
{
  double steamTempOut = steamThermo.readCelsius();
  double boilerTempOut = boilerThermo.readCelsius();
  double internalTempOut = steamThermo.readInternal();
  int error = steamThermo.readError();

  // todo: need error code mappings
  switch (error)
  {
  case 1:
    return -100;
  case 2:
    return -200;
  case 4:
    return -400;
  default:
    break;
  }

  if (isnan(steamTempOut) || steamTempOut > 9000)
  {
    return -500;
  }

  if (steamModeOn)
  {
    // using boiler (bottom) temperature sensor for this, it appears to be more accurate and is closer to the heating element
    updateSteamPower(boilerTempOut);
  }

  return steamTempOut;
}

int getPumpPressure()
{
  float rawAdc = 0.00;
  for (int i = 0; i < 10; i++)
  {
    rawAdc += analogRead(PRESSURE_SENSOR_PIN);
  }
  float avgAdc = rawAdc / 10;
  // sensor starts at 0.5 volts for 0 bar, shift
  int adcShift = 4 * ADC_TO_BAR;
  int correctedAdc = avgAdc - adcShift;
  int pressureBar = correctedAdc / ADC_TO_BAR;

  if (pressureBar < 0)
  {
    return 0;
  }
  return pressureBar;
}

void setup()
{
  Serial.begin(115200);
  SPIFFS.begin();

  pinMode(STEAM_SWITCH_PIN, INPUT_PULLUP);
  pinMode(PUMP_SWITCH_PIN, INPUT_PULLUP);
  pinMode(DIMMER_BYPASS_PIN, OUTPUT);
  attachInterrupt(STEAM_SWITCH_PIN, isr, CHANGE);
  attachInterrupt(PUMP_SWITCH_PIN, pump_isr, CHANGE);

  // Connect to Wi-Fi
  WiFi.setHostname("CoffeeBuddy");
  WiFi.begin(ssid, password);
  // add screen here for connection attempts
  for (int i = 0; i < 5; i++)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      break;
    }
    delay(1000);
  }

  configuration = new Configuration();
  Serial.println("Attempting to start pressure profile service");
  pressureProfileService = new PressureProfileService(configuration);
  machineStats = new Stats();
  api = new Api(80, configuration, pressureProfileService);
  display = new Display(LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_BL_PIN);
  heaterPID = new PID_v2(configuration->getConfiguration().boilerP, configuration->getConfiguration().boilerI, configuration->getConfiguration().boilerD, PID::Direct, PID::P_On::Measurement);
  heaterPID->SetOutputLimits(0, configuration->getConfiguration().boilerEspressoTemperature);
  heaterPID->SetMode(PID::Automatic);

  int tempBoilerTemp = 24;
  int boilerThermoCouple = getBoilerTemp();

  if (getBoilerTemp() > 0)
  {
    heaterPID->Start(getBoilerTemp(), 0, configuration->getConfiguration().boilerEspressoTemperature);
  }
  else
  {
    heaterPID->Start(tempBoilerTemp, 0, configuration->getConfiguration().boilerEspressoTemperature);
  }

  boiler.begin(NORMAL_MODE, ON);
  digitalWrite(DIMMER_BYPASS_PIN, LOW);
  pump.begin(NORMAL_MODE, ON);
  pump.setPower(0);

  systemState = 1;
}

int getCycleTime()
{
  return currentCycleTime++;
}

void sendDataPayload(int espressoTemp, int steamTemp, int pressure, int cycleTime)
{
  StaticJsonDocument<384> doc;

  char networkSsid[32] = "Embernet";
  doc["boilerBottom"] = espressoTemp;
  doc["boilerTop"] = steamTemp;
  doc["pressure"] = pressure;
  doc["machineOnTime"] = machineOnTime;
  doc["cycles"] = machineStats->getCycleCount();
  doc["runningCycle"] = runningCycle;
  doc["currentCycleTime"] = cycleTime;
  doc["requiredTemperature"] = steamModeOn ? configuration->getConfiguration().boilerSteamTemperature : configuration->getConfiguration().boilerEspressoTemperature;
  doc["boilerPower"] = boiler.getPower();

  machineOnTime++;
  char output[384];
  serializeJson(doc, output);
  api->sendDataPayload(output);
}

void loop()
{
  int cycleTime = 0;
  int boilerTemp = 0;
  int steamTemp = 0;
  int pressure = 0;
  int pumpPressure = getPumpPressure();
  int shotLength = pressureProfileService->getLoadedPressureProfile().shotLength;
  int *pressureProfile = pressureProfileService->getLoadedPressureProfile().pressure;

  // check temperature and compute PID every 100ms for better read.
  for (int i = 0; i < 9; i++)
  {
    boilerTemp = getBoilerTemp();
    steamTemp = getSteamTemp();
    delay(100);
  }

  boilerTemp = getBoilerTemp();
  steamTemp = getSteamTemp();

  if (!runPumpCalibration && pumpSwitchOn && currentCycleTime < shotLength)
  {
    runningCycle = 1;
    screenSet = false;
    int cycleTime = getCycleTime();
    pressure = pressureProfile[cycleTime];
    int adjPumpPower = configuration->getConfiguration().pumpCalibration[pressure];
    pump.setPower(adjPumpPower);
    display->drawEspressoProgress(cycleTime, boilerTemp, pumpPressure, pressureProfile, shotLength);
  }
  // this may hit twice
  if (pumpSwitchOn && currentCycleTime >= shotLength)
  {
    pump.setPower(0);
    if (!screenSet)
    {
      int cycleCount = machineStats->getCycleCount();
      machineStats->updateCycleCount();
      display->drawCycleFinishedPage(cycleCount);
      screenSet = true;
    }
  }

  if (!pumpSwitchOn && !steamModeOn)
  {
    pump.setPower(0);
    if (screenState < 5)
    {
      display->drawEspressoPage(boilerTemp);
      screenState += 1;
    }
    else if (screenState >= 5)
    {
      display->drawEspressoPreview(boilerTemp, pressureProfile, shotLength);
      screenState += 1;
    }

    if (screenState > 10)
    {
      screenState = 0;
    }
    runningCycle = 0;
  }

  if (steamModeOn && !pumpSwitchOn)
  {
    display->drawSteamPage(boilerTemp);
  }

  if (temperatureLimitChanged)
  {
    if (steamModeOn)
    {
      heaterPID->SetOutputLimits(0, configuration->getConfiguration().boilerSteamTemperature);
      heaterPID->Setpoint(configuration->getConfiguration().boilerSteamTemperature);
    }
    else
    {
      heaterPID->SetOutputLimits(0, configuration->getConfiguration().boilerEspressoTemperature);
      heaterPID->Setpoint(configuration->getConfiguration().boilerEspressoTemperature);
    }
    temperatureLimitChanged = false;
  }

  delay(100);
  sendDataPayload(boilerTemp, steamTemp, pumpPressure, cycleTime);
}
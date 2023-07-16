#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <WiFiManager.h>
#define WEBSERVER_H // Fix for webserver wifi portal https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_I2CDevice.h>
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

const char *ssid = "";
const char *password = "";

// Globals
int cycleCount = 0;
int pumpCalibrationMem[15];
long long machineOnTime = 0;
bool screenSet = false;
int espressoTemperature = 85;
int steamTemperature = 140;
bool pumpSwitchOn;
int currentCycleTime = 0;
int screenState = 0;
PressureProfile pressureProfile;
int shotLength = 0;
int systemState = 0;
bool steamModeOn = false;
bool boilerEnabled = true;
bool fullPowerSet = false;
bool temperatureLimitChanged = false;
int runningCycle = 0;
int selectedProfile = 0;
bool runPumpCalibration = false;
float pressureSensorReading = 0.0;
int calibrationPressurePoint = 0;
int calibrationPumpPower = 0;

// Diagnostics
int boilerPid = 0;
int dimmerPower = 0;

// 3rd Party
Adafruit_MAX31855 boilerThermo(TEMP_SENSOR_ONE_CS_PIN);
Adafruit_MAX31855 steamThermo(TEMP_SENSOR_TWO_CS_PIN);
dimmerLamp pump(PUMP_OUTPUT_PIN, ZERO_CROSS_PIN);
dimmerLamp boiler(BOILER_OUTPUT_PIN, ZERO_CROSS_PIN);
PID_v2 *heaterPID;

AsyncWebServer server(80);
AsyncEventSource events("/data");

// Custom
PressureProfileService *pressureProfileService;
Configuration *configuration;
Display *display;
Stats *machineStats;

void calculateShotLength()
{
  for (int i = 0; i < 60; i++)
  {
    Serial.print(i);
    int pressurePoint = pressureProfile.pressure[i];
    Serial.print(" pressure ");
    Serial.println(pressurePoint);
    if (pressurePoint < 0)
    {
      shotLength = i;
      break;
    }
  }
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
  dimmerPower = boiler.getPower();
  boilerPid = output;
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
    boilerPid = output;
    boiler.setPower(output);
    dimmerPower = boiler.getPower();
    fullPowerSet = false;
  }
}

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
  pressureSensorReading = correctedAdc;
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
    Serial.println("Connecting to WiFi..");
  }

  Serial.print("Connected to ");
  Serial.println(WiFi.localIP());

  configuration = new Configuration();

  selectedProfile = configuration->getConfiguration().selectedProfile;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/dashboard/home.html"); });

  server.on("/profiles", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/dashboard/profile.html"); });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/dashboard/setting.html"); });

  server.on("/profile/download", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  PressureProfile pressureProfile = pressureProfileService->getPressureProfile(selectedProfile);

  StaticJsonDocument<1050> doc;
  doc["profileId"] = pressureProfile.profileId;
  JsonArray pressure = doc.createNestedArray("pressure");

  int profileLength =sizeof(pressureProfile.pressure)/sizeof(int);
  for (int i = 0; i < profileLength; i++) {
    pressure.add(pressureProfile.pressure[i]);
  }
  serializeJson(doc, *response);
  request->send(response); });

  AsyncCallbackJsonWebHandler *configurationUploadHandler = new AsyncCallbackJsonWebHandler("/uploadConfiguration", [](AsyncWebServerRequest *request, JsonVariant &json)
                                                                                            {
    
    Serial.println("Handling configuration upload");
    JsonObject jsonObj = json.as<JsonObject>();
    if (jsonObj.isNull()) {
      Serial.println("Something went wrong with upload");
      request->send(400);
      return;
    }
    int boilerTimeout = jsonObj["boilerTimeout"];
    int boilerSteamTemperature = jsonObj["boilerSteamTemperature"];
    int boilerEspressoTemperature = jsonObj["boilerEspressoTemperature"];
    int autoFlushTime = jsonObj["autoFlushTime"];
    int fastHeatCutOff = jsonObj["fastHeatCutOff"];
    double boilerP = jsonObj["boilerP"];
    double boilerI = jsonObj["boilerI"];
    double boilerD = jsonObj["boilerD"];
    selectedProfile = jsonObj["selectedProfile"];
    steamTemperature = boilerSteamTemperature;
    espressoTemperature = boilerEspressoTemperature;

    Serial.print("Selected profile is now: ");
    Serial.println(selectedProfile);

    JsonArray pumpCalibration = jsonObj["pumpCalibration"];
    const int pumpCalibrationLength = pumpCalibration.size();

    int pumpCalibrationData[15] = {};
    for (int i = 0; i < pumpCalibrationLength; i++) {
      pumpCalibrationData[i] = pumpCalibration[i];
    }

    int oldBoilerP = heaterPID->GetKp();
    int oldBoilerI = heaterPID->GetKi();
    int oldBoilerD = heaterPID->GetKd();

    if (oldBoilerP != boilerP || oldBoilerI != boilerI || oldBoilerD != boilerD) {
      heaterPID->SetTunings(boilerP, boilerI, boilerD);
      Serial.println("Setting PID to new settings");
    }

    bool machineConfigurationUpdated = configuration->updateConfiguration(boilerTimeout, boilerSteamTemperature, boilerEspressoTemperature, autoFlushTime, fastHeatCutOff, pumpCalibrationData, boilerP, boilerI, boilerD, selectedProfile);

    for (int i = 0; i < pumpCalibrationLength; i++) {
      int calibrationPoint = pumpCalibration[i];
      pumpCalibrationMem[i] = calibrationPoint;
    }

    if (machineConfigurationUpdated) {
      pressureProfile = pressureProfileService->getPressureProfile(selectedProfile);
      calculateShotLength();
    }


    request->send(200); });

  server.on("/configuration/download", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    StaticJsonDocument<384> doc;

    MachineConfiguration machineConfiguration = configuration->getConfiguration();

    doc["boilerTimeout"] = machineConfiguration.boilerTimeOut;
    doc["boilerSteamTemperature"] = machineConfiguration.boilerSteamTemperature;
    doc["boilerEspressoTemperature"] = machineConfiguration.boilerEspressoTemperature;
    doc["autoFlushTime"] = machineConfiguration.autoFlushTime;
    doc["fastHeatCutOff"] = machineConfiguration.fastHeatCutOff;
    doc["boilerP"] = machineConfiguration.boilerP;
    doc["boilerI"] = machineConfiguration.boilerI;
    doc["boilerD"] = machineConfiguration.boilerD;
    doc["selectedProfile"] = machineConfiguration.selectedProfile;

    JsonArray pumpCalibrationData = doc.createNestedArray("pumpCalibration");

    int pumpCalibrationLength =sizeof(machineConfiguration.pumpCalibration)/sizeof(int);
    for (int i = 0; i < pumpCalibrationLength; i++) {
      if (machineConfiguration.pumpCalibration[i] >= 0) {
        pumpCalibrationData.add(machineConfiguration.pumpCalibration[i]);
      }
    }
    serializeJson(doc, *response);
    request->send(response); });

  server.on("/allProfiles", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  AsyncResponseStream *response = request->beginResponseStream("application/json");

  StaticJsonDocument<6144> doc;

  JsonArray profilesObject = doc.createNestedArray("profiles");
  for (int i = 1; i < 7; i++) {
    PressureProfile pressureProfile = pressureProfileService->getPressureProfile(i);

    JsonObject profileObject = profilesObject.createNestedObject();
    profileObject["profileId"] = pressureProfile.profileId;
    JsonArray pressure = profileObject.createNestedArray("pressure");

    int profileLength = sizeof(pressureProfile.pressure) / sizeof(int);
    for (int i = 0; i < profileLength; i++)
    {
      pressure.add(pressureProfile.pressure[i]);
    }
  }

  serializeJson(doc, *response);
  request->send(response); });

  server.on("/calibratePump", HTTP_GET, [](AsyncWebServerRequest *request)
            { 
              String runCalibration;

              if (request->hasParam("EnableCalibration")) {
                runCalibration = request->getParam("EnableCalibration")->value();
                if (runCalibration == "true") {
                  Serial.println("Start calibration");
                  runPumpCalibration = true;
                  boiler.setPower(0);
                } else {
                  Serial.println("Stop Calibration");
                  runPumpCalibration = false;
                  pump.setPower(0);
                }
              }
              request->send(200); });

  server.on("/updatePumpPower", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              // Needs better message like pump calibration mode is not
              if (!runPumpCalibration) {
                request->send(200);
                return;
              }
              int power;
              int pressure;

              if (request->hasParam("Power"))
              {
                power = request->getParam("Power")->value().toInt();
                // needs validation
                Serial.print("Setting pump power to: ");
                Serial.println(power);
                pump.setPower(power);
              }
              if (request->hasParam("Pressure"))
              {
                pressure = request->getParam("Pressure")->value().toInt();
              }
           calibrationPressurePoint = pressure;
           calibrationPumpPower = power;
              request->send(200); });

  AsyncCallbackJsonWebHandler *profileUploadHandler = new AsyncCallbackJsonWebHandler("uploadProfile", [](AsyncWebServerRequest *request, JsonVariant &json)
                                                                                      {
  Serial.println("Handling message now");

  JsonObject jsonObj = json.as<JsonObject>();
  if (jsonObj.isNull())
  {
    request->send(400);
    return;
  }
  int profileId = jsonObj["profileId"];
  JsonArray pressure = jsonObj["pressure"];
  const int profileLength = pressure.size();
  int pressureProfile[60] = {};

  for (int i = 0; i < profileLength; i++)
  {
    int pressurePoint = pressure[i];
    pressureProfile[i] = pressure[i];
  }

  bool profileUpdated = pressureProfileService->updatePressureProfile(profileId, pressureProfile);
  
  request->send(200); });

  server.serveStatic("/", SPIFFS, "/dashboard/");

  // Handle Web Server Events
  events.onConnect([](AsyncEventSourceClient *client)
                   {
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    } });
  server.addHandler(&events);
  server.addHandler(configurationUploadHandler);
  server.addHandler(profileUploadHandler);

  server.begin();

  pressureProfileService = new PressureProfileService();
  pressureProfile = pressureProfileService->getPressureProfile(selectedProfile);
  machineStats = new Stats();

  display = new Display(LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_BL_PIN);

  memcpy(pumpCalibrationMem, configuration->getConfiguration().pumpCalibration, 15);

  for (int i = 0; i < 15; i++)
  {
    pumpCalibrationMem[i] = configuration->getConfiguration().pumpCalibration[i];
  }

  calculateShotLength();

  cycleCount = machineStats->getCycleCount();
  Serial.print("Cycle count");
  Serial.print(cycleCount);
  Serial.println();

  // refactor this in settings file
  espressoTemperature = configuration->getConfiguration().boilerEspressoTemperature;
  steamTemperature = configuration->getConfiguration().boilerSteamTemperature;

  pump.begin(NORMAL_MODE, ON);
  pump.setPower(0);

  // todo needs a better way to initialise this without multiple calls to the getConfiguration() library function
  heaterPID = new PID_v2(configuration->getConfiguration().boilerP, configuration->getConfiguration().boilerI, configuration->getConfiguration().boilerD, PID::Direct, PID::P_On::Measurement);

  heaterPID->SetOutputLimits(0, espressoTemperature);

  heaterPID->SetMode(PID::Automatic);

  // for test only

  Serial.print("PID Mode ");
  Serial.print(heaterPID->GetMode());
  // test code only, needs a check on thermocouple first
  int tempBoilerTemp = 24;
  int boilerThermoCouple = getBoilerTemp();

  if (getBoilerTemp() > 0)
  {
    heaterPID->Start(getBoilerTemp(), 0, espressoTemperature);
  }
  else
  {
    heaterPID->Start(tempBoilerTemp, 0, espressoTemperature);
  }

  boiler.begin(NORMAL_MODE, ON);
  digitalWrite(DIMMER_BYPASS_PIN, LOW);

  systemState = 1;

  Serial.println();
  Serial.print("Version ");
  Serial.print(MAJOR_FIRMWARE_VERSION);
  Serial.print(".");
  Serial.print(MINOR_FIRMWARE_VERSION);
  Serial.println();

  AsyncElegantOTA.begin(&server);
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
  doc["cycles"] = cycleCount;
  doc["runningCycle"] = runningCycle;
  doc["currentCycleTime"] = cycleTime;
  doc["requiredTemperature"] = steamModeOn ? steamTemperature : espressoTemperature;
  doc["boilerPower"] = boiler.getPower();

  machineOnTime++;
  char output[384];
  serializeJson(doc, output);
  events.send(output, "dataPayload", millis());
}

void loop()
{
  Serial.println("Idling");
  int pumpPressureCount = 1;
  bool pumpCalibrated = false;
  while (runPumpCalibration)
  {
    int currentPressure = getPumpPressure();
    display->drawPumpCalibration(calibrationPressurePoint, currentPressure, calibrationPumpPower, pressureSensorReading);
    delay(1000);
  }

  int cycleTime = 0;
  int boilerTemp = 0;
  int steamTemp = 0;
  int pressure = 0;
  int pumpPressure = getPumpPressure();

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
    pressure = pressureProfile.pressure[cycleTime];
    int adjProfilePressure = pumpCalibrationMem[pressure];
    pump.setPower(adjProfilePressure);
    display->drawEspressoProgress(cycleTime, boilerTemp, pumpPressure, pressureProfile.pressure, shotLength);
  }
  // this may hit twice
  if (pumpSwitchOn && currentCycleTime >= shotLength)
  {
    pump.setPower(0);
    if (!screenSet)
    {
      cycleCount = machineStats->getCycleCount();
      display->drawCycleFinishedPage(cycleCount);
      machineStats->updateCycleCount();
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
      display->drawEspressoPreview(boilerTemp, pressureProfile.pressure, shotLength);
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
      heaterPID->SetOutputLimits(0, steamTemperature);
      heaterPID->Setpoint(steamTemperature);
    }
    else
    {
      heaterPID->SetOutputLimits(0, espressoTemperature);
      heaterPID->Setpoint(espressoTemperature);
    }
    temperatureLimitChanged = false;
  }

  delay(100);
  sendDataPayload(boilerTemp, steamTemp, pumpPressure, cycleTime);
}
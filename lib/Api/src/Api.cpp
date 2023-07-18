#include "Arduino.h"
#include "Api.h"
#include "Configuration.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "SPIFFS.h"
#include "AsyncElegantOTA.h"
// #define WEBSERVER_H // Fix for webserver wifi portal https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
// #include <ESPAsyncWebServer.h>

AsyncWebServer *_server;
Configuration *_configuration;
PressureProfileService *_pressureProfileService;
AsyncEventSource events("/data");

Api::Api(int serverPort, Configuration *configuration, PressureProfileService *pressureProfileService)
{
  _configuration = configuration;
  _server = new AsyncWebServer(serverPort);
  _pressureProfileService = pressureProfileService;
  // static files for dashboard
  _server->serveStatic("/", SPIFFS, "/dashboard/");
  _server->on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/dashboard/home.html"); });

  _server->on("/profiles", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/dashboard/profile.html"); });

  _server->on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/dashboard/setting.html"); });

  _server->on("/updatePumpPower", HTTP_GET, [](AsyncWebServerRequest *request)
              {
              int power;

              if (request->hasParam("Power"))
              {
                power = request->getParam("Power")->value().toInt();
                // need pump class as well
                //pump.setPower(power);
              }
              request->send(200); });

  _server->on("/calibratePump", HTTP_GET, [](AsyncWebServerRequest *request)
              {
              String runCalibration;

              if (request->hasParam("EnableCalibration")) {
                runCalibration = request->getParam("EnableCalibration")->value();
                if (runCalibration == "true") {
                  //runPumpCalibration = true;
                  //boiler.setPower(0);
                } else {
                  //runPumpCalibration = false;
                  //pump.setPower(0);
                }
              }
              request->send(200); });

  AsyncCallbackJsonWebHandler *profileUploadHandler = new AsyncCallbackJsonWebHandler("uploadProfile", [](AsyncWebServerRequest *request, JsonVariant &json)
                                                                                      {
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

        bool profileUpdated = _pressureProfileService->updatePressureProfile(profileId, pressureProfile);
        
        request->send(200); });

  _server->on("/profile/download", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    PressureProfile pressureProfile = _pressureProfileService->getLoadedPressureProfile();

    StaticJsonDocument<1050> doc;
    doc["profileId"] = pressureProfile.profileId;
    JsonArray pressure = doc.createNestedArray("pressure");

    int profileLength =sizeof(pressureProfile.pressure)/sizeof(int);
    for (int i = 0; i < profileLength; i++) {
      pressure.add(pressureProfile.pressure[i]);
    }
    serializeJson(doc, *response);
    request->send(response); });

  _server->on("/allProfiles", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    StaticJsonDocument<6144> doc;

    JsonArray profilesObject = doc.createNestedArray("profiles");
    for (int i = 1; i < 7; i++) {
      PressureProfile pressureProfile = _pressureProfileService->getPressureProfileById(i);
      
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

  AsyncCallbackJsonWebHandler *configurationUploadHandler = new AsyncCallbackJsonWebHandler("/uploadConfiguration", [](AsyncWebServerRequest *request, JsonVariant &json)
                                                                                            {

      JsonObject jsonObj = json.as<JsonObject>();
      if (jsonObj.isNull()) {
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
      int selectedProfile = jsonObj["selectedProfile"];

      JsonArray pumpCalibration = jsonObj["pumpCalibration"];
      const int pumpCalibrationLength = pumpCalibration.size();

      int pumpCalibrationData[15] = {};
      for (int i = 0; i < pumpCalibrationLength; i++) {
        pumpCalibrationData[i] = pumpCalibration[i];
      }

      bool machineConfigurationUpdated = _configuration->updateConfiguration(boilerTimeout, boilerSteamTemperature, boilerEspressoTemperature, autoFlushTime, fastHeatCutOff, pumpCalibrationData, boilerP, boilerI, boilerD, selectedProfile);

    //   if (machineConfigurationUpdated) {
    //     pressureProfile = pressureProfileService->getPressureProfile(selectedProfile);
    //     calculateShotLength();
    //   }


    request->send(200); });

  _server->on("/configuration/download", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      AsyncResponseStream *response = request->beginResponseStream("application/json");
      StaticJsonDocument<384> doc;

      MachineConfiguration machineConfiguration = _configuration->getConfiguration();

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

  _server->addHandler(profileUploadHandler);
  _server->addHandler(configurationUploadHandler);
  _server->addHandler(&events);
  _server->begin();

  AsyncElegantOTA.begin(_server);
}

void Api::sendDataPayload(char payload[384])
{
  events.send(payload, "dataPayload", millis());
}
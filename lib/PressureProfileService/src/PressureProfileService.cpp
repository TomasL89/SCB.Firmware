#include "Arduino.h"
#include "PressureProfileService.h"
#include "SPIFFS.h"
#include "ArduinoJson.h"
#include "Configuration.h"

#define DEFAULT_PROFILE_ID -1
#define DEFAULT_SHOT_TEMPERATURE 95
#define PROFILE_SIZE 1050

PressureProfile *_pressureProfile;
Configuration *_configurationForPressureProfileService;

int calculateShotLength(int pressure[60])
{
    for (int i = 0; i < 60; i++)
    {
        int pressurePoint = pressure[i];
        if (pressurePoint < 0)
        {
            return i;
            break;
        }
    }
    return 0;
}

PressureProfile getPressureProfile(int profileId)
{
    String fileExtension = ".txt";
    String fileName = "/p" + String(profileId) + fileExtension;
    Serial.print("Opening file: ");
    Serial.println(fileName);
    File pressureFile = SPIFFS.open(fileName);
    StaticJsonDocument<PROFILE_SIZE> doc;
    DeserializationError error = deserializeJson(doc, pressureFile.readString());

    PressureProfile loadedProfile;
    loadedProfile.profileId = doc["profileId"];
    JsonArray pressureData = doc["pressure"];

    for (int i = 0; i < 60; i++)
    {
        loadedProfile.pressure[i] = pressureData[i];
    }

    loadedProfile.shotLength = calculateShotLength(loadedProfile.pressure);

    return loadedProfile;
}

PressureProfileService::PressureProfileService(Configuration *configuration)
{
    Serial.println("Loading pressure profile service");
    _configurationForPressureProfileService = configuration;

    int selectedProfile = _configurationForPressureProfileService->getConfiguration().selectedProfile;
    Serial.print("Attempting to get profile: ");
    Serial.print(selectedProfile);
    _pressureProfile = getPressureProfile(selectedProfile);
}

bool PressureProfileService::updatePressureProfile(int profileId, int pressure[60])
{
    StaticJsonDocument<PROFILE_SIZE> doc;
    doc["profileId"] = profileId;
    JsonArray pressureDoc = doc.createNestedArray("pressure");
    _pressureProfile.profileId = profileId;

    for (int i = 0; i < 60; i++)
    {
        _pressureProfile.pressure[i] = pressure[i];
        pressureDoc.add(pressure[i]);
    }

    _pressureProfile.shotLength = calculateShotLength(_pressureProfile.pressure);

    String fileExtension = ".txt";
    String fileName = "/p" + profileId + fileExtension;
    File file = SPIFFS.open("/profile.txt", FILE_WRITE);
    if (!file)
    {
        Serial.println("Unable to open profile file for writing");
        // warn user
        return false;
    }
    char output[PROFILE_SIZE];
    serializeJson(doc, output);
    Serial.print(output);
    if (file.print(output))
    {
        Serial.println("File written");
    }
    else
    {
        Serial.println("File failed");
    }

    file.close();
    return true;
}

PressureProfile PressureProfileService::getPressureProfileById(int profileId)
{
    return getPressureProfile(profileId);
}

PressureProfile PressureProfileService::getLoadedPressureProfile()
{
    return _pressureProfile;
}

void PressureProfileService::setPressureProfile(int profileId)
{
    _pressureProfile = getPressureProfile(profileId);
}

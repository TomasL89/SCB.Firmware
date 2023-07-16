#include "Arduino.h"
#include "PressureProfileService.h"
#include "SPIFFS.h"
#include "ArduinoJson.h"

#define DEFAULT_PROFILE_ID -1
#define DEFAULT_SHOT_TEMPERATURE 95
#define PROFILE_SIZE 1050

PressureProfileService::PressureProfileService()
{
    int defaultPressureProfile[60] = {
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 8, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    // update this once the selected profile is stored in another settings file
    File pressureFile = SPIFFS.open("/p1.txt");
    if (!pressureFile)
    {
        Serial.println("No pressure profile found, creating new one");
        // -1 to indicate it is the default profile
        if (updatePressureProfile(DEFAULT_PROFILE_ID, defaultPressureProfile))
        {
            Serial.println("Created default pressure profile and saved it");
        }
    }
    else
    {
        StaticJsonDocument<PROFILE_SIZE> doc;
        DeserializationError error = deserializeJson(doc, pressureFile.readString());
        if (error)
        {
            Serial.println("Error deserialising pressure file, using default");
            if (updatePressureProfile(DEFAULT_PROFILE_ID, defaultPressureProfile))
            {
                Serial.println("Created default pressure file and saved it");
            }
        }

        _pressureProfile.profileId = doc["profile"];
        JsonArray pressureData = doc["pressure"];

        for (int i = 0; i < 60; i++)
        {
            _pressureProfile.pressure[i] = pressureData[i];
        }
    }
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

PressureProfile PressureProfileService::getPressureProfile(int profileId)
{
    // this is different from Configuration.h as it seems that when a default profile is setup for the first time
    // that the _pressureProfile is not persisting the data in memory and therefore returns 0's, reading from file
    // means that this should be avoided
    if (profileId < 1 || profileId > 5)
    {
        Serial.println("Using default profile 1");
        profileId = 1;
    }
    String fileExtension = ".txt";
    String fileName = "/p" + String(profileId) + fileExtension;
    Serial.print("Opening file: ");
    Serial.println(fileName);
    File pressureFile = SPIFFS.open(fileName);
    StaticJsonDocument<PROFILE_SIZE> doc;
    DeserializationError error = deserializeJson(doc, pressureFile.readString());

    _pressureProfile.profileId = doc["profileId"];
    JsonArray pressureData = doc["pressure"];

    for (int i = 0; i < 60; i++)
    {
        _pressureProfile.pressure[i] = pressureData[i];
    }

    return _pressureProfile;
}

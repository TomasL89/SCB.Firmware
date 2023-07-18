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

// PressureProfile loadPressureProfileFromFile(int profileId)
// {
//     if (profileId < 1 || profileId > 5)
//     {
//         Serial.println("Using default profile 1");
//         profileId = 1;
//     }
//     String fileExtension = ".txt";
//     String fileName = "/p" + String(profileId) + fileExtension;
//     Serial.print("Opening file: ");
//     Serial.println(fileName);
//     File pressureFile = SPIFFS.open(fileName);
//     StaticJsonDocument<PROFILE_SIZE> doc;
//     DeserializationError error = deserializeJson(doc, pressureFile.readString());

//     _pressureProfile->profileId = doc["profileId"];
//     JsonArray pressureData = doc["pressure"];

//     for (int i = 0; i < 60; i++)
//     {
//         _pressureProfile->pressure[i] = pressureData[i];
//     }

//     _pressureProfile->shotLength = calculateShotLength(_pressureProfile->pressure);
// }

PressureProfileService::PressureProfileService(Configuration *configuration)
{
    Serial.println("Loading pressure profile service");
    _configurationForPressureProfileService = configuration;
    // int defaultPressureProfile[60] = {
    //     2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    //     9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    //     9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    //     9, 9, 9, 9, 8, -1, -1, -1, -1, -1,
    //     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    //     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    // update this once the selected profile is stored in another settings file
    // File pressureFile = SPIFFS.open("/p1.txt");
    // if (!pressureFile)
    // {
    //     Serial.println("No pressure profile found, creating new one");
    //     // -1 to indicate it is the default profile
    //     if (updatePressureProfile(DEFAULT_PROFILE_ID, defaultPressureProfile))
    //     {
    //         Serial.println("Created default pressure profile and saved it");
    //     }
    // }
    // else
    // {'

    int selectedProfile = _configurationForPressureProfileService->getConfiguration().selectedProfile;
    Serial.print("Attempting to get profile: ");
    Serial.print(selectedProfile);
    _pressureProfile = getPressureProfile(selectedProfile);

    // if ()
    // if (error)
    // {
    //     Serial.println("Error deserialising pressure file, using default");
    //     if (updatePressureProfile(DEFAULT_PROFILE_ID, defaultPressureProfile))
    //     {
    //         Serial.println("Created default pressure file and saved it");
    //     }
    // }

    // _pressureProfile.profileId = doc["profile"];
    // JsonArray pressureData = doc["pressure"];

    // for (int i = 0; i < 60; i++)
    // {
    //     _pressureProfile.pressure[i] = pressureData[i];
    // }

    // _pressureProfile.shotLength = calculateShotLength(_pressureProfile.pressure);
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
    // loadPressureProfileFromFile(profileId);
    // return _pressureProfile;
    return getPressureProfile(profileId);
}

PressureProfile PressureProfileService::getLoadedPressureProfile()
{
    return _pressureProfile;
}

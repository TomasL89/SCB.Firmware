#include "Arduino.h"
#include "Stats.h"
#include "SPIFFS.h"
#include "ArduinoJson.h"

#define STAT_FILE_SIZE 48

void writeToFile(int cycleCount)
{
    StaticJsonDocument<STAT_FILE_SIZE> doc;

    File file = SPIFFS.open("/stats.txt", FILE_WRITE);
    if (!file)
    {
        Serial.println("Unable to open stats file for writing");
        return;
    }
    
    doc["cycleCount"] = cycleCount;

    char output[STAT_FILE_SIZE];
    serializeJson(doc, output);
    if (file.print(output))
    {
        Serial.println("cycle file written");
    }
    else 
    {
        Serial.println("cycle file failed");
    }
}

Stats::Stats()
{
    File statsFile = SPIFFS.open("/stats.txt");
    if (!statsFile) 
    {
        Serial.println("No stats file found, creating new one");

        cycleCount = 0;
        writeToFile(cycleCount);
    }
    else 
    {
        StaticJsonDocument<STAT_FILE_SIZE> doc;
        DeserializationError error = deserializeJson(doc, statsFile.readString());
        if (error)
        {
            // needs a better solution, like getting backup from the cloud or similiar
            Serial.println("Error deserialising file, using default");
            cycleCount = 0;
            writeToFile(cycleCount);
        }
        cycleCount = doc["cycleCount"];
    }
}

void Stats::updateCycleCount()
{
    cycleCount += 1;
    writeToFile(cycleCount);
}

int Stats::getCycleCount()
{
    return cycleCount;
}

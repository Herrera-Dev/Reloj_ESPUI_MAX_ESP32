#include <Preferences.h>
Preferences mem;
void InitOTA()
{
  ArduinoOTA.onStart([]()
                     {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_SPIFFS
        type = "filesystem";
      }
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); });

  ArduinoOTA.onEnd([]()
                   { 
    Serial.println("\nEnd");
    ESP.restart(); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
      delay(10);
      ESP.restart(); });

  ArduinoOTA.begin();
  Serial.println("");
  Serial.println("OTA iniciado");
}

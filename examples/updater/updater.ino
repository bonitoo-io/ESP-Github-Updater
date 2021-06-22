#include <ESP8266WiFi.h>
#include "ESPGithubUpdater.h"

// If update file has version in the name, use %version% variable in the file name. e.g. ws-firmware-%version%.bin
ESPGithubUpdater updater("<owner>","<repo>", "<update-file-name>");

void setup() {
  Serial.begin(74880);
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin("SSID", "password");
  int i = 30;
  while (WiFi.status() != WL_CONNECTED && i>0) {
    Serial.print(".");
    delay(500);
    --i;
  }
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi failed");
    Serial.println("Restarting");
    delay(3000);  
    ESP.restart();
  }
  // Github has anonymous rate limit 60/hours
  // In order to access private repos or use higher limit, 1500/hour, user authenticated request 
  updater.setAuthorization("<username>","<token>");
  // Set MD5 sum file name to verify MD5 sum. 
  // If MD5 sum file has version in the name, use %version% variable in the file name. e.g. ws-firmware-%version%.md5
  updater.setMD5FileName("<update-md5-file-name>");

  Serial.println();
  Serial.print("version 0.1: ");
  Serial.println(updater.checkVersion("0.1"));
  Serial.print("Latest version: ");
  Serial.println(updater.getLatestVersion());
  Serial.print("Latest beta: ");
  String ver = updater.getLatestVersion(true);
  if(!ver.length()) {
    Serial.println(updater.getLastError());  
  } else {
    Serial.println(ver);
    updater.runUpdate(ver, [ver](int progress) {
      Serial.printf("Updating to %s: %d\%\n",ver.c_str(), progress);
    });
  }
}

void loop() {
   delay(5000);
}
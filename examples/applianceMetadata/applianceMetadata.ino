#include <Arduino.h>
#include "GEA2.h"

static GEA2 gea2;

void setup()
{
  Serial.begin(115200);

  Serial1.begin(GEA2::baud);

  gea2.begin(Serial1);

  struct ModelNumber {
    char contents[32];
  };
  auto model_number = gea2.readERD<ModelNumber>(0x0001);
  if(model_number.status == GEA2::ReadStatus::success) {
    Serial.printf("Model Number: %.32s\n", model_number.value.contents);
  }
  else {
    Serial.printf("Failed to read Model Number\n");
  }

  struct SerialNumber {
    char contents[32];
  };
  auto serial_number = gea2.readERD<SerialNumber>(0x0002);
  if(serial_number.status == GEA2::ReadStatus::success) {
    Serial.printf("Serial Number: %.32s\n", serial_number.value);
  }
  else {
    Serial.printf("Failed to read Serial Number\n");
  }

  auto appliance_type = gea2.readERD<GEA2::U8>(0x0008);
  if(appliance_type.status == GEA2::ReadStatus::success) {
    Serial.printf("Appliance Type: %d\n", appliance_type.value.read());
  }
  else {
    Serial.printf("Failed to read Appliance Type\n");
  }

  auto personality = gea2.readERD<GEA2::U32>(0x0035);
  if(personality.status == GEA2::ReadStatus::success) {
    Serial.printf("Personality: %d\n", personality.value.read());
  }
  else {
    Serial.printf("Failed to read Personality\n");
  }
}

void loop()
{
  gea2.loop();
}

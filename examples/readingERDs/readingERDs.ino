#include <Arduino.h>
#include <GEA2.h>

static GEA2 gea2;

void setup()
{
  Serial.begin(115200);

  Serial1.begin(GEA2::baud);

  gea2.begin(Serial1);

  gea2.readERDAsync(
    0x0035, +[](GEA2::ReadStatus status, GEA2::U32 value) {
      if(status == GEA2::ReadStatus::success) {
        Serial.printf("Successfully read ERD 0x0035 asynchronously: 0x%08X\n", value.read());
      }
      else {
        Serial.printf("Failed to read ERD 0x0035 asynchronously\n");
      }
    });

  auto result = gea2.readERD<GEA2::U32>(0x0035);
  if(result.status == GEA2::ReadStatus::success) {
    Serial.printf("Successfully read ERD 0x0035: 0x%08X\n", value.read());
  }
  else {
    Serial.printf("Failed to read ERD 0x0035\n");
  }
}

void loop()
{
  gea2.loop();
}

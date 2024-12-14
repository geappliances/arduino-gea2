#include <Arduino.h>
#include <GEA2.h>

static GEA2 gea2;

void setup()
{
  Serial.begin(115200);

  Serial1.begin(GEA2::baud);

  gea2.begin(Serial1);

  gea2.writeERDAsync(
    0x0035, GEA2::U32(0x01ABCDEF), +[](GEA2::WriteStatus status) {
      if(status == GEA2::WriteStatus::success) {
        Serial.println("Wrote ERD 0x0035 asynchronously");
      }
      else {
        Serial.println("Failed to write ERD 0x0035 asynchronously");
      }
    });

  if(gea2.writeERD(0x0035, GEA2::U32(0x01ABCDEF)) == GEA2::WriteStatus::success) {
    Serial.println("Wrote ERD 0x0035");
  }
  else {
    Serial.println("Failed to write ERD 0x0035");
  }
}

void loop()
{
  gea2.loop();
}

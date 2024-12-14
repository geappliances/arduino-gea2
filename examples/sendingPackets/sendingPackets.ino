#include <Arduino.h>
#include <GEA2.h>

static GEA2 gea2;

void setup()
{
  Serial.begin(115200);

  Serial1.begin(GEA2::baud);

  gea2.begin(Serial1);

  gea2.sendPacket(GEA2::Packet(0xE4, 0xFF, { 0x01 }));
}

void loop()
{
  gea2.loop();
}

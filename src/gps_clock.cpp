#include <SoftwareSerial.h> // GPS通信的软件串口库
#include <TinyGPS++.h>      // GPS解析库
#include <AceTime.h>        // AceTime 时间和时区处理库
#include <ESP.h>
using namespace ace_time;

// 定义引脚
#define GPS_TX_PIN 4
#define GPS_RX_PIN 5
#define GPS_PPS_PIN 15

// 创建对象
SoftwareSerial gpsSerial(GPS_TX_PIN, GPS_RX_PIN);
TinyGPSPlus gps;

// 定义 Asia/Shanghai 时区
static BasicZoneProcessor localTzProcessor;
TimeZone localTz = TimeZone::forZoneInfo(&zonedb::kZoneAsia_Shanghai, &localTzProcessor);
// 星期映射数组（1=星期日，2=星期一，...，7=星期六）
static const char* const WEEKDAYS[] PROGMEM = {
  "", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"
};

// 全局变量
volatile unsigned long ppsTime = 0;     // 存储 PPS 时间
unsigned long lastGPSTime = 0;          // 存储上次 GPS 时间更新
static int lastSecond = -1;             // 记录上次输出的秒值
static bool gpsLocked = false;          // GPS 锁定状态
static int ppsCount = 0;                // PPS 触发计数
static unsigned long lastValidData = 0; // 上次有效数据时间

// PPS 中断服务程序
IRAM_ATTR void ppsInterrupt()
{
  ppsTime = gps.time.value();
  ppsCount++;
}

void setup()
{
  ESP.wdtEnable(8000); // 8秒看门狗
  Serial.begin(115200);
  gpsSerial.begin(38400);
  pinMode(GPS_PPS_PIN, INPUT);
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);                                                      // LED 关闭
  attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), ppsInterrupt, RISING); // 绑定 PPS 中断
}

void loop()
{
  //  if (!gpsSerial.available()) {
  //    delay(10); // 无数据时短暂暂停，降低 CPU 占用
  //  }
  
  ESP.wdtFeed();
  
  while (gpsSerial.available() > 0)
  {
    if (gps.encode(gpsSerial.read()) && gps.location.isValid() && gps.time.isValid() && gps.date.isValid())
    {
      gpsLocked = true;
      digitalWrite(2, HIGH); // LED 点亮
      lastValidData = millis();

      // 获取 GPS 日期和时间（UTC）
      static int year, month, day, hour, minute, second;
      year = gps.date.year();
      month = gps.date.month();
      day = gps.date.day();
      hour = gps.time.hour();
      minute = gps.time.minute();
      second = gps.time.second();

      // 创建 UTC 时间
      ZonedDateTime utcTime = ZonedDateTime::forComponents(year, month, day, hour, minute, second, TimeZone::forUtc());
      ZonedDateTime localTime = utcTime.convertToTimeZone(localTz);

      // PPS 校准
      unsigned long currentTime = millis();
      if (currentTime - lastGPSTime >= 1000)
      {
        lastGPSTime = currentTime;
        if (ppsTime != 0 && labs(ppsTime - gps.time.value()) < 1000000)
        { // 确保 PPS 合理
          unsigned long timeDiff = ppsTime - gps.time.value();
          if (labs(timeDiff) > 100)
          { // 时间差大于 100ms 校准
            utcTime = ZonedDateTime::forComponents(
                year, month, day,
                ppsTime / 1000000,           // 小时
                (ppsTime % 1000000) / 10000, // 分钟
                (ppsTime % 10000) / 100,     // 秒
                TimeZone::forUtc());
            localTime = utcTime.convertToTimeZone(localTz);
          }
        }
      }

      // 输出本地时间（仅当秒值变化时）
      if (localTime.second() != lastSecond)
      {
        lastSecond = localTime.second();
        Serial.print(F("Local Time: "));
        Serial.print(localTime.year());
        Serial.print(F("-"));
        Serial.print(localTime.month() < 10 ? F("0") : F(""));
        Serial.print(localTime.month());
        Serial.print(F("-"));
        Serial.print(localTime.day() < 10 ? F("0") : F(""));
        Serial.print(localTime.day());
        Serial.print(F(" "));
        Serial.print(localTime.hour() < 10 ? F("0") : F(""));
        Serial.print(localTime.hour());
        Serial.print(F(":"));
        Serial.print(localTime.minute() < 10 ? F("0") : F(""));
        Serial.print(localTime.minute());
        Serial.print(F(":"));
        Serial.print(localTime.second() < 10 ? F("0") : F(""));
        Serial.print(localTime.second());
        Serial.print(F(" | Day: "));
        Serial.print(FPSTR(WEEKDAYS[localTime.dayOfWeek()])); // 输出星期，如 MON
        //        Serial.print(F(" | Lat: "));
        //        Serial.print(gps.location.lat(), 6);
        //        Serial.print(F(" | Lon: "));
        //        Serial.print(gps.location.lng(), 6);
        Serial.print(F(" | PPS Count: "));
        Serial.println(ppsCount);
		Serial.print(F(" | DayOfWeek: "));
        Serial.println(localTime.dayOfWeek()); // 输出 dayOfWeek 值
      }
    }
    else
    {
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink >= 500)
      {
        lastBlink = millis();
        digitalWrite(2, gpsLocked ? HIGH : !digitalRead(2)); // 未锁定时闪烁
      }
      if (millis() - lastValidData > 30000)
      {
        Serial.println(F("GPS signal lost for 30 seconds"));
        lastValidData = millis() - 25000; // 每5秒报告一次
      }
    }
  }
}

#include <SoftwareSerial.h> // GPS通信的软件串口库
#include <TinyGPS++.h>      // GPS解析库
#include <AceTime.h>        // AceTime 时间和时区处理库
#include <ESP.h>
#include <SHTSensor.h>
using namespace ace_time;

// 定义引脚
#define GPS_RX_PIN 4
#define GPS_TX_PIN 5
#define GPS_PPS_PIN 15
#define SDA_PIN 12
#define SCL_PIN 13

// 创建对象
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
SHTSensor sht;

// 定义 Asia/Shanghai 时区
static BasicZoneProcessor localTzProcessor;
TimeZone localTz = TimeZone::forZoneInfo(&zonedb::kZoneAsia_Shanghai, &localTzProcessor);

// 星期映射数组（调整为 1=MON, ..., 6=SAT, 7=SUN）
static const char* const WEEKDAYS[] PROGMEM = {
  "", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"
};

// 全局变量
volatile unsigned long ppsTime = 0;   // 存储 PPS 时间
unsigned long lastGPSTime = 0;        // 存储上次 GPS 时间更新
static int lastSecond = -1;           // 记录上次输出的秒值
static bool gpsLocked = false;        // GPS 锁定状态
static int ppsCount = 0;              // PPS 触发计数
static unsigned long lastValidData = 0; // 上次有效数据时间
static uint32_t next_th_sensor_sample_millis = 0; // 下次温湿度采样时间

// PPS 中断服务程序
IRAM_ATTR void ppsInterrupt() {
  ppsTime = gps.time.value();
  ppsCount++;
}

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(38400);
  pinMode(GPS_PPS_PIN, INPUT);
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW); // LED 关闭
  Wire.begin(SDA_PIN, SCL_PIN);
  sht.init(); 
  sht.setAccuracy(SHTSensor::SHT_ACCURACY_MEDIUM); // only supported by SHT3x

  attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), ppsInterrupt, RISING); // 绑定 PPS 中断
  ESP.wdtEnable(8000); // 8秒看门狗
  // 初始化下一次温湿度采样时间，首次立即采样
  next_th_sensor_sample_millis = millis(); 
}

void loop() {
  ESP.wdtFeed();
  if (!gpsSerial.available()) {
    delay(10); // 无数据时短暂暂停，降低 CPU 占用
  }

  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read()) && gps.location.isValid() && gps.time.isValid() && gps.date.isValid()) {
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
      if (currentTime - lastGPSTime >= 1000) {
        lastGPSTime = currentTime;
        if (ppsTime != 0 && labs(ppsTime - gps.time.value()) < 1000000) { // 确保 PPS 合理
          unsigned long timeDiff = ppsTime - gps.time.value();
          if (labs(timeDiff) > 100) { // 时间差大于 100ms 校准
            utcTime = ZonedDateTime::forComponents(
              year, month, day,
              ppsTime / 1000000,          // 小时
              (ppsTime % 1000000) / 10000, // 分钟
              (ppsTime % 10000) / 100,     // 秒
              TimeZone::forUtc()
            );
            localTime = utcTime.convertToTimeZone(localTz);
          }
        }
      }

      // 输出本地时间（仅当秒值变化时）
      if (localTime.second() != lastSecond) {
        lastSecond = localTime.second();
        Serial.print(F("UTC Time: "));
        Serial.print(utcTime.year());
        Serial.print(F("-"));
        Serial.print(utcTime.month() < 10 ? F("0") : F(""));
        Serial.print(utcTime.month());
        Serial.print(F("-"));
        Serial.print(utcTime.day() < 10 ? F("0") : F(""));
        Serial.print(utcTime.day());
        Serial.print(F(" "));
        Serial.print(utcTime.hour() < 10 ? F("0") : F(""));
        Serial.print(utcTime.hour());
        Serial.print(F(":"));
        Serial.print(utcTime.minute() < 10 ? F("0") : F(""));
        Serial.print(utcTime.minute());
        Serial.print(F(":"));
        Serial.print(utcTime.second() < 10 ? F("0") : F(""));
        Serial.print(utcTime.second());
        Serial.println();

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
        Serial.print(FPSTR(WEEKDAYS[localTime.dayOfWeek()])); // 输出星期，如 SUN
        Serial.print(F(" | Lat: "));
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(" | Lon: "));
        Serial.print(gps.location.lng(), 6);
        Serial.print(F(" | PPS Count: "));
        Serial.print(ppsCount);
        Serial.print(F(" | DayOfWeek: "));
        Serial.println(localTime.dayOfWeek()); // 调试：输出 dayOfWeek 值
      }
    } else {
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink >= 500) {
        lastBlink = millis();
        digitalWrite(2, gpsLocked ? HIGH : !digitalRead(2)); // 未锁定时闪烁
      }
      if (millis() - lastValidData > 30000) {
        Serial.println(F("GPS signal lost for 30 seconds"));
        lastValidData = millis() - 25000; // 每5秒报告一次
      }
    }
  }

  // 每5秒读取并输出温湿度
  if (millis() >= next_th_sensor_sample_millis) {
    if (sht.readSample()) { // 尝试读取传感器数据
      Serial.print(F("Temperature: "));
      Serial.print(sht.getTemperature(), 2); // 保留两位小数
      Serial.print(F(" *C    "));
      Serial.print(F("Humidity: "));
      Serial.print(sht.getHumidity(), 2); // 保留两位小数
      Serial.println(F(" %"));
    } else {
      Serial.println(F("Failed to read SHT sensor."));
    }
    // 设置下一次采样时间
    next_th_sensor_sample_millis = millis() + 5000; 
  }
}
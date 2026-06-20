#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Firebase.h>
#include <RTClib.h>
#include <DHT.h>
#include <SPI.h>
#include <DMD32.h>
#include <Preferences.h>
#include "fonts/SystemFont5x7.h"
#include "fonts/Font3x5.h"

#define SSID "Kha"
#define PASSWORD "27122005"
#define API_KEY "AIzaSyCgXZegFdu02rhzI90DD1a1by0CidEaG5g"
#define DATABASE_URL "https://dong-ho-dien-tu-daktdt-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "eAx4Js7aVrc2hSwqDcmbwhLdoucxe02370UUDGjM"

#define BELL 25
#define DHTPIN 15
#define MAX_BAO_THUC 50
#define DHTTYPE DHT22

#define DISPLAYS_ACROSS 1
#define DISPLAYS_DOWN 1

FirebaseData Data;
FirebaseAuth Auth;
FirebaseConfig Config;

DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);
hw_timer_t *timer = NULL;

RTC_DS3231 rtc;
DHT dht(DHTPIN, DHTTYPE);
Preferences preferences;

const char daysOfTheWeek[7][9] = {"Chu Nhat", "Thu 2", "Thu 3", "Thu 4", "Thu 5", "Thu 6", "Thu 7"};
unsigned long TimeChuongReo = 0;
bool tiengchuongreo = false;
unsigned long TimeDocDS3231 = 0;
unsigned long TimeDocDHT = 0;
unsigned long TimeCheckFirebase = 0;
unsigned long TimeCheckDatNgay = 0;
unsigned long TimeCheckDatGio = 0;
unsigned long TimeCheckDoSang = 0;

int8_t phutcuoicung = -1;
int8_t NhietDo = 0;
uint8_t DoAm = 0;
uint8_t Gio = 0;
uint8_t Phut = 0;
uint8_t Giay = 0;

typedef struct __attribute__((packed))
{
  uint8_t gio;
  uint8_t phut;
  bool active;
} baothuc;

baothuc dsbaothuc[MAX_BAO_THUC];
bool firebaseDaKhoiTao = false;

void IRAM_ATTR triggerScan();
void KiemTraTatChuong();
void BaoThuc(DateTime now);
void DongHo(DateTime now);
void CamBienDHT();
void XuLyDocBaoThucFirebase();
void XuLyDatNgayFirebase();
void XuLyDatGioFirebase();
void XuLyDoSangFirebase();
void MatrixPanel();
void KichHoatCauHinhFirebase();
void DocBaoThucTuFlash();
void LuuBaoThucVaoFlash();
void TaskKhoiTaoNgatCore0(void *ThamSo);

// Vẽ dấu % tùy chỉnh tại (ox, oy) theo pattern 8x8
void VeDauPhanTram(int ox, int oy)
{
  // pattern[row] = bitmask 8 bit, bit7=col0 ... bit0=col7
  // pattern 7 cột x 8 hàng, bit6=col0 .. bit0=col6
  const uint8_t pattern[7] = {
    0x61,
    0x62,
    0x04,
    0x08,
    0x10,
    0x23,
    0x43,
  };
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 7; col++) {
      if (pattern[row] & (0x40 >> col)) {
        dmd.writePixel(ox + col, oy + row, GRAPHICS_NORMAL, 1);
      }
    }
  }
}

void setup()
{
  Serial.begin(9600);
  delay(200);

  if (!rtc.begin())
  {
    delay(200);
    Serial.println("\nKhong The Ket Noi Voi RTC");
    Serial.flush();
    while (1)
      delay(10);
  }

  DocBaoThucTuFlash();

  Serial.println("\nChuan bi ket noi WiFi");
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  WiFi.setAutoReconnect(false);
  Serial.print("Dang Ket Noi Wifi ");

  unsigned long thoiGianKetNoiWifi = millis();

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
    if (millis() - thoiGianKetNoiWifi > 10000)
    {
      Serial.println("\n[WiFi] Ket noi that bai! Chuyen sang che do OFFLINE.");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("\nDa Ket Noi WiFi Thanh Cong\n");
    WiFi.setAutoReconnect(true);
    KichHoatCauHinhFirebase();
  }

  dht.begin();
  delay(200);
  pinMode(BELL, OUTPUT);

  SPI.begin(18, -1, 23, -1);
  delay(200);
  dmd.clearScreen(true);
  dmd.selectFont(System5x7);

  xTaskCreatePinnedToCore(
      TaskKhoiTaoNgatCore0,
      "InitNgatCore0",
      2048,
      NULL,
      configMAX_PRIORITIES,
      NULL,
      0);

  delay(200);
}

void loop()
{
  DateTime now = rtc.now();

  BaoThuc(now);
  KiemTraTatChuong();
  MatrixPanel();
  DongHo(now);
  CamBienDHT();
  XuLyDocBaoThucFirebase();
  XuLyDatNgayFirebase();
  XuLyDatGioFirebase();
  XuLyDoSangFirebase();
}

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
}

void KichHoatCauHinhFirebase()
{
  if (!firebaseDaKhoiTao)
  {
    Config.api_key = API_KEY;
    Config.database_url = DATABASE_URL;
    Config.signer.tokens.legacy_token = FIREBASE_AUTH;

    Firebase.begin(&Config, &Auth);
    Firebase.reconnectWiFi(true);
    firebaseDaKhoiTao = true;
    Serial.println("[Firebase] Da kich hoat cau hinh Firebase an toan!");
  }
}

void DocBaoThucTuFlash()
{
  preferences.begin("BaoThucNS", true);
  size_t kieuSize = sizeof(dsbaothuc);
  size_t readLen = preferences.getBytes("dsBT", dsbaothuc, kieuSize);
  preferences.end();

  if (readLen == 0)
  {
    for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
    {
      dsbaothuc[i].gio = 0;
      dsbaothuc[i].phut = 0;
      dsbaothuc[i].active = false;
    }
    Serial.println("\n[Flash] Vung nho trong. Da khoi tao mac dinh.");
    delay(100);
  }
  else
  {
    Serial.println("\n[Flash] Khoi phuc danh sach tu Flash thanh cong!");
    delay(100);
  }
}

void LuuBaoThucVaoFlash()
{
  if (timer != NULL)
  {
    timerAlarmDisable(timer);
  }

  preferences.begin("BaoThucNS", false);
  size_t kieuSize = sizeof(dsbaothuc);
  preferences.putBytes("dsBT", dsbaothuc, kieuSize);
  preferences.end();

  if (timer != NULL)
  {
    timerAlarmEnable(timer);
  }
  Serial.println("[Flash] Da khoa ngat ngam - Dong bo Flash an toan tuyet doi!");
}

void DongHo(DateTime now)
{
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeDocDS3231 >= 1000 || TimeDocDS3231 == 0))
  {
    TimeDocDS3231 = millis();

    FirebaseJson json;
    json.set("GioGiac/Gio", now.hour());
    json.set("GioGiac/Phut", now.minute());
    json.set("GioGiac/Giay", now.second());
    json.set("Date/Ngay", now.day());
    json.set("Date/Thang", now.month());
    json.set("Date/Nam", now.year());
    json.set("Date/Thu", now.dayOfTheWeek());

    Firebase.RTDB.updateNode(&Data, F("/DongHo/ThoiGian"), &json);
  }
  Gio = now.hour();
  Phut = now.minute();
  Giay = now.second();
}

void BaoThuc(DateTime now)
{
  if (tiengchuongreo || now.minute() == phutcuoicung)
  {
    if (now.minute() != phutcuoicung && phutcuoicung != -1)
    {
      phutcuoicung = -1;
    }
    return;
  }

  for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
  {
    if (dsbaothuc[i].active && now.hour() == dsbaothuc[i].gio && now.minute() == dsbaothuc[i].phut)
    {
      tiengchuongreo = true;
      TimeChuongReo = millis();
      phutcuoicung = now.minute();
      digitalWrite(BELL, HIGH);
      Serial.printf("\nBAO THUC SO %d KICH HOAT!\n", i + 1);
      break;
    }
  }
}

void KiemTraTatChuong()
{
  if (tiengchuongreo)
  {
    if (millis() - TimeChuongReo <= 1000)
    {
      digitalWrite(BELL, !digitalRead(BELL));
    }
    else
    {
      tiengchuongreo = false;
      digitalWrite(BELL, LOW);
      Serial.println("\n--- Tu dong tat chuong thanh cong! ---");
    }
  }
}

void CamBienDHT()
{
  if (millis() - TimeDocDHT > 15000 || TimeDocDHT == 0)
  {
    TimeDocDHT = millis();
    float temp = dht.readTemperature();
    float humi = dht.readHumidity();
    if (!isnan(temp) && !isnan(humi))
    {
      if (firebaseDaKhoiTao && Firebase.ready())
      {
        Firebase.RTDB.setFloat(&Data, F("/CamBien/NhietDo"), temp);
        Firebase.RTDB.setFloat(&Data, F("/CamBien/DoAm"), humi);
      }
      NhietDo = (int8_t)temp;
      DoAm = (uint8_t)humi;
    }
  }
}

void XuLyDocBaoThucFirebase()
{
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckFirebase > 7000 || TimeCheckFirebase == 0))
  {
    TimeCheckFirebase = millis();

    if (Firebase.RTDB.getJSON(&Data, F("/DongHo/dsBaoThuc")))
    {
      if (Data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
      {

        FirebaseJson &json = Data.jsonObject();
        FirebaseJsonData resultBaoThuc, resultGio, resultPhut, resultActive;
        String pathBaoThuc, pathGio, pathPhut, pathActive;

        bool coThayDoi = false;

        for (int i = 0; i < MAX_BAO_THUC; i++)
        {
          pathBaoThuc = "/BaoThuc" + String(i + 1);
          json.get(resultBaoThuc, pathBaoThuc);

          if (resultBaoThuc.success && resultBaoThuc.type == "object")
          {
            pathGio = pathBaoThuc + "/gio";
            pathPhut = pathBaoThuc + "/phut";
            pathActive = pathBaoThuc + "/active";

            json.get(resultGio, pathGio);
            json.get(resultPhut, pathPhut);
            json.get(resultActive, pathActive);

            uint8_t g_moi = resultGio.success ? resultGio.intValue : 0;
            uint8_t p_moi = resultPhut.success ? resultPhut.intValue : 0;
            bool a_moi = resultActive.success ? resultActive.boolValue : false;

            if (dsbaothuc[i].gio != g_moi || dsbaothuc[i].phut != p_moi || dsbaothuc[i].active != a_moi)
            {
              dsbaothuc[i].gio = g_moi;
              dsbaothuc[i].phut = p_moi;
              dsbaothuc[i].active = a_moi;
              coThayDoi = true;
            }
          }
          else
          {
            if (dsbaothuc[i].gio != 0 || dsbaothuc[i].phut != 0 || dsbaothuc[i].active != false)
            {
              dsbaothuc[i].gio = 0;
              dsbaothuc[i].phut = 0;
              dsbaothuc[i].active = false;
              coThayDoi = true;
            }
          }
        }

        Serial.println("[Firebase] Da cap nhat danh sach bao thuc vao RAM!");

        if (coThayDoi)
        {
          LuuBaoThucVaoFlash();
        }
      }
    }
  }
}

void XuLyDatNgayFirebase()
{
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDatNgay > 2000 || TimeCheckDatNgay == 0))
  {
    TimeCheckDatNgay = millis();

    if (Firebase.RTDB.getJSON(&Data, F("/DongHo/DatNgay")))
    {
      if (Data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
      {
        FirebaseJson &json = Data.jsonObject();
        FirebaseJsonData resultCapNhat, resultNgay, resultThang, resultNam;

        json.get(resultCapNhat, "/capNhat");

        // Chỉ xử lý khi App đặt cờ capNhat = true
        if (resultCapNhat.success && resultCapNhat.boolValue == true)
        {
          json.get(resultNgay, "/Ngay");
          json.get(resultThang, "/Thang");
          json.get(resultNam, "/Nam");

          // Giữ nguyên giờ:phút:giây hiện tại của RTC, chỉ thay ngày/tháng/năm
          DateTime hienTai = rtc.now();

          uint8_t ngayMoi = resultNgay.success ? resultNgay.intValue : hienTai.day();
          uint8_t thangMoi = resultThang.success ? resultThang.intValue : hienTai.month();
          uint16_t namMoi = resultNam.success ? resultNam.intValue : hienTai.year();

          DateTime ngayMoiSet(namMoi, thangMoi, ngayMoi,
                              hienTai.hour(), hienTai.minute(), hienTai.second());

          rtc.adjust(ngayMoiSet);

          Serial.printf("\n[Firebase] Da nhan lenh chinh ngay -> RTC: %02d/%02d/%04d\n",
                        ngayMoi, thangMoi, namMoi);

          // Reset cờ capNhat về false, tránh chỉnh lại liên tục
          Firebase.RTDB.setBool(&Data, F("/DongHo/DatNgay/capNhat"), false);
        }
      }
    }
  }
}

void XuLyDatGioFirebase()
{
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDatGio > 2000 || TimeCheckDatGio == 0))
  {
    TimeCheckDatGio = millis();

    if (Firebase.RTDB.getJSON(&Data, F("/DongHo/DatGio")))
    {
      if (Data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
      {

        FirebaseJson &json = Data.jsonObject();
        FirebaseJsonData resultCapNhat, resultGio, resultPhut, resultGiay;

        json.get(resultCapNhat, "/capNhat");

        // Chỉ xử lý khi App đặt cờ capNhat = true (đang có yêu cầu chỉnh giờ mới)
        if (resultCapNhat.success && resultCapNhat.boolValue == true)
        {
          json.get(resultGio, "/Gio");
          json.get(resultPhut, "/Phut");
          json.get(resultGiay, "/Giay");

          uint8_t gioMoi = resultGio.success ? resultGio.intValue : 0;
          uint8_t phutMoi = resultPhut.success ? resultPhut.intValue : 0;
          uint8_t giayMoi = resultGiay.success ? resultGiay.intValue : 0;

          DateTime hienTai = rtc.now();
          DateTime gioMoiSet(hienTai.year(), hienTai.month(), hienTai.day(), gioMoi, phutMoi, giayMoi);

          rtc.adjust(gioMoiSet);

          Serial.printf("\n[Firebase] Da nhan lenh chinh gio -> RTC: %02d:%02d:%02d\n", gioMoi, phutMoi, giayMoi);

          Firebase.RTDB.setBool(&Data, F("/DongHo/DatGio/capNhat"), false);
        }
      }
    }
  }
}

void XuLyDoSangFirebase()
{
  // Đọc độ sáng từ Firebase mỗi 5 giây
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDoSang > 5000 || TimeCheckDoSang == 0))
  {
    TimeCheckDoSang = millis();

    if (Firebase.RTDB.getInt(&Data, F("/DongHo/DoSang")))
    {
      int doSang = Data.intData();
      // Giới hạn 0–255
      doSang = constrain(doSang, 0, 255);
      dmd.setBrightness((uint8_t)doSang);
      Serial.printf("[DoSang] Cap nhat do sang LED: %d\n", doSang);
    }
  }
}

void TaskKhoiTaoNgatCore0(void *ThamSo)
{
  uint8_t cpuClock = ESP.getCpuFreqMHz();
  timer = timerBegin(0, cpuClock, true);
  timerAttachInterrupt(timer, &triggerScan, true);
  timerAlarmWrite(timer, 1000, true);
  timerAlarmEnable(timer);

  Serial.println("\n[He thong] Ngat cung da duoc ghim vao CORE 0!");
  vTaskDelete(NULL);
}

void MatrixPanel()
{
  static int8_t phutTruocDo = -1;
  static int8_t nhietDoTruocDo = -1;
  static int8_t doAmTruocDo = -1;
  static unsigned long thoiGianToggle = 0;
  static bool dauHaiChamHien = true;
  static unsigned long thoiGianDoiCamBien = 0;
  static bool hienNhietDo = true;

  bool phutThayDoi = (Phut != phutTruocDo || NhietDo != nhietDoTruocDo || DoAm != doAmTruocDo);
  bool canToggle = (millis() - thoiGianToggle >= 500);
  bool canDoiCamBien = (millis() - thoiGianDoiCamBien >= 5000);

  if (!phutThayDoi && !canToggle && !canDoiCamBien)
    return;

  if (canDoiCamBien)
  {
    thoiGianDoiCamBien = millis();
    hienNhietDo = !hienNhietDo;
    phutThayDoi = true;
  }

  if (phutThayDoi)
  {
    phutTruocDo = Phut;
    nhietDoTruocDo = NhietDo;
    doAmTruocDo = DoAm;

    dmd.clearScreen(true);
    dmd.selectFont(System5x7);

    char TextGio[3];
    char TextPhut[3];
    sprintf(TextGio, "%02d", Gio);
    sprintf(TextPhut, "%02d", Phut);

    dmd.drawString(1, 0, TextGio, 2, GRAPHICS_NORMAL);
    dmd.drawString(19, 0, TextPhut, 2, GRAPHICS_NORMAL);

    if (hienNhietDo)
    {
      char textSo[3];
      sprintf(textSo, "%02d", NhietDo);

      dmd.drawString(4, 9, textSo, 2, GRAPHICS_NORMAL);

      const uint8_t deg[2] = {0x70, 0x50};

      for (int row = 0; row < 2; row++)
        for (int col = 0; col < 2; col++)
          if (deg[row] & (0x40 >> col)) dmd.writePixel(18 + col, 9 + row, GRAPHICS_NORMAL, 1);

      dmd.drawChar(22, 9, 'C', GRAPHICS_NORMAL);
    }
    else
    {
      char textSoAm[3];
      sprintf(textSoAm, "%02d", DoAm);
      dmd.drawString(4, 9, textSoAm, 2, GRAPHICS_NORMAL);

      VeDauPhanTram(18, 9);
    }
  }

  if (canToggle)
  {
    thoiGianToggle = millis();
    dauHaiChamHien = !dauHaiChamHien;
    dmd.selectFont(System5x7);
    if (dauHaiChamHien)
      dmd.drawChar(14, 0, ':', GRAPHICS_NORMAL);
    else
      dmd.drawFilledBox(14, 0, 18, 7, GRAPHICS_INVERSE);
  }
}
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Firebase.h>
#include <RTClib.h>
#include <DHT.h>
#include <SPI.h>
#include <DMD32.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include "fonts/SystemFont5x7.h"
#include "fonts/Font3x5.h"

#define SSID_DEFAULT "Kha"
#define PASSWORD_DEFAULT "27122005"
#define API_KEY "AIzaSyCgXZegFdu02rhzI90DD1a1by0CidEaG5g"
#define DATABASE_URL "https://dong-ho-dien-tu-daktdt-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "eAx4Js7aVrc2hSwqDcmbwhLdoucxe02370UUDGjM"

#define BELL 25
#define DHTPIN 15
#define MAX_BAO_THUC 50
#define DHTTYPE DHT22

#define DISPLAYS_ACROSS 1
#define DISPLAYS_DOWN 1

#define CHU_KY_CHUONG_THU_CONG_MS 500UL
#define CHU_KY_GHI_THOI_GIAN_MS 1000UL
#define CHU_KY_DAT_NGAY_MS 700UL
#define CHU_KY_DAT_GIO_MS 800UL
#define CHU_KY_DO_SANG_MS 3000UL
#define CHU_KY_THOI_GIAN_REO_MS 8000UL
#define CHU_KY_DINH_DANG_GIO_MS 8000UL
#define CHU_KY_DOC_BAO_THUC_MS 4000UL
#define CHU_KY_DOC_DHT_MS 15000UL

FirebaseData Data;
FirebaseAuth Auth;
FirebaseConfig Config;

DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);
hw_timer_t *timer = NULL;

RTC_DS3231 rtc;
DHT dht(DHTPIN, DHTTYPE);
Preferences preferences;

volatile bool rtcOk = false;
unsigned long TimeThuLaiRTC = 0;

const char daysOfTheWeek[7][9] = {"Chu Nhat", "Thu 2", "Thu 3", "Thu 4", "Thu 5", "Thu 6", "Thu 7"};
bool tiengchuongreo = false;

#define THOI_GIAN_REO_PHA 3000UL
#define THOI_GIAN_NGHI_PHA 2000UL
unsigned long TimeBatDauChuongReo = 0;
unsigned long TimeDoiPhaChuong = 0;
bool dangPhaNghiChuong = false;
uint16_t ThoiGianReoGiay = 5;
unsigned long TimeCheckThoiGianReo = 0;
unsigned long TimeCheckDinhDangGio = 0;
bool DinhDang24Gio = true; // true = 24h, false = 12h (AM/PM)

bool chuongThuCongFirebase = false;
unsigned long TimeCheckChuongThuCong = 0;
unsigned long TimeBatDauChuongThuCong = 0;
bool chuongThuCongCanTatFirebase = false;
#define THOI_GIAN_REO_CHUONG_THU_CONG_MS 3000UL

unsigned long TimeDocDS3231 = 0;
unsigned long TimeDocDHT = 0;
unsigned long TimeCheckFirebase = 0;
unsigned long TimeCheckDatNgay = 0;
unsigned long TimeCheckDatGio = 0;
unsigned long TimeCheckDoSang = 0;

bool dhtDaOnDinh = false;
unsigned long thoiGianKhoiDongDht = 0;

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
uint8_t thuMaskBaoThuc[MAX_BAO_THUC];
volatile bool firebaseDaKhoiTao = false;
bool baoThucCanDongBoFirebase = false;
unsigned long TimeThuLaiDongBoBaoThuc = 0;
volatile bool ssidCanDongBoFirebase = false;
unsigned long TimeThuLaiSsidFirebase = 0;

bool apModeActive = false;

String wifiSSID = SSID_DEFAULT;
String wifiPassword = PASSWORD_DEFAULT;

volatile bool yeuCauDoiWifi = false;
volatile bool taskDoiWifiDangChay = false;
String pendingSsid = "";
String pendingPass = "";

volatile bool dangQuetWifi = false;
unsigned long TimeCheckQuetWifi = 0;
unsigned long TimeCheckWifi = 0;

TaskHandle_t hTaskScanLED = NULL;
volatile bool canScan = false;
SemaphoreHandle_t serialMutex = NULL;
SemaphoreHandle_t baoThucMutex = NULL;

bool taskDocBaoThucDaTao = false;
volatile bool docDHT = false;

void SafePrint(const String &s)
{
  if (serialMutex != NULL)
    xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.print(s);
  if (serialMutex != NULL)
    xSemaphoreGive(serialMutex);
}

void SafePrintln(const String &s)
{
  if (serialMutex != NULL)
    xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.println(s);
  if (serialMutex != NULL)
    xSemaphoreGive(serialMutex);
}

void IRAM_ATTR triggerScan();
void KiemTraTatChuong();
void BaoThuc(DateTime now);
void DongHo(DateTime now);
void XuLyDocBaoThucFirebase();
void XuLyDatNgayFirebase();
void XuLyDatGioFirebase();
void XuLyDoSangFirebase();
void XuLyThoiGianReoFirebase();
void XuLyDinhDangGioFirebase();
uint8_t KiemTraTrangThaiIconBaoThuc(DateTime now);
void MatrixPanel();
void KichHoatCauHinhFirebase();
void DocBaoThucTuFlash();
void LuuBaoThucVaoFlash();
bool DongBoBaoThucLenFirebase();
uint8_t DocThuMaskTuFirebase(FirebaseJson &json, const String &pathThu);
bool LaBaoThucMotLan(uint8_t index);
void TaskKhoiTaoNgatCore(void *ThamSo);
void DocWifiTuFlash();
bool GhiWifiHienTaiLenFirebase();
void XuLyDongBoSsidFirebase();
void XuLyWifiFirebase();
void TaskDoiWifi(void *param);
void XuLyQuetWifiFirebase();
void TaskQuetWifi(void *param);
String JsonEscape(const String &s);
String SanitizeSsid(const String &s);
void KhoiTaoManHinhLED();
void KiemTraLaiRTC();
void TaskKetNoiWifiBanDau(void *param);
bool BatAPMode();
void XuLyAPMode();
void XuLyChuongThuCongFirebase();
void TaskMatrixPanel(void *param);
void TaskDocBaoThucFirebase(void *param);
void TaskDocDHT(void *param);

void DocWifiTuFlash()
{
  preferences.begin("WiFiCfg", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (ssid.length() > 0)
  {
    wifiSSID = ssid;
    wifiPassword = pass;
    Serial.printf("[Flash] Da doc WiFi tu flash: %s\n", ssid.c_str());
  }
  else
  {
    wifiSSID = SSID_DEFAULT;
    wifiPassword = PASSWORD_DEFAULT;
    Serial.printf("[Flash] Dung WiFi default: %s\n", wifiSSID.c_str());
  }
}

const char *TenTrangThaiWiFi(uint8_t trangThai)
{
  switch (trangThai)
  {
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_NO_SSID_AVAIL:
    return "NO_SSID_AVAIL";
  case WL_CONNECT_FAILED:
    return "CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION_LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  case WL_IDLE_STATUS:
    return "IDLE";
  default:
    return "UNKNOWN";
  }
}

void TheoDoiTrangThaiWiFi()
{
  static int trangThaiCu = -1;
  uint8_t trangThaiMoi = WiFi.status();

  if (trangThaiCu == trangThaiMoi)
    return;

  if (trangThaiMoi == WL_CONNECTED)
  {
    Serial.printf("[WiFi] KET NOI THANH CONG: SSID='%s', IP=%s, RSSI=%d dBm\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  else if (trangThaiCu == WL_CONNECTED)
  {
    Serial.printf("[WiFi] MAT KET NOI: trang thai=%s (%u)\n",
                  TenTrangThaiWiFi(trangThaiMoi), trangThaiMoi);
  }
  else
  {
    Serial.printf("[WiFi] Trang thai: %s (%u)\n",
                  TenTrangThaiWiFi(trangThaiMoi), trangThaiMoi);
  }

  trangThaiCu = trangThaiMoi;
}

void XoaWifiFlash()
{
  preferences.begin("WiFiCfg", false);
  preferences.remove("ssid");
  preferences.remove("pass");
  preferences.end();
  Serial.println("[Flash] Da xoa WiFi trong flash");
}

bool BatAPMode()
{
  Serial.println("[AP] Bat AP Mode qua WiFiManager...");
  WiFiManager wm;
  wm.setConfigPortalBlocking(false);
  bool nguoiDungDangCauHinh = false;

  wm.setPreSaveConfigCallback([&]()
                              {
                                nguoiDungDangCauHinh = true;
                                Serial.println("[AP] Nguoi dung dang thu ket noi WiFi moi, tam dung retry WiFi cu."); });

  wm.setSaveConfigCallback([]()
                           { Serial.println("[AP] Da luu WiFi moi, dang ket noi..."); });

  wm.startConfigPortal("LED P10");
  Serial.println("[AP] Config portal dang chay, cho WiFi cu tro lai hoac nguoi dung cau hinh...");

  unsigned long thoiDiemThuLai = millis();
  unsigned long thoiDiemBatDauAP = millis();
  bool dangThuWiFiCu = false;

  while (millis() - thoiDiemBatDauAP < 180000UL)
  {
    wm.process();
    TheoDoiTrangThaiWiFi();

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.printf("[AP] WiFi cu da tro lai: %s\n", WiFi.SSID().c_str());
      wm.stopConfigPortal();
      break;
    }

    if (!nguoiDungDangCauHinh && !dangThuWiFiCu && millis() - thoiDiemThuLai >= 5000UL)
    {
      dangThuWiFiCu = true;
      thoiDiemThuLai = millis();
      Serial.printf("[AP] Thu ket noi lai WiFi da luu: %s\n", wifiSSID.c_str());
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }

    if (dangThuWiFiCu && millis() - thoiDiemThuLai >= 10000UL)
    {
      dangThuWiFiCu = false;
      WiFi.disconnect(false, false);
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[AP] Timeout hoac WiFi cu chua tro lai, thoat AP mode va thu lai...");
    wm.stopConfigPortal();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    return false;
  }

  preferences.begin("WiFiCfg", false);
  preferences.putString("ssid", WiFi.SSID());
  preferences.putString("pass", WiFi.psk());
  preferences.end();
  Serial.printf("[AP] Ket noi thanh cong: %s\n", WiFi.SSID().c_str());
  apModeActive = false;

  KichHoatCauHinhFirebase();
  GhiWifiHienTaiLenFirebase();
  return true;
}

void XuLyAPMode() {}

void TaskDoiWifi(void *param)
{
  String newSsid = pendingSsid;
  String newPass = pendingPass;

  Serial.printf("[WiFi-Task] Bat dau thu ket noi: ssid='%s'\n", newSsid.c_str());

  if (hTaskScanLED != NULL)
    vTaskSuspend(hTaskScanLED);

  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(1000));
  WiFi.begin(newSsid.c_str(), newPass.c_str());

  if (hTaskScanLED != NULL)
    vTaskResume(hTaskScanLED);

  Serial.print("[WiFi-Task] Dang ket noi ");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(300));
    if (millis() - t > 15000)
      break;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("\n[WiFi-Task] Thanh cong: %s\n", newSsid.c_str());

    FirebaseData wifiTaskData;
    wifiTaskData.setBSSLBufferSize(4096, 1024);

    unsigned long tReady = millis();
    while (!Firebase.ready() && millis() - tReady < 8000)
    {
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!Firebase.ready())
    {
      Serial.println("[Firebase] Firebase KHONG san sang sau khi doi WiFi, van thu ghi...");
    }

    bool okTrangThai = Firebase.RTDB.setString(&wifiTaskData, F("/WiFi/trangThai"), "thanhCong");
    if (!okTrangThai)
    {
      Serial.printf("[Firebase] LOI ghi /WiFi/trangThai: %s\n", wifiTaskData.errorReason().c_str());
      vTaskDelay(pdMS_TO_TICKS(500));
      okTrangThai = Firebase.RTDB.setString(&wifiTaskData, F("/WiFi/trangThai"), "thanhCong");
      if (!okTrangThai)
        Serial.printf("[Firebase] LOI lan 2 ghi /WiFi/trangThai: %s\n", wifiTaskData.errorReason().c_str());
      else
        Serial.println("[Firebase] Ghi /WiFi/trangThai thanh cong o lan thu 2!");
    }
    else
    {
      Serial.println("[Firebase] Da ghi /WiFi/trangThai = thanhCong");
    }

    preferences.begin("WiFiCfg", false);
    preferences.putString("ssid", newSsid);
    preferences.putString("pass", newPass);
    preferences.end();

    wifiSSID = newSsid;
    wifiPassword = newPass;
    WiFi.setAutoReconnect(true);
    GhiWifiHienTaiLenFirebase();
    TimeCheckWifi = millis();
    Serial.println("[WiFi-Task] Da ap dung WiFi moi, khong can restart ESP.");
  }
  else
  {
    Serial.printf("\n[WiFi-Task] That bai, ket noi lai WiFi cu: %s\n", wifiSSID.c_str());

    if (hTaskScanLED != NULL)
      vTaskSuspend(hTaskScanLED);

    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(500));
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    if (hTaskScanLED != NULL)
      vTaskResume(hTaskScanLED);

    unsigned long t2 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t2 < 10000)
      vTaskDelay(pdMS_TO_TICKS(300));

    FirebaseData wifiTaskData;
    wifiTaskData.setBSSLBufferSize(4096, 1024);
    bool okThatBai = Firebase.RTDB.setString(&wifiTaskData, F("/WiFi/trangThai"), "thatBai");
    if (!okThatBai)
      Serial.printf("[Firebase] LOI ghi /WiFi/trangThai=thatBai: %s\n", wifiTaskData.errorReason().c_str());

    WiFi.setAutoReconnect(true);
    TimeCheckWifi = millis();
  }

  yeuCauDoiWifi = false;
  taskDoiWifiDangChay = false;
  vTaskDelete(NULL);
}

void XuLyWifiFirebase()
{
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return;

  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (millis() - TimeCheckWifi < 10000 && TimeCheckWifi != 0)
    return;
  TimeCheckWifi = millis();

  FirebaseData wData;
  bool capNhat = false;
  if (Firebase.RTDB.getBool(&wData, F("/WiFi/capNhat")))
    capNhat = wData.boolData();

  if (!capNhat)
    return;

  String newSsid = "", newPass = "";
  if (Firebase.RTDB.getString(&wData, F("/WiFi/ssid")))
    newSsid = wData.stringData();
  if (Firebase.RTDB.getString(&wData, F("/WiFi/password")))
    newPass = wData.stringData();

  if (newSsid.length() == 0)
    return;

  Serial.printf("[WiFi] Nhan lenh doi WiFi: ssid='%s'\n", newSsid.c_str());

  Firebase.RTDB.setBool(&Data, F("/WiFi/capNhat"), false);
  Firebase.RTDB.deleteNode(&Data, F("/WiFi/password"));
  Firebase.RTDB.setString(&Data, F("/WiFi/trangThai"), "dangKetNoi");

  pendingSsid = newSsid;
  pendingPass = newPass;
  yeuCauDoiWifi = true;
  taskDoiWifiDangChay = true;

  if (xTaskCreatePinnedToCore(TaskDoiWifi, "TaskDoiWifi", 16384, NULL, 1, NULL, 1) != pdPASS)
  {
    yeuCauDoiWifi = false;
    taskDoiWifiDangChay = false;
    Serial.println("[WiFi-Task] LOI: khong tao duoc task doi WiFi");
  }
}

String JsonEscape(const String &s)
{
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s[i];
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out;
}

String SanitizeSsid(const String &s)
{
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++)
  {
    uint8_t c = (uint8_t)s[i];
    if (c >= 0x20 && c <= 0x7E)
      out += (char)c;
  }
  return out;
}

void TaskQuetWifi(void *param)
{
  Serial.println("[WiFi-Scan] Bat dau quet mang WiFi lan can...");

  int soMang = WiFi.scanNetworks(false, false);
  if (soMang < 0)
    soMang = 0;

  const int GIOI_HAN_MANG = 20;
  const int GIOI_HAN_SAP_XEP = 64;

  FirebaseJsonArray dsMangArr;
  int soDaThem = 0;
  int soBiLoc = 0;
  String daThem[GIOI_HAN_MANG];

  if (soMang > 0)
  {
    int soPhanTu = soMang < GIOI_HAN_SAP_XEP ? soMang : GIOI_HAN_SAP_XEP;
    int idx[GIOI_HAN_SAP_XEP];
    for (int i = 0; i < soPhanTu; i++)
      idx[i] = i;

    for (int i = 0; i < soPhanTu - 1; i++)
      for (int j = i + 1; j < soPhanTu; j++)
        if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[i]))
        {
          int tmp = idx[i];
          idx[i] = idx[j];
          idx[j] = tmp;
        }

    for (int k = 0; k < soPhanTu && soDaThem < GIOI_HAN_MANG; k++)
    {
      int i = idx[k];
      String ssidGoc = WiFi.SSID(i);
      String ssid = SanitizeSsid(ssidGoc);

      if (ssid.length() == 0)
      {
        if (ssidGoc.length() > 0)
          soBiLoc++;
        continue;
      }

      bool daTrung = false;
      for (int m = 0; m < soDaThem; m++)
      {
        if (daThem[m] == ssid)
        {
          daTrung = true;
          break;
        }
      }
      if (daTrung)
        continue;

      daThem[soDaThem++] = ssid;

      FirebaseJson objMang;
      objMang.set("ssid", ssid);
      objMang.set("rssi", WiFi.RSSI(i));
      dsMangArr.add(objMang);
    }
  }

  Serial.printf("[WiFi-Scan] Tim thay %d mang, gui %d mang (da loc trung, bo %d mang SSID hong) len Firebase\n",
                soMang, soDaThem, soBiLoc);

  WiFi.scanDelete();

  FirebaseData scanData;
  scanData.setBSSLBufferSize(4096, 1024);

  bool okList = Firebase.RTDB.setArray(&scanData, F("/WiFi/danhSachWifi"), &dsMangArr);
  if (!okList)
    Serial.printf("[Firebase] LOI ghi /WiFi/danhSachWifi: %s\n", scanData.errorReason().c_str());
  else
    Serial.println("[Firebase] Da ghi /WiFi/danhSachWifi thanh cong!");

  bool okFlag = Firebase.RTDB.setBool(&scanData, F("/WiFi/quetLuoi"), false);
  if (!okFlag)
    Serial.printf("[Firebase] LOI ghi /WiFi/quetLuoi=false: %s\n", scanData.errorReason().c_str());

  Serial.println("[WiFi-Scan] Hoan tat, da gui danh sach len Firebase.");

  dangQuetWifi = false;
  vTaskDelete(NULL);
}

void XuLyQuetWifiFirebase()
{
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return;

  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (millis() - TimeCheckQuetWifi < 2000 && TimeCheckQuetWifi != 0)
    return;
  TimeCheckQuetWifi = millis();

  FirebaseData qData;
  bool capNhat = false;
  if (Firebase.RTDB.getBool(&qData, F("/WiFi/quetLuoi")))
    capNhat = qData.boolData();

  if (!capNhat)
    return;

  Serial.println("[WiFi] Nhan lenh quet mang WiFi lan can tu app");
  dangQuetWifi = true;

  if (xTaskCreatePinnedToCore(TaskQuetWifi, "TaskQuetWifi", 12288, NULL, 1, NULL, 1) != pdPASS)
  {
    dangQuetWifi = false;
    Serial.println("[WiFi-Scan] LOI: khong tao duoc task quet WiFi");
  }
}

void VeDauPhanTram(int ox, int oy)
{
  const uint8_t pattern[5] = {
      0x19,
      0x1A,
      0x04,
      0x0B,
      0x13};

  for (int row = 0; row < 5; row++)
  {
    for (int col = 0; col < 5; col++)
    {
      if (pattern[row] & (0x10 >> col))
      {
        dmd.writePixel(ox + col, oy + row, GRAPHICS_NORMAL, 1);
      }
    }
  }
}

void VeHaiDauCong(int ox, int oy)
{
  const uint8_t pattern[7] = {
      0x02,
      0x07,
      0x02,
      0x00,
      0x02,
      0x07,
      0x02};

  for (int row = 0; row < 7; row++)
  {
    for (int col = 0; col < 3; col++)
    {
      if (pattern[row] & (0x04 >> col))
      {
        dmd.writePixel(ox + col, oy + row, GRAPHICS_NORMAL, 1);
      }
    }
  }
}

void KhoiTaoManHinhLED()
{
  SPI.begin(18, -1, 23, -1);
  delay(200);
  dmd.clearScreen(true);
  dmd.selectFont(System5x7);

  if (xTaskCreatePinnedToCore(TaskKhoiTaoNgatCore, "InitScanLED", 2048, NULL, configMAX_PRIORITIES, NULL, 1) != pdPASS)
    Serial.println("[LED] LOI: khong tao duoc task khoi tao ngat");
}

void KiemTraLaiRTC()
{
  if (rtcOk)
    return;
  if (millis() - TimeThuLaiRTC < 10000 && TimeThuLaiRTC != 0)
    return;
  TimeThuLaiRTC = millis();

  if (rtc.begin())
  {
    rtcOk = true;
    Serial.println("[RTC] Da ket noi lai DS3231 thanh cong!");
  }
}

void TaskKetNoiWifiBanDau(void *param)
{
  for (;;)
  {
    WiFi.mode(WIFI_STA);
    DocWifiTuFlash();
    Serial.printf("[WiFi] SSID: %s\n", wifiSSID.c_str());
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    WiFi.setAutoReconnect(false);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000)
      vTaskDelay(pdMS_TO_TICKS(200));

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("\n[WiFi] Da ket noi WiFi thanh cong!");
      WiFi.setAutoReconnect(true);
      KichHoatCauHinhFirebase();
      break;
    }

    Serial.println("\n[WiFi] Ket noi that bai! Bat AP Mode de cau hinh (chay nen, khong chan he thong).");
    if (BatAPMode())
      break;

    Serial.println("[WiFi] AP timeout, se thu ket noi lai sau 5 giay.");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  vTaskDelete(NULL);
}

void setup()
{
  Serial.begin(9600);
  delay(200);

  serialMutex = xSemaphoreCreateMutex();
  baoThucMutex = xSemaphoreCreateMutex();
  KhoiTaoManHinhLED();
  DocBaoThucTuFlash();

  if (!rtc.begin())
  {
    Serial.println("\n[RTC] Khong tim thay DS3231!");
    rtcOk = false;
  }
  else
  {
    rtcOk = true;
  }

  dht.begin();
  delay(200);
  thoiGianKhoiDongDht = millis();
  pinMode(BELL, OUTPUT);

  Data.setBSSLBufferSize(4096, 1024);

  if (xTaskCreatePinnedToCore(TaskKetNoiWifiBanDau, "WifiInitTask", 16384, NULL, 1, NULL, 1) != pdPASS)
    Serial.println("[WiFi] LOI: khong tao duoc task khoi tao WiFi");
  if (xTaskCreatePinnedToCore(TaskMatrixPanel, "TaskMatrixPanel", 4096, NULL, 2, NULL, 1) != pdPASS)
    Serial.println("[LED] LOI: khong tao duoc task hien thi");
  if (xTaskCreatePinnedToCore(TaskDocDHT, "TaskDocDHT", 16384, NULL, 1, NULL, 0) != pdPASS)
    Serial.println("[DHT] LOI: khong tao duoc task doc DHT");
}

void loop()
{
  TheoDoiTrangThaiWiFi();
  XuLyDongBoSsidFirebase();
  KiemTraLaiRTC();

  if (WiFi.status() == WL_CONNECTED && !firebaseDaKhoiTao && !yeuCauDoiWifi && !dangQuetWifi)
  {
    KichHoatCauHinhFirebase();
  }

  DateTime now = rtcOk ? rtc.now() : DateTime((uint32_t)0);
  BaoThuc(now);
  KiemTraTatChuong();
  DongHo(now);

  static uint8_t buocFirebase = 0;
  switch (buocFirebase)
  {
  case 0:
    XuLyDocBaoThucFirebase();
    buocFirebase = 1;
    break;
  case 1:
    XuLyDatNgayFirebase();
    buocFirebase = 2;
    break;
  case 2:
    XuLyDatGioFirebase();
    buocFirebase = 3;
    break;
  case 3:
    XuLyDoSangFirebase();
    buocFirebase = 4;
    break;
  case 4:
    XuLyThoiGianReoFirebase();
    buocFirebase = 5;
    break;
  case 5:
    XuLyChuongThuCongFirebase();
    buocFirebase = 6;
    break;
  case 6:
    XuLyWifiFirebase();
    buocFirebase = 7;
    break;
  case 7:
    XuLyQuetWifiFirebase();
    buocFirebase = 8;
    break;
  case 8:
    XuLyDinhDangGioFirebase();
    buocFirebase = 0;
    break;
  default:
    buocFirebase = 0;
    break;
  }

  XuLyAPMode();
  vTaskDelay(1);
}

void IRAM_ATTR triggerScan()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (hTaskScanLED)
    vTaskNotifyGiveFromISR(hTaskScanLED, &xHigherPriorityTaskWoken);

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void TaskScanLED(void *param)
{
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    dmd.scanDisplayBySPI();
  }
}

void TaskMatrixPanel(void *param)
{
  for (;;)
  {
    MatrixPanel();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

#define NHIET_DO_OFFSET 1.5f
#define DO_AM_OFFSET 5.0f
void TaskDocDHT(void *param)
{
  FirebaseData dhtData;
  dhtData.setBSSLBufferSize(2048, 1024);

  static bool canGuiDHTLenFirebase = false;
  static float tempChoGui = 0;
  static float humiChoGui = 0;

  for (;;)
  {
    if (!dhtDaOnDinh && (millis() - thoiGianKhoiDongDht > 180000UL))
    {
      dhtDaOnDinh = true;
      Serial.println("[DHT] Da on dinh");
    }

    if (dhtDaOnDinh && (millis() - TimeDocDHT > CHU_KY_DOC_DHT_MS || TimeDocDHT == 0))
    {
      if (yeuCauDoiWifi || dangQuetWifi || taskDoiWifiDangChay)
      {
        SafePrintln("[DHT] Bo qua lan doc, RF mode dang ban, se thu lai sau");
      }
      else
      {
        TimeDocDHT = millis();
        docDHT = true;

        const uint8_t SO_LAN_THU_LAI_TOI_DA = 2;
        float temp = NAN;
        float humi = NAN;

        for (uint8_t lanThu = 0; lanThu <= SO_LAN_THU_LAI_TOI_DA; lanThu++)
        {
          temp = dht.readTemperature() - NHIET_DO_OFFSET;
          humi = dht.readHumidity() + DO_AM_OFFSET;

          if (!isnan(temp) && !isnan(humi))
            break;

          if (lanThu < SO_LAN_THU_LAI_TOI_DA)
          {
            SafePrintln("[DHT] Doc loi (NaN), thu lai lan " + String(lanThu + 1) + "...");
            vTaskDelay(pdMS_TO_TICKS(2200));
          }
        }

        docDHT = false;

        if (isnan(temp) || isnan(humi))
        {
          Serial.println("[DHT] Doc that bai (NaN) sau khi da thu lai");
        }
        else
        {
          NhietDo = (int8_t)temp;
          DoAm = (uint8_t)humi;
          tempChoGui = temp;
          humiChoGui = humi;
          canGuiDHTLenFirebase = true;
        }
      }
    }

    if (canGuiDHTLenFirebase && !yeuCauDoiWifi && !dangQuetWifi && firebaseDaKhoiTao)
    {
      if (Firebase.ready())
      {
        FirebaseJson json;
        json.set("NhietDo", tempChoGui);
        json.set("DoAm", humiChoGui);

        if (Firebase.RTDB.updateNode(&dhtData, F("/CamBien"), &json))
        {
          canGuiDHTLenFirebase = false;
        }
        else
        {
          SafePrintln("[DHT] Gui Firebase that bai: " + dhtData.errorReason() + ", se thu lai");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
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

  if (!taskDocBaoThucDaTao)
  {
    if (xTaskCreatePinnedToCore(TaskDocBaoThucFirebase, "TaskDocBaoThuc", 16384, NULL, 1, NULL, 1) == pdPASS)
      taskDocBaoThucDaTao = true;
    else
      Serial.println("[Firebase] LOI: khong tao duoc task doc bao thuc");
  }
}

bool GhiWifiHienTaiLenFirebase()
{
  if (!firebaseDaKhoiTao || !Firebase.ready())
  {
    ssidCanDongBoFirebase = true;
    return false;
  }
  String ssidHienTai = WiFi.SSID();
  if (ssidHienTai.length() == 0)
  {
    ssidCanDongBoFirebase = true;
    return false;
  }

  FirebaseData ssidData;
  ssidData.setBSSLBufferSize(4096, 1024);
  bool daGhi = Firebase.RTDB.setString(&ssidData, F("/WiFi/ssidHienTai"), ssidHienTai);
  if (!daGhi)
  {
    ssidCanDongBoFirebase = true;
    Serial.printf("[Firebase] LOI ghi /WiFi/ssidHienTai: %s, se thu lai\n", ssidData.errorReason().c_str());
  }
  else
  {
    ssidCanDongBoFirebase = false;
    Serial.printf("[Firebase] Da cap nhat /WiFi/ssidHienTai = %s\n", ssidHienTai.c_str());
  }
  return daGhi;
}

void XuLyDongBoSsidFirebase()
{
  if (!ssidCanDongBoFirebase)
    return;
  if (millis() - TimeThuLaiSsidFirebase < 3000 && TimeThuLaiSsidFirebase != 0)
    return;

  TimeThuLaiSsidFirebase = millis();
  GhiWifiHienTaiLenFirebase();
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
      thuMaskBaoThuc[i] = 0xFF;
    }
  }
  for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
  {
    thuMaskBaoThuc[i] = 0xFF;
  }
}

void LuuBaoThucVaoFlash()
{
  if (timer != NULL)
    timerAlarmDisable(timer);
  preferences.begin("BaoThucNS", false);
  size_t kieuSize = sizeof(dsbaothuc);
  preferences.putBytes("dsBT", dsbaothuc, kieuSize);
  preferences.end();
  if (timer != NULL)
    timerAlarmEnable(timer);
}

uint8_t DocThuMaskTuFirebase(FirebaseJson &json, const String &pathThu)
{
  FirebaseJsonData resultThu;
  json.get(resultThu, pathThu);
  if (!resultThu.success || resultThu.type != "array")
    return 0xFF;

  FirebaseJsonArray thuArr;
  resultThu.get<FirebaseJsonArray>(thuArr);
  uint8_t mask = 0;
  FirebaseJsonData resultItem;
  for (size_t i = 0; i < thuArr.size(); i++)
  {
    thuArr.get(resultItem, i);
    if (!resultItem.success)
      continue;
    int day = resultItem.to<int>();
    if (day >= 0 && day < 7)
      mask |= (uint8_t)(1U << day);
  }
  return mask;
}

bool DongBoBaoThucLenFirebase()
{
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return false;
  FirebaseJson json;
  for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
  {
    FirebaseJson itemJson;
    itemJson.set("gio", dsbaothuc[i].gio);
    itemJson.set("phut", dsbaothuc[i].phut);
    itemJson.set("active", dsbaothuc[i].active);
    FirebaseJsonArray thuArr;
    if (thuMaskBaoThuc[i] != 0xFF)
    {
      for (uint8_t day = 0; day < 7; day++)
      {
        if (thuMaskBaoThuc[i] & (uint8_t)(1U << day))
          thuArr.add(day);
      }
    }
    itemJson.set("thu", thuArr);
    json.set("BaoThuc" + String(i + 1), itemJson);
  }
  return Firebase.RTDB.setJSON(&Data, F("/DongHo/dsBaoThuc"), &json);
}

bool TatBaoThucMotLan(int index)
{
  if (index < 0 || index >= MAX_BAO_THUC)
    return false;
  if (yeuCauDoiWifi || dangQuetWifi)
    return false;
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return false;
  String basePath = "/DongHo/dsBaoThuc/BaoThuc" + String(index + 1);
  return Firebase.RTDB.setBool(&Data, basePath + "/active", false);
}

bool LaBaoThucMotLan(uint8_t index)
{
  if (index >= MAX_BAO_THUC)
    return false;
  return thuMaskBaoThuc[index] == 0;
}

void DongHo(DateTime now)
{
  if (!rtcOk)
    return;
  if (yeuCauDoiWifi || dangQuetWifi)
  {
    Gio = now.hour();
    Phut = now.minute();
    Giay = now.second();
    return;
  }
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeDocDS3231 >= CHU_KY_GHI_THOI_GIAN_MS || TimeDocDS3231 == 0))
  {
    TimeDocDS3231 = millis();
    static bool daGhiWifi = false;

    if (!daGhiWifi)
      daGhiWifi = GhiWifiHienTaiLenFirebase();

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
  if (!rtcOk)
    return;
  if (tiengchuongreo || now.minute() == phutcuoicung)
  {
    if (now.minute() != phutcuoicung && phutcuoicung != -1)
      phutcuoicung = -1;
    return;
  }
  if (xSemaphoreTake(baoThucMutex, pdMS_TO_TICKS(50)) != pdTRUE)
    return;

  for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
  {
    bool dungNgay = (thuMaskBaoThuc[i] == 0xFF) || (thuMaskBaoThuc[i] == 0) || (thuMaskBaoThuc[i] & (uint8_t)(1U << now.dayOfTheWeek()));
    if (dsbaothuc[i].active && dungNgay && now.hour() == dsbaothuc[i].gio && now.minute() == dsbaothuc[i].phut)
    {
      tiengchuongreo = true;
      TimeBatDauChuongReo = millis();
      TimeDoiPhaChuong = millis();
      dangPhaNghiChuong = false;
      phutcuoicung = now.minute();
      digitalWrite(BELL, HIGH);
      if (LaBaoThucMotLan(i))
      {
        dsbaothuc[i].active = false;
        xSemaphoreGive(baoThucMutex);
        LuuBaoThucVaoFlash();
        if (!TatBaoThucMotLan(i))
          baoThucCanDongBoFirebase = true;
        return;
      }
      break;
    }
  }
  xSemaphoreGive(baoThucMutex);
}

void KiemTraTatChuong()
{
  if (chuongThuCongFirebase)
  {
    if (millis() - TimeBatDauChuongThuCong >= THOI_GIAN_REO_CHUONG_THU_CONG_MS)
    {
      chuongThuCongFirebase = false;
      chuongThuCongCanTatFirebase = true;
      digitalWrite(BELL, LOW);
      return;
    }
    digitalWrite(BELL, HIGH);
    return;
  }
  if (tiengchuongreo)
  {
    unsigned long tongDaTroi = millis() - TimeBatDauChuongReo;
    if (tongDaTroi >= (unsigned long)ThoiGianReoGiay * 1000UL)
    {
      tiengchuongreo = false;
      digitalWrite(BELL, LOW);
      return;
    }
    if (dangPhaNghiChuong)
    {
      digitalWrite(BELL, LOW);
      if (millis() - TimeDoiPhaChuong >= THOI_GIAN_NGHI_PHA)
      {
        dangPhaNghiChuong = false;
        TimeDoiPhaChuong = millis();
      }
    }
    else
    {
      digitalWrite(BELL, HIGH);
      if (millis() - TimeDoiPhaChuong >= THOI_GIAN_REO_PHA)
      {
        dangPhaNghiChuong = true;
        TimeDoiPhaChuong = millis();
        digitalWrite(BELL, LOW);
      }
    }
  }
  else
  {
    digitalWrite(BELL, LOW);
  }
}

void XuLyDocBaoThucFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (!baoThucCanDongBoFirebase)
    return;
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return;
  if (millis() - TimeThuLaiDongBoBaoThuc < 3000 && TimeThuLaiDongBoBaoThuc != 0)
    return;
  TimeThuLaiDongBoBaoThuc = millis();

  bool conLoiSot = false;
  if (xSemaphoreTake(baoThucMutex, pdMS_TO_TICKS(200)) == pdTRUE)
  {
    for (int i = 0; i < MAX_BAO_THUC; i++)
    {
      if (!dsbaothuc[i].active && LaBaoThucMotLan(i))
      {
        if (!TatBaoThucMotLan(i))
          conLoiSot = true;
      }
    }
    xSemaphoreGive(baoThucMutex);
  }
  else
  {
    conLoiSot = true;
  }

  if (!conLoiSot)
    baoThucCanDongBoFirebase = false;
}

void TaskDocBaoThucFirebase(void *param)
{
  FirebaseData baoThucData;
  baoThucData.setBSSLBufferSize(4096, 1024);

  const TickType_t KHOANG_NGHI = pdMS_TO_TICKS(CHU_KY_DOC_BAO_THUC_MS);

  for (;;)
  {
    if (!yeuCauDoiWifi && !dangQuetWifi && firebaseDaKhoiTao && Firebase.ready())
    {
      if (Firebase.RTDB.getJSON(&baoThucData, F("/DongHo/dsBaoThuc")))
      {
        if (baoThucData.dataTypeEnum() == fb_esp_rtdb_data_type_json)
        {
          FirebaseJson &json = baoThucData.jsonObject();
          FirebaseJsonData resultBaoThuc, resultGio, resultPhut, resultActive;
          String pathBaoThuc, pathGio, pathPhut, pathActive;
          bool coThayDoi = false;

          if (xSemaphoreTake(baoThucMutex, pdMS_TO_TICKS(200)) == pdTRUE)
          {
            for (int i = 0; i < MAX_BAO_THUC; i++)
            {
              pathBaoThuc = "/BaoThuc" + String(i + 1);
              json.get(resultBaoThuc, pathBaoThuc);
              if (resultBaoThuc.success && resultBaoThuc.type == "object")
              {
                pathGio = pathBaoThuc + "/gio";
                pathPhut = pathBaoThuc + "/phut";
                pathActive = pathBaoThuc + "/active";
                String pathThu = pathBaoThuc + "/thu";
                json.get(resultGio, pathGio);
                json.get(resultPhut, pathPhut);
                json.get(resultActive, pathActive);
                uint8_t g_moi = resultGio.success ? resultGio.intValue : 0;
                uint8_t p_moi = resultPhut.success ? resultPhut.intValue : 0;
                bool a_moi = resultActive.success ? resultActive.boolValue : false;
                uint8_t thuMoi = DocThuMaskTuFirebase(json, pathThu);
                if (dsbaothuc[i].gio != g_moi || dsbaothuc[i].phut != p_moi || dsbaothuc[i].active != a_moi || thuMaskBaoThuc[i] != thuMoi)
                {
                  dsbaothuc[i].gio = g_moi;
                  dsbaothuc[i].phut = p_moi;
                  dsbaothuc[i].active = a_moi;
                  thuMaskBaoThuc[i] = thuMoi;
                  coThayDoi = true;
                }
              }
              else
              {
                if (dsbaothuc[i].gio != 0 || dsbaothuc[i].phut != 0 || dsbaothuc[i].active != false || thuMaskBaoThuc[i] != 0xFF)
                {
                  dsbaothuc[i].gio = 0;
                  dsbaothuc[i].phut = 0;
                  dsbaothuc[i].active = false;
                  thuMaskBaoThuc[i] = 0xFF;
                  coThayDoi = true;
                }
              }
            }
            xSemaphoreGive(baoThucMutex);
          }

          if (coThayDoi)
          {
            LuuBaoThucVaoFlash();
          }
        }
      }
    }
    vTaskDelay(KHOANG_NGHI);
  }
}

void XuLyDatNgayFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi || !rtcOk)
    return;
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDatNgay > CHU_KY_DAT_NGAY_MS || TimeCheckDatNgay == 0))
  {
    TimeCheckDatNgay = millis();
    if (Firebase.RTDB.getJSON(&Data, F("/DongHo/DatNgay")))
    {
      if (Data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
      {
        FirebaseJson &json = Data.jsonObject();
        FirebaseJsonData resultCapNhat, resultNgay, resultThang, resultNam;
        json.get(resultCapNhat, "/capNhat");
        if (resultCapNhat.success && resultCapNhat.boolValue == true)
        {
          json.get(resultNgay, "/Ngay");
          json.get(resultThang, "/Thang");
          json.get(resultNam, "/Nam");
          DateTime hienTai = rtc.now();
          uint8_t ngayMoi = resultNgay.success ? resultNgay.intValue : hienTai.day();
          uint8_t thangMoi = resultThang.success ? resultThang.intValue : hienTai.month();
          uint16_t namMoi = resultNam.success ? resultNam.intValue : hienTai.year();
          DateTime ngayMoiSet(namMoi, thangMoi, ngayMoi, hienTai.hour(), hienTai.minute(), hienTai.second());
          rtc.adjust(ngayMoiSet);
          Firebase.RTDB.setBool(&Data, F("/DongHo/DatNgay/capNhat"), false);
        }
      }
    }
    else
    {
      SafePrintln("[DatNgay] Doc that bai: " + Data.errorReason());
    }
  }
}

void XuLyDatGioFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi || !rtcOk)
    return;
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDatGio > CHU_KY_DAT_GIO_MS || TimeCheckDatGio == 0))
  {
    TimeCheckDatGio = millis();
    if (Firebase.RTDB.getJSON(&Data, F("/DongHo/DatGio")))
    {
      if (Data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
      {
        FirebaseJson &json = Data.jsonObject();
        FirebaseJsonData resultCapNhat, resultGio, resultPhut, resultGiay;
        json.get(resultCapNhat, "/capNhat");
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
          Firebase.RTDB.setBool(&Data, F("/DongHo/DatGio/capNhat"), false);
        }
      }
    }
    else
    {
      SafePrintln("[DatGio] Doc that bai: " + Data.errorReason());
    }
  }
}

void XuLyDoSangFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDoSang > CHU_KY_DO_SANG_MS || TimeCheckDoSang == 0))
  {
    TimeCheckDoSang = millis();
    if (Firebase.RTDB.getInt(&Data, F("/DongHo/DoSang")))
    {
      int doSang = Data.intData();
      doSang = constrain(doSang, 0, 255);
      static int doSangTruocDo = -1;
      if (doSang != doSangTruocDo)
      {
        doSangTruocDo = doSang;
        dmd.setBrightness((uint8_t)doSang);
      }
    }
    else
    {
      SafePrintln("[DoSang] Doc that bai: " + Data.errorReason());
    }
  }
}

void XuLyThoiGianReoFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckThoiGianReo > CHU_KY_THOI_GIAN_REO_MS || TimeCheckThoiGianReo == 0))
  {
    TimeCheckThoiGianReo = millis();
    if (Firebase.RTDB.getInt(&Data, F("/DongHo/ThoiGianReo")))
    {
      int giay = Data.intData();
      giay = constrain(giay, 1, 30);
      static int giayTruocDo = -1;
      if (giay != giayTruocDo)
      {
        giayTruocDo = giay;
        ThoiGianReoGiay = (uint16_t)giay;
      }
    }
    else
    {
      SafePrintln("[ThoiGianReo] Doc that bai: " + Data.errorReason());
    }
  }
}

void XuLyDinhDangGioFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDinhDangGio > CHU_KY_DINH_DANG_GIO_MS || TimeCheckDinhDangGio == 0))
  {
    TimeCheckDinhDangGio = millis();
    if (Firebase.RTDB.getBool(&Data, F("/DongHo/DinhDang24Gio")))
    {
      bool la24Gio = Data.boolData();
      static int8_t truocDo = -1;
      if ((int8_t)la24Gio != truocDo)
      {
        truocDo = (int8_t)la24Gio;
        DinhDang24Gio = la24Gio;
      }
    }
    else
    {
      SafePrintln("[DinhDangGio] Doc that bai: " + Data.errorReason());
    }
  }
}

void TaskKhoiTaoNgatCore(void *ThamSo)
{
  xTaskCreatePinnedToCore(TaskScanLED, "TaskScanLED", 4096, NULL, 5, &hTaskScanLED, 1);
  uint8_t cpuClock = ESP.getCpuFreqMHz();
  timer = timerBegin(0, cpuClock, true);
  timerAttachInterrupt(timer, &triggerScan, true);

  timerAlarmWrite(timer, 1200, true);
  timerAlarmEnable(timer);
  vTaskDelete(NULL);
}

void XuLyChuongThuCongFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return;

  if (chuongThuCongCanTatFirebase)
  {
    if (millis() - TimeCheckChuongThuCong < CHU_KY_CHUONG_THU_CONG_MS && TimeCheckChuongThuCong != 0)
      return;
    TimeCheckChuongThuCong = millis();
    if (Firebase.RTDB.setBool(&Data, F("/DongHo/ChuongThuCong"), false))
    {
      chuongThuCongCanTatFirebase = false;
    }
    else
    {
      SafePrintln("[ChuongThuCong] Tat that bai: " + Data.errorReason());
    }
    return;
  }

  if (millis() - TimeCheckChuongThuCong >= CHU_KY_CHUONG_THU_CONG_MS || TimeCheckChuongThuCong == 0)
  {
    TimeCheckChuongThuCong = millis();
    if (Firebase.RTDB.getBool(&Data, F("/DongHo/ChuongThuCong")))
    {
      bool trangThaiMoi = Data.boolData();
      if (trangThaiMoi != chuongThuCongFirebase)
      {
        chuongThuCongFirebase = trangThaiMoi;
        if (chuongThuCongFirebase)
        {
          TimeBatDauChuongThuCong = millis();
          digitalWrite(BELL, HIGH);
        }
        else
        {
          digitalWrite(BELL, LOW);
        }
      }
    }
    else
    {
      SafePrintln("[ChuongThuCong] Doc that bai: " + Data.errorReason());
    }
  }
}

uint8_t KiemTraTrangThaiIconBaoThuc(DateTime now)
{
  if (!rtcOk)
    return 0;
  static uint8_t trangThaiGanNhat = 0;
  bool sapReoTrong10Phut = false;
  uint32_t phutHienTai = now.hour() * 60 + now.minute();

  if (xSemaphoreTake(baoThucMutex, pdMS_TO_TICKS(20)) != pdTRUE)
    return trangThaiGanNhat;

  for (int i = 0; i < MAX_BAO_THUC; i++)
  {
    if (dsbaothuc[i].active)
    {
      uint32_t phutBaoThuc = dsbaothuc[i].gio * 60 + dsbaothuc[i].phut;
      int32_t khoangCachPhut = (int32_t)phutBaoThuc - (int32_t)phutHienTai;
      if (khoangCachPhut < 0)
      {
        khoangCachPhut += 1440;
      }
      if (khoangCachPhut > 0 && khoangCachPhut <= 10)
      {
        sapReoTrong10Phut = true;
        break;
      }
    }
  }
  xSemaphoreGive(baoThucMutex);

  trangThaiGanNhat = sapReoTrong10Phut ? 1 : 0;
  return trangThaiGanNhat;
}

void veHaiCham(int x, int y, byte style)
{
  for (int dx = 0; dx < 2; dx++)
  {
    dmd.writePixel(x + dx, y + 1, style, 1);
    dmd.writePixel(x + dx, y + 2, style, 1);
    dmd.writePixel(x + dx, y + 4, style, 1);
    dmd.writePixel(x + dx, y + 5, style, 1);
  }
}

void MatrixPanel()
{
  DateTime now = DateTime(2020, 1, 1, Gio, Phut, Giay);
  static int8_t phutTruocDo = -1;
  static int8_t nhietDoTruocDo = -1;
  static int8_t doAmTruocDo = -1;
  static uint8_t trangThaiBaoThucTruocDo = 99;
  uint8_t trangThaiBaoThuc = KiemTraTrangThaiIconBaoThuc(now);
  const uint8_t iconDongHo_X = 27;
  const uint8_t iconDongHo_Y = 2;
  static unsigned long thoiGianToggle = 0;
  static bool dauHaiChamHien = true;
  static unsigned long thoiGianDoiCamBien = 0;
  static bool hienNhietDo = true;

  bool phutThayDoi = (Phut != phutTruocDo || NhietDo != nhietDoTruocDo || DoAm != doAmTruocDo);
  bool trangThaiBaoThucThayDoi = (trangThaiBaoThuc != trangThaiBaoThucTruocDo);
  bool canToggle = (millis() - thoiGianToggle >= 500);
  bool canDoiCamBien = (millis() - thoiGianDoiCamBien >= 30000);

  if (canToggle)
  {
    thoiGianToggle = millis();
    dauHaiChamHien = !dauHaiChamHien;
    dmd.selectFont(System5x7);
    if (dauHaiChamHien)
    {
      if (trangThaiBaoThuc == 1)
      {
        VeHaiDauCong(15, 0);
      }
      else
      {
        veHaiCham(15, 0, GRAPHICS_NORMAL);
      }
    }
    else
    {
      dmd.drawFilledBox(15, 0, 17, 7, GRAPHICS_INVERSE);
    }
    if (!dhtDaOnDinh)
    {
      if (dauHaiChamHien)
      {
        dmd.drawLine(9, 12, 14, 12, GRAPHICS_NORMAL);
        dmd.drawLine(17, 12, 22, 12, GRAPHICS_NORMAL);
      }
      else
      {
        dmd.drawFilledBox(9, 12, 14, 12, GRAPHICS_INVERSE);
        dmd.drawFilledBox(17, 12, 22, 12, GRAPHICS_INVERSE);
      }
    }
  }

  if (canDoiCamBien)
  {
    thoiGianDoiCamBien = millis();
    hienNhietDo = !hienNhietDo;
    phutThayDoi = true;
  }

  if (phutThayDoi || trangThaiBaoThucThayDoi)
  {
    phutTruocDo = Phut;
    nhietDoTruocDo = NhietDo;
    doAmTruocDo = DoAm;
    trangThaiBaoThucTruocDo = trangThaiBaoThuc;
    dmd.drawFilledBox(0, 0, 31, 7, GRAPHICS_INVERSE);
    dmd.drawFilledBox(0, 9, 31, 15, GRAPHICS_INVERSE);

    dmd.selectFont(System5x7);
    char TextGio[5];
    char TextPhut[5];
    uint8_t gioHienThi = Gio;
    if (!DinhDang24Gio)
    {
      gioHienThi = Gio % 12;
      if (gioHienThi == 0)
        gioHienThi = 12;
    }
    sprintf(TextGio, "%02d", gioHienThi);
    sprintf(TextPhut, "%02d", Phut);
    dmd.drawString(3, 0, TextGio, 2, GRAPHICS_NORMAL);
    dmd.drawString(19, 0, TextPhut, 2, GRAPHICS_NORMAL);

    if (dauHaiChamHien)
    {
      if (trangThaiBaoThuc == 1)
        VeHaiDauCong(15, 0);
      else
        veHaiCham(15, 0, GRAPHICS_NORMAL);
    }

    if (dhtDaOnDinh)
    {
      if (hienNhietDo)
      {
        dmd.drawString(1, 9, "T", 1, GRAPHICS_NORMAL);
        veHaiCham(7, 9, GRAPHICS_NORMAL);
        char textSo[5];
        sprintf(textSo, "%02d", NhietDo);
        dmd.drawString(11, 9, textSo, 2, GRAPHICS_NORMAL);
        const uint8_t deg[2] = {0x60, 0x60};
        for (int row = 0; row < 2; row++)
          for (int col = 0; col < 2; col++)
            if (deg[row] & (0x40 >> col))
              dmd.writePixel(23 + col, 9 + row, GRAPHICS_NORMAL, 1);
        dmd.drawChar(26, 9, 'C', GRAPHICS_NORMAL);
      }
      else
      {
        dmd.drawString(2, 9, "H", 1, GRAPHICS_NORMAL);
        veHaiCham(8, 9, GRAPHICS_NORMAL);
        char textDoAm[5];
        sprintf(textDoAm, "%02d", DoAm);
        dmd.drawString(12, 9, textDoAm, 2, GRAPHICS_NORMAL);
        VeDauPhanTram(25, 10);
      }
    }
    else
    {
      if (dauHaiChamHien)
      {
        dmd.drawLine(9, 12, 14, 12, GRAPHICS_NORMAL);
        dmd.drawLine(17, 12, 22, 12, GRAPHICS_NORMAL);
      }
    }
  }
}
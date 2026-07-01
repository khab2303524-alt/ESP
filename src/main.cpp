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
uint8_t thuMaskBaoThuc[MAX_BAO_THUC];
bool firebaseDaKhoiTao = false;
bool baoThucCanDongBoFirebase = false;

bool apModeActive = false;

String wifiSSID = SSID_DEFAULT;
String wifiPassword = PASSWORD_DEFAULT;

volatile bool yeuCauDoiWifi = false;
String pendingSsid = "";
String pendingPass = "";

volatile bool dangQuetWifi = false;
unsigned long TimeCheckQuetWifi = 0;

TaskHandle_t hTaskScanLED = NULL;

volatile bool canScan = false;

SemaphoreHandle_t serialMutex = NULL;

// In Serial an toàn khi nhiều Task cùng in cùng lúc
void SafePrint(const String &s)
{
  if (serialMutex != NULL)
    xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.print(s);
  if (serialMutex != NULL)
    xSemaphoreGive(serialMutex);
}

// In Serial (xuống dòng) an toàn khi nhiều Task cùng in cùng lúc
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
void CamBienDHT();
void XuLyDocBaoThucFirebase();
void XuLyDatNgayFirebase();
void XuLyDatGioFirebase();
void XuLyDoSangFirebase();
void XuLyThoiGianReoFirebase();
void MatrixPanel();
void KichHoatCauHinhFirebase();
void DocBaoThucTuFlash();
void LuuBaoThucVaoFlash();
bool DongBoBaoThucLenFirebase();
uint8_t DocThuMaskTuFirebase(FirebaseJson &json, const String &pathThu);
bool LaBaoThucMotLan(uint8_t index);
void TaskKhoiTaoNgatCore0(void *ThamSo);
void DocWifiTuFlash();
void GhiWifiHienTaiLenFirebase();
void XuLyWifiFirebase();
void GhiWifiHienTaiLenFirebase();
void TaskDoiWifi(void *param);
void XuLyQuetWifiFirebase();
void TaskQuetWifi(void *param);
String JsonEscape(const String &s);
String SanitizeSsid(const String &s);
void KhoiTaoManHinhLED();
void KiemTraLaiRTC();
void TaskKetNoiWifiBanDau(void *param);
void BatAPMode();

// Đọc SSID/mật khẩu WiFi đã lưu trong flash
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

// Xoá thông tin WiFi đã lưu trong flash
void XoaWifiFlash()
{
  preferences.begin("WiFiCfg", false);
  preferences.remove("ssid");
  preferences.remove("pass");
  preferences.end();
  Serial.println("[Flash] Da xoa WiFi trong flash");
}

// Bật chế độ AP để người dùng cấu hình WiFi qua điện thoại
void BatAPMode()
{
  Serial.println("[AP] Bat AP Mode qua WiFiManager...");
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  wm.setSaveConfigCallback([]()
                           { Serial.println("[AP] Da luu WiFi moi, dang restart..."); });

  if (!wm.startConfigPortal("ESP32-Clock 192.168.4.1"))
  {
    Serial.println("[AP] Timeout hoac that bai, restart...");
    ESP.restart();
  }

  preferences.begin("WiFiCfg", false);
  preferences.putString("ssid", WiFi.SSID());
  preferences.putString("pass", WiFi.psk());
  preferences.end();
  Serial.printf("[AP] Ket noi thanh cong: %s\n", WiFi.SSID().c_str());
  apModeActive = false;

  KichHoatCauHinhFirebase();
}

// WiFiManager tự xử lý AP Mode, không cần làm gì thêm
void XuLyAPMode() {}

unsigned long TimeCheckWifi = 0;

// Task đổi sang WiFi mới theo yêu cầu từ app/Firebase
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

    bool okSsid = Firebase.RTDB.setString(&wifiTaskData, F("/WiFi/ssidHienTai"), newSsid);
    if (!okSsid)
      Serial.printf("[Firebase] LOI ghi /WiFi/ssidHienTai: %s\n", wifiTaskData.errorReason().c_str());

    preferences.begin("WiFiCfg", false);
    preferences.putString("ssid", newSsid);
    preferences.putString("pass", newPass);
    preferences.end();

    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP.restart();
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
  vTaskDelete(NULL);
}

// Kiểm tra Firebase xem có yêu cầu đổi WiFi không
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
  Firebase.RTDB.setString(&Data, F("/WiFi/trangThai"), "dangKetNoi");

  pendingSsid = newSsid;
  pendingPass = newPass;
  yeuCauDoiWifi = true;

  xTaskCreatePinnedToCore(
      TaskDoiWifi,
      "TaskDoiWifi",
      8192,
      NULL,
      1,
      NULL,
      1);
}

// Escape ký tự đặc biệt để đưa vào chuỗi JSON
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

// Làm sạch tên SSID trước khi gửi lên Firebase
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

// Task quét danh sách WiFi xung quanh
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

// Kiểm tra Firebase xem có yêu cầu quét WiFi không
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

  xTaskCreatePinnedToCore(
      TaskQuetWifi,
      "TaskQuetWifi",
      8192,
      NULL,
      1,
      NULL,
      1);
}

// Vẽ ký hiệu %
void VeDauPhanTram(int ox, int oy)
{
  const uint8_t pattern[7] = {
      0x61,
      0x62,
      0x04,
      0x08,
      0x10,
      0x23,
      0x43,
  };
  for (int row = 0; row < 7; row++)
  {
    for (int col = 0; col < 7; col++)
    {
      if (pattern[row] & (0x40 >> col))
      {
        dmd.writePixel(ox + col, oy + row, GRAPHICS_NORMAL, 1);
      }
    }
  }
}

// Khởi tạo màn hình LED matrix
void KhoiTaoManHinhLED()
{
  SPI.begin(18, -1, 23, -1);
  delay(50);
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
}

// Thử kết nối lại RTC nếu lần đầu thất bại
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

// Task kết nối WiFi lúc khởi động, tự bật AP Mode nếu thất bại
void TaskKetNoiWifiBanDau(void *param)
{
  WiFi.mode(WIFI_STA);
  DocWifiTuFlash();
  Serial.printf("[WiFi] SSID: %s\n", wifiSSID.c_str());
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  WiFi.setAutoReconnect(false);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000)
  {
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n[WiFi] Da ket noi WiFi thanh cong!");
    WiFi.setAutoReconnect(true);
    KichHoatCauHinhFirebase();
  }
  else
  {
    Serial.println("\n[WiFi] Ket noi that bai! Bat AP Mode de cau hinh (chay nen, khong chan he thong).");
    BatAPMode();
  }

  vTaskDelete(NULL);
}

// Khởi tạo hệ thống
void setup()
{
  Serial.begin(9600);
  delay(200);

  serialMutex = xSemaphoreCreateMutex();

  KhoiTaoManHinhLED();

  DocBaoThucTuFlash();

  if (!rtc.begin())
  {
    Serial.println("\n[RTC] Khong tim thay DS3231! He thong van tiep tuc chay, se tu dong thu lai.");
    rtcOk = false;
  }
  else
  {
    rtcOk = true;
  }

  dht.begin();
  pinMode(BELL, OUTPUT);

  xTaskCreatePinnedToCore(
      TaskKetNoiWifiBanDau,
      "WifiInitTask",
      12288,
      NULL,
      1,
      NULL,
      1);
}

// Vòng lặp chính
void loop()
{
  KiemTraLaiRTC();

  DateTime now = rtcOk ? rtc.now() : DateTime((uint32_t)0);

  BaoThuc(now);
  KiemTraTatChuong();
  MatrixPanel();
  DongHo(now);
  CamBienDHT();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyDocBaoThucFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyDatNgayFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyDatGioFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyDoSangFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyThoiGianReoFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyWifiFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyQuetWifiFirebase();
  KiemTraTatChuong();
  MatrixPanel();

  XuLyAPMode();
}

// Ngắt timer phần cứng, báo Task quét LED
void IRAM_ATTR triggerScan()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (hTaskScanLED != NULL)
    vTaskNotifyGiveFromISR(hTaskScanLED, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Task quét màn hình LED qua SPI
void TaskScanLED(void *param)
{
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    dmd.scanDisplayBySPI();
  }
}

// Kích hoạt cấu hình kết nối Firebase
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

// Ghi SSID WiFi đang kết nối lên Firebase
void GhiWifiHienTaiLenFirebase()
{
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return;
  String ssidHienTai = WiFi.SSID();
  if (ssidHienTai.length() == 0)
    return;
  Firebase.RTDB.setString(&Data, F("/WiFi/ssidHienTai"), ssidHienTai);
  Serial.printf("[Firebase] Da ghi WiFi hien tai: %s\n", ssidHienTai.c_str());
}

// Đọc danh sách báo thức đã lưu từ flash
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
    Serial.println("\n[Flash] Vung nho trong. Da khoi tao mac dinh.");
    delay(100);
  }
  else
  {
    Serial.println("\n[Flash] Khoi phuc danh sach tu Flash thanh cong!");
    delay(100);
  }

  for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
  {
    thuMaskBaoThuc[i] = 0xFF;
  }
}

// Lưu danh sách báo thức vào flash
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

// Đọc mảng ngày lặp từ Firebase và nén thành bitmask (0 = báo thức một lần, 0xFF = chưa có dữ liệu)
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

// Ghi toàn bộ danh sách báo thức hiện tại từ RAM lên Firebase
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

  if (!Firebase.RTDB.setJSON(&Data, F("/DongHo/dsBaoThuc"), &json))
  {
    Serial.printf("[Firebase] LOI dong bo danh sach bao thuc: %s\n", Data.errorReason().c_str());
    return false;
  }

  Serial.println("[Firebase] Da dong bo danh sach bao thuc len Firebase.");
  return true;
}

// Tắt riêng một báo thức theo index và đẩy trạng thái xuống Firebase ngay lập tức
bool TatBaoThucMotLan(int index)
{
  if (index < 0 || index >= MAX_BAO_THUC)
    return false;

  String basePath = "/DongHo/dsBaoThuc/BaoThuc" + String(index + 1);
  bool okActive = Firebase.RTDB.setBool(&Data, basePath + "/active", false);

  FirebaseJsonArray emptyDays;
  bool okDays = Firebase.RTDB.setArray(&Data, basePath + "/thu", &emptyDays);

  if (!okActive || !okDays)
  {
    Serial.printf("[Firebase] LOI tat bao thuc mot lan #%d: active=%s, thu=%s\n",
                  index + 1,
                  okActive ? "OK" : Data.errorReason().c_str(),
                  okDays ? "OK" : Data.errorReason().c_str());
    return false;
  }

  Serial.printf("[Firebase] Da tat bao thuc mot lan #%d\n", index + 1);
  return true;
}

bool LaBaoThucMotLan(uint8_t index)
{
  if (index >= MAX_BAO_THUC)
    return false;
  return thuMaskBaoThuc[index] == 0;
}

// Cập nhật giờ hiển thị và đồng bộ lên Firebase
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

  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeDocDS3231 >= 1000 || TimeDocDS3231 == 0))
  {
    TimeDocDS3231 = millis();

    static bool daGhiWifi = false;
    if (!daGhiWifi)
    {
      GhiWifiHienTaiLenFirebase();
      daGhiWifi = true;
    }

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

// Kiểm tra và kích hoạt báo thức đúng giờ
void BaoThuc(DateTime now)
{
  if (!rtcOk)
    return;

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
    bool dungNgay = (thuMaskBaoThuc[i] == 0xFF) ||
                    (thuMaskBaoThuc[i] == 0) ||
                    (thuMaskBaoThuc[i] & (uint8_t)(1U << now.dayOfTheWeek()));

    if (dsbaothuc[i].active && dungNgay && now.hour() == dsbaothuc[i].gio && now.minute() == dsbaothuc[i].phut)
    {
      tiengchuongreo = true;
      TimeBatDauChuongReo = millis();
      TimeDoiPhaChuong = millis();
      dangPhaNghiChuong = false;
      phutcuoicung = now.minute();
      digitalWrite(BELL, HIGH);
      Serial.printf("\nBAO THUC SO %d KICH HOAT! (Reo trong %u giay, ngat quang 3s reo/2s nghi)\n", i + 1, ThoiGianReoGiay);

      if (LaBaoThucMotLan(i))
      {
        dsbaothuc[i].active = false;
        LuuBaoThucVaoFlash();

        if (!TatBaoThucMotLan(i))
        {
          baoThucCanDongBoFirebase = true;
          Serial.println("[Firebase] Chua tat duoc bao thuc 1 lan ngay lap tuc, se thu dong bo lai sau.");
        }
      }

      break;
    }
  }
}

// Điều khiển nhịp chuông reo/nghỉ và tự tắt chuông
void KiemTraTatChuong()
{
  if (!tiengchuongreo)
    return;

  unsigned long tongDaTroi = millis() - TimeBatDauChuongReo;

  if (tongDaTroi >= (unsigned long)ThoiGianReoGiay * 1000UL)
  {
    tiengchuongreo = false;
    digitalWrite(BELL, LOW);
    Serial.println("\n--- Tu dong tat chuong thanh cong! ---");
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
    digitalWrite(BELL, !digitalRead(BELL));
    if (millis() - TimeDoiPhaChuong >= THOI_GIAN_REO_PHA)
    {
      dangPhaNghiChuong = true;
      TimeDoiPhaChuong = millis();
      digitalWrite(BELL, LOW);
    }
  }
}

// Đọc cảm biến nhiệt độ, độ ẩm
void CamBienDHT()
{
  if (millis() - TimeDocDHT > 15000 || TimeDocDHT == 0)
  {
    TimeDocDHT = millis();
    float temp = dht.readTemperature();
    float humi = dht.readHumidity();
    if (!isnan(temp) && !isnan(humi))
    {
      if (!yeuCauDoiWifi && !dangQuetWifi && firebaseDaKhoiTao && Firebase.ready())
      {
        Firebase.RTDB.setFloat(&Data, F("/CamBien/NhietDo"), temp);
        Firebase.RTDB.setFloat(&Data, F("/CamBien/DoAm"), humi);
      }
      NhietDo = (int8_t)temp;
      DoAm = (uint8_t)humi;
    }
  }
}

// Đọc danh sách báo thức từ Firebase
void XuLyDocBaoThucFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;

  if (baoThucCanDongBoFirebase)
  {
    if (!firebaseDaKhoiTao || !Firebase.ready())
      return;

    bool daDongBo = false;
    for (int i = 0; i < MAX_BAO_THUC; i++)
    {
      if (!dsbaothuc[i].active && LaBaoThucMotLan(i))
      {
        if (TatBaoThucMotLan(i))
        {
          daDongBo = true;
        }
      }
    }

    if (daDongBo)
    {
      baoThucCanDongBoFirebase = false;
    }

    return;
  }

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

        if (coThayDoi)
        {
          LuuBaoThucVaoFlash();
          Serial.println("[Firebase] Da cap nhat danh sach bao thuc vao RAM!");
        }
      }
    }
  }
}

// Xử lý lệnh chỉnh ngày từ Firebase
void XuLyDatNgayFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;

  if (!rtcOk)
    return;

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

        if (resultCapNhat.success && resultCapNhat.boolValue == true)
        {
          json.get(resultNgay, "/Ngay");
          json.get(resultThang, "/Thang");
          json.get(resultNam, "/Nam");

          DateTime hienTai = rtc.now();

          uint8_t ngayMoi = resultNgay.success ? resultNgay.intValue : hienTai.day();
          uint8_t thangMoi = resultThang.success ? resultThang.intValue : hienTai.month();
          uint16_t namMoi = resultNam.success ? resultNam.intValue : hienTai.year();

          DateTime ngayMoiSet(namMoi, thangMoi, ngayMoi,
                              hienTai.hour(), hienTai.minute(), hienTai.second());

          rtc.adjust(ngayMoiSet);

          Serial.printf("\n[Firebase] Da nhan lenh chinh ngay -> RTC: %02d/%02d/%04d\n",
                        ngayMoi, thangMoi, namMoi);

          Firebase.RTDB.setBool(&Data, F("/DongHo/DatNgay/capNhat"), false);
        }
      }
    }
  }
}

// Xử lý lệnh chỉnh giờ từ Firebase
void XuLyDatGioFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;

  if (!rtcOk)
    return;

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

// Xử lý độ sáng LED từ Firebase
void XuLyDoSangFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;

  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckDoSang > 5000 || TimeCheckDoSang == 0))
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
        Serial.printf("[DoSang] Cap nhat do sang LED: %d\n", doSang);
      }
    }
  }
}

// Xử lý thời gian chuông reo từ Firebase
void XuLyThoiGianReoFirebase()
{
  if (yeuCauDoiWifi || dangQuetWifi)
    return;

  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeCheckThoiGianReo > 5000 || TimeCheckThoiGianReo == 0))
  {
    TimeCheckThoiGianReo = millis();

    if (Firebase.RTDB.getInt(&Data, F("/DongHo/ThoiGianReo")))
    {
      int giay = Data.intData();
      giay = constrain(giay, 1, 300);
      static int giayTruocDo = -1;

      if (giay != giayTruocDo)
      {
        giayTruocDo = giay;
        ThoiGianReoGiay = (uint16_t)giay;
        Serial.printf("[ThoiGianReo] Cap nhat thoi gian chuong reo: %d giay\n", giay);
      }
    }
  }
}

// Task khởi tạo ngắt timer trên Core 0
void TaskKhoiTaoNgatCore0(void *ThamSo)
{
  xTaskCreatePinnedToCore(
      TaskScanLED,
      "TaskScanLED",
      4096,
      NULL,
      configMAX_PRIORITIES - 1,
      &hTaskScanLED,
      0);

  uint8_t cpuClock = ESP.getCpuFreqMHz();
  timer = timerBegin(0, cpuClock, true);
  timerAttachInterrupt(timer, &triggerScan, true);
  timerAlarmWrite(timer, 1000, true);
  timerAlarmEnable(timer);

  Serial.println("\n[He thong] Ngat cung da duoc ghim vao CORE 0!");
  vTaskDelete(NULL);
}

// Hiển thị giờ, nhiệt độ/độ ẩm lên màn hình LED
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
  bool canDoiCamBien = (millis() - thoiGianDoiCamBien >= 15000);

  if (!phutThayDoi && !canToggle && !canDoiCamBien)
    return;

  if (canDoiCamBien)
  {
    thoiGianDoiCamBien = millis();
    hienNhietDo = !hienNhietDo;
    phutThayDoi = true;
  }

  if (canToggle)
  {
    thoiGianToggle = millis();
    dauHaiChamHien = !dauHaiChamHien;
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
      dmd.drawString(5, 9, textSo, 2, GRAPHICS_NORMAL);
      const uint8_t deg[2] = {0x60, 0x60};
      for (int row = 0; row < 2; row++)
        for (int col = 0; col < 2; col++)
          if (deg[row] & (0x40 >> col))
            dmd.writePixel(19 + col, 9 + row, GRAPHICS_NORMAL, 1);
      dmd.drawChar(22, 9, 'C', GRAPHICS_NORMAL);
    }
    else
    {
      char textSoAm[3];
      sprintf(textSoAm, "%02d", DoAm);
      dmd.drawString(5, 9, textSoAm, 2, GRAPHICS_NORMAL);
      VeDauPhanTram(20, 9);
    }

    dmd.selectFont(System5x7);
    if (dauHaiChamHien)
      dmd.drawChar(14, 0, ':', GRAPHICS_NORMAL);
    else
      dmd.drawFilledBox(14, 0, 18, 7, GRAPHICS_INVERSE);
  }
  else if (canToggle)
  {
    dmd.selectFont(System5x7);
    if (dauHaiChamHien)
      dmd.drawChar(14, 0, ':', GRAPHICS_NORMAL);
    else
      dmd.drawFilledBox(14, 0, 18, 7, GRAPHICS_INVERSE);
  }
}

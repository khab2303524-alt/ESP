#include <Arduino.h>
#include <WiFi.h>
#include <Firebase.h>
#include <RTClib.h>
#include <DHT.h>
#include <SPI.h>
#include <DMD32.h>
#include <Preferences.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "fonts/SystemFont5x7.h"

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
#define CHU_KY_DINH_DANG_GIO_MS 900UL
#define CHU_KY_DOC_BAO_THUC_MS 4000UL
#define CHU_KY_DOC_DHT_MS 15000UL
#define THOI_GIAN_CHO_CAM_BIEN_MS 180000UL // 3 phut cho cam bien DHT on dinh & cho phep ket noi Wi-Fi/App

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
unsigned long TimeCheckDatNgay = 0;
unsigned long TimeCheckDatGio = 0;
unsigned long TimeCheckDoSang = 0;

bool dhtDaOnDinh = false;
unsigned long thoiGianKhoiDongDht = 0;
volatile bool choPhepKetNoiWifi = true;  // Chi cho phep ket noi Wi-Fi luc moi mo dien (den chop tat)
volatile bool wifiDaTatHoanToan = false; // Danh dau da ngat va tat Wi-Fi hoan toan
uint8_t doSangHienTai = 150;             // Do sang LED (0-255)

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

String wifiSSID = SSID_DEFAULT;
String wifiPassword = PASSWORD_DEFAULT;

#define AP_SSID "LED_P10"
WebServer server(80);
bool apDangHoatDong = false;
unsigned long thoiDiemBatDauAP = 0;
#define AP_TIMEOUT_MS 300000UL // 5 phut

volatile bool coWifiMoiTuAP = false;
String apSsidMoi = "";
String apPassMoi = "";

volatile TaskHandle_t hTaskScanLED = NULL;
volatile bool scanChoPhep = true;
SemaphoreHandle_t serialMutex = NULL;
SemaphoreHandle_t baoThucMutex = NULL;
SemaphoreHandle_t prefMutex = NULL;

bool taskDocBaoThucDaTao = false;

void TamDungQuetLED()
{
  scanChoPhep = false;
  vTaskDelay(pdMS_TO_TICKS(3)); // Dùng vTaskDelay thay delay() để yield FreeRTOS scheduler
  OE_DMD_ROWS_OFF();
}

void TiepTucQuetLED()
{
  scanChoPhep = true;
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
uint8_t DocThuMaskTuFirebase(FirebaseJson &json, const String &pathThu);
bool LaBaoThucMotLan(uint8_t index);
void TaskKhoiTaoNgatCore(void *ThamSo);
void DocWifiTuFlash();
bool GhiWifiHienTaiLenFirebase();
void XuLyDongBoSsidFirebase();
void BatAPServer();
void XuLyAPServer();
void HandlePostWifi();
void KhoiTaoManHinhLED();
void KiemTraLaiRTC();
void TaskKetNoiWifiBanDau(void *param);
void XuLyChuongThuCongFirebase();
void TaskMatrixPanel(void *param);
void TaskDocBaoThucFirebase(void *param);
void TaskDocDHT(void *param);
void DocCaiDatTuFlash();
void LuuCaiDatVaoFlash();
void TatWiFiHoanToan();

void DocWifiTuFlash()
{
  if (prefMutex != NULL)
    xSemaphoreTake(prefMutex, portMAX_DELAY);
  preferences.begin("WiFiCfg", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();
  if (prefMutex != NULL)
    xSemaphoreGive(prefMutex);

  if (ssid.length() > 0)
  {
    wifiSSID = ssid;
    wifiPassword = pass;
    Serial.printf("[Flash] Doc tu flash:\n  SSID: '%s' (do dai: %d)\n  Pass: '%s' (do dai: %d)\n",
                  ssid.c_str(), ssid.length(), pass.c_str(), pass.length());
  }
  else
  {
    wifiSSID = SSID_DEFAULT;
    wifiPassword = PASSWORD_DEFAULT;
    Serial.printf("[Flash] Dung WiFi default:\n  SSID: '%s'\n  Pass: '%s'\n", wifiSSID.c_str(), wifiPassword.c_str());
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
  if (!choPhepKetNoiWifi)
    return;

  static int trangThaiCu = -1;
  uint8_t trangThaiMoi = WiFi.status();

  if (trangThaiCu == trangThaiMoi)
    return;

  if (trangThaiMoi == WL_CONNECTED)
  {
    Serial.printf("[WiFi] KET NOI THANH CONG: SSID='%s', IP=%s, RSSI=%d dBm\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // Ket noi thanh cong -> gui SSID hien tai len Firebase (loop tu thu lai den khi Firebase san sang)
    ssidCanDongBoFirebase = true;
    TimeThuLaiSsidFirebase = 0;
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

void HandlePostWifi()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");

  String body = server.arg("plain");
  Serial.printf("[AP-Server] Nhan POST /wifi: %s\n", body.c_str());

  String parsedSsid = "";
  String parsedPass = "";

  int idxSsid = body.indexOf("\"ssid\":");
  if (idxSsid >= 0)
  {
    int startQuote = body.indexOf("\"", idxSsid + 7);
    if (startQuote >= 0)
    {
      int endQuote = body.indexOf("\"", startQuote + 1);
      if (endQuote >= 0)
        parsedSsid = body.substring(startQuote + 1, endQuote);
    }
  }

  int idxPass = body.indexOf("\"password\":");
  if (idxPass >= 0)
  {
    int startQuote = body.indexOf("\"", idxPass + 11);
    if (startQuote >= 0)
    {
      int endQuote = body.indexOf("\"", startQuote + 1);
      if (endQuote >= 0)
        parsedPass = body.substring(startQuote + 1, endQuote);
    }
  }

  auto parseIntField = [&](const char *field) -> int
  {
    int idx = body.indexOf(field);
    if (idx < 0)
      return -1;
    int colon = body.indexOf(":", idx);
    if (colon < 0)
      return -1;
    int comma = body.indexOf(",", colon);
    int brace = body.indexOf("}", colon);
    int endIdx = comma > 0 ? (brace > 0 && brace < comma ? brace : comma) : brace;
    if (endIdx < 0)
      endIdx = body.length();
    String valStr = body.substring(colon + 1, endIdx);
    valStr.trim();
    return valStr.toInt();
  };

  int g = parseIntField("\"gio\"");
  int p = parseIntField("\"phut\"");
  int s = parseIntField("\"giay\"");
  int d = parseIntField("\"ngay\"");
  int m = parseIntField("\"thang\"");
  int y = parseIntField("\"nam\"");

  if (g >= 0 && p >= 0 && d >= 1 && m >= 1 && y >= 2020 && rtcOk)
  {
    s = (s >= 0 && s <= 59) ? s : 0;
    DateTime appTime((uint16_t)y, (uint8_t)m, (uint8_t)d, (uint8_t)g, (uint8_t)p, (uint8_t)s);
    rtc.adjust(appTime);
    Serial.printf("[AP-Server] Da dong bo gio tu App: %02d:%02d:%02d %02d/%02d/%04d\n", g, p, s, d, m, y);
  }

  if (parsedSsid.length() == 0)
  {
    server.send(400, "application/json; charset=utf-8", "{\"status\":\"error\",\"message\":\"Thiếu tên Wi-Fi (SSID)\"}");
    return;
  }

  apSsidMoi = parsedSsid;
  apPassMoi = parsedPass;
  coWifiMoiTuAP = true;

  server.send(200, "application/json; charset=utf-8", "{\"status\":\"success\",\"message\":\"Đã nhận Wi-Fi! Đồng hồ đang khởi động lại để kết nối...\"}");
  Serial.printf("[AP-Server] Da nhan tu App:\n  SSID: '%s' (do dai: %d)\n  Pass: '%s' (do dai: %d)\n",
                apSsidMoi.c_str(), apSsidMoi.length(), apPassMoi.c_str(), apPassMoi.length());
}

void TatWiFiHoanToan()
{
  if (wifiDaTatHoanToan)
    return;
  wifiDaTatHoanToan = true;
  choPhepKetNoiWifi = false;

  SafePrintln("\n========================================================");
  SafePrintln("[WiFi] HET GIAI DOAN KHOI DONG (CAM BIEN DA ON DINH).");
  SafePrintln("[WiFi] DANG NGAT KET NOI VA TAT WI-FI RADIO HOAN TOAN...");
  SafePrintln("========================================================");

  // Tat WebServer va SoftAP neu dang mo
  if (apDangHoatDong)
  {
    server.stop();
    WiFi.softAPdisconnect(true);
    apDangHoatDong = false;
    SafePrintln("[AP] Da tat WebServer va SoftAP.");
  }

  // Dung Firebase tu dong ket noi lai
  Firebase.reconnectWiFi(false);

  // Ngat ket noi STA va tat radio Wi-Fi hoan toan
  WiFi.disconnect(true, false);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.mode(WIFI_OFF);

  SafePrintln("[WiFi] Da tat Wi-Fi hoan toan! Bang den chuyen sang che do chay doc lap (offline).");
  SafePrintln("[WiFi] De ket noi lai voi App: Vui long tat nguon va bat lai bang den.");
}

void BatAPServer()
{
  if (!choPhepKetNoiWifi)
    return;
  if (apDangHoatDong)
    return;
  Serial.println("[AP] Bat AP Mode 'LED_P10' va khoi dong WebServer...");
  WiFi.disconnect(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);

  server.on("/wifi", HTTP_OPTIONS, []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204); });

  server.on("/wifi", HTTP_POST, HandlePostWifi);

  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html; charset=utf-8", "<h3>Đồng hồ LED P10 - Chế độ AP</h3><p>Kết nối thành công vào LED_P10.</p>"); });

  server.begin();
  apDangHoatDong = true;
  thoiDiemBatDauAP = millis();
  Serial.printf("[AP] AP 'LED_P10' IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void XuLyAPServer()
{
  if (!apDangHoatDong || !choPhepKetNoiWifi)
    return;

  server.handleClient();

  if (coWifiMoiTuAP)
  {
    coWifiMoiTuAP = false;

    Serial.printf("[AP] Dang luu WiFi moi: %s\n", apSsidMoi.c_str());

    if (prefMutex != NULL)
      xSemaphoreTake(prefMutex, portMAX_DELAY);
    TamDungQuetLED();
    preferences.begin("WiFiCfg", false);
    preferences.putString("ssid", apSsidMoi);
    preferences.putString("pass", apPassMoi);
    preferences.end();
    TiepTucQuetLED();
    if (prefMutex != NULL)
      xSemaphoreGive(prefMutex);

    wifiSSID = apSsidMoi;
    wifiPassword = apPassMoi;

    // Chờ 1.5s để server truyền xong response HTTP 200 cho điện thoại trước khi restart
    Serial.println("[AP] Da luu WiFi moi vao flash. Dang khoi dong lai ESP32 de ket noi STA sach...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP.restart();
    return;
  }

  // Timeout AP mode neu khong ai cau hinh
  if (millis() - thoiDiemBatDauAP > AP_TIMEOUT_MS)
  {
    Serial.println("[AP] Het thoi gian cho AP, khoi dong lai ESP32 de thu ket noi lai...");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
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

  if (xTaskCreatePinnedToCore(TaskKhoiTaoNgatCore, "InitScanLED", 4096, NULL, configMAX_PRIORITIES, NULL, 1) != pdPASS)
    Serial.println("[LED] LOI: khong tao duoc task khoi tao ngat"); // Stack tăng 2048->4096 (tạo task con + timerBegin bên trong)
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
  DocWifiTuFlash();

  // Kiem tra xem co WiFi da luu trong flash khong
  bool coWifiFlash = false;
  {
    if (prefMutex != NULL)
      xSemaphoreTake(prefMutex, portMAX_DELAY);
    TamDungQuetLED();
    preferences.begin("WiFiCfg", true);
    String ssidFlash = preferences.getString("ssid", "");
    preferences.end();
    TiepTucQuetLED();
    if (prefMutex != NULL)
      xSemaphoreGive(prefMutex);
    coWifiFlash = (ssidFlash.length() > 0);
  }

  if (coWifiFlash)
  {
    // Co WiFi trong flash: thu ket noi STA
    Serial.printf("[WiFi] Dang thu ket noi toi:\n  SSID: '%s' (do dai: %d)\n  Pass: '%s' (do dai: %d)\n",
                  wifiSSID.c_str(), wifiSSID.length(), wifiPassword.c_str(), wifiPassword.length());
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    WiFi.setAutoReconnect(true);

    unsigned long t = millis();
    while (choPhepKetNoiWifi && WiFi.status() != WL_CONNECTED && millis() - t < 15000)
    {
      vTaskDelay(pdMS_TO_TICKS(500));
      Serial.print(".");
    }
    Serial.println();

    if (!choPhepKetNoiWifi)
    {
      vTaskDelete(NULL);
      return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("[WiFi] Ket noi STA thanh cong!");
      KichHoatCauHinhFirebase();
    }
    else
    {
      // Ket noi STA that bai -> bat AP de cho cau hinh lai (neu con trong thoi gian cho phep)
      if (choPhepKetNoiWifi)
      {
        Serial.println("[WiFi] Ket noi STA that bai. Bat AP de cho cau hinh lai...");
        BatAPServer();
      }
    }
  }
  else
  {
    // Chua co WiFi trong flash -> phat AP ngay (neu con trong thoi gian cho phep)
    if (choPhepKetNoiWifi)
    {
      Serial.println("[WiFi] Chua co WiFi trong flash, bat AP de cau hinh...");
      BatAPServer();
    }
  }

  // Vong lap giu ket noi + xu ly mat mang trong giai doan cho phep Wi-Fi
  while (choPhepKetNoiWifi)
  {
    if (!apDangHoatDong && WiFi.status() != WL_CONNECTED)
    {
      vTaskDelay(pdMS_TO_TICKS(15000));
      if (choPhepKetNoiWifi && !apDangHoatDong && WiFi.status() != WL_CONNECTED)
      {
        Serial.printf("[WiFi] Thu ket noi lai: %s\n", wifiSSID.c_str());
        WiFi.disconnect();
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
  }

  Serial.println("[WifiInitTask] Task ket noi Wi-Fi da ket thuc.");
  vTaskDelete(NULL);
}

void setup()
{
  // Tat brownout detector de tranh reset gia khi WiFi radio keo dong lon (~500mA peak)
  // Nguyen nhan that su la nguon yeu / cap USB kem — nen dung adapter 5V/2A hoac tu loc
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(9600);
  delay(200);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
               {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    const char *msg = "Loi ket noi";
    switch (reason)
    {
    case 2:   msg = "AUTH_EXPIRE (AP timeout / tu choi xac thuc - do PMF/WPA3 hoac router tu choi)"; break;
    case 201: msg = "NO_AP_FOUND (Khong tim thay mang - router chi phat 5GHz hoac ngoai vung song)"; break;
    case 202: msg = "AUTH_FAIL (Sai mat khau Wi-Fi!)"; break;
    case 203: msg = "ASSOC_FAIL (Router tu choi ket noi - day client / chan MAC)"; break;
    case 204: msg = "HANDSHAKE_TIMEOUT (Timeout bat tay 4 buoc - sai mat khau hoac song yeu)"; break;
    case 15:  msg = "4WAY_HANDSHAKE_TIMEOUT (Sai mat khau WPA/WPA2)"; break;
    default:  break;
    }
    Serial.printf("\n[WiFi-Event] Mat ket noi STA, Ma ly do: %d (%s)\n", reason, msg); }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  serialMutex = xSemaphoreCreateMutex();
  baoThucMutex = xSemaphoreCreateMutex();
  prefMutex = xSemaphoreCreateMutex();
  KhoiTaoManHinhLED();
  DocBaoThucTuFlash();
  DocCaiDatTuFlash();

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

  // Stagger khoi dong task de tranh spike dong: LED truoc, WiFi sau 500ms, DHT sau 1000ms
  if (xTaskCreatePinnedToCore(TaskMatrixPanel, "TaskMatrixPanel", 4096, NULL, 2, NULL, 1) != pdPASS)
    Serial.println("[LED] LOI: khong tao duoc task hien thi");
  vTaskDelay(pdMS_TO_TICKS(500)); // Cho LED on dinh truoc khi WiFi radio bat len
  if (xTaskCreatePinnedToCore(TaskKetNoiWifiBanDau, "WifiInitTask", 12288, NULL, 1, NULL, 1) != pdPASS)
    Serial.println("[WiFi] LOI: khong tao duoc task khoi tao WiFi"); // Stack 12288 (WiFi+SSL+Firebase)
  vTaskDelay(pdMS_TO_TICKS(500));                                    // Cho WiFi khoi dong truoc khi DHT bat len
  if (xTaskCreatePinnedToCore(TaskDocDHT, "TaskDocDHT", 12288, NULL, 1, NULL, 0) != pdPASS)
    Serial.println("[DHT] LOI: khong tao duoc task doc DHT"); // Stack 12288 (DHT+Firebase SSL)
}

void loop()
{
  KiemTraLaiRTC();

  DateTime now = rtcOk ? rtc.now() : DateTime((uint32_t)0);
  BaoThuc(now);
  KiemTraTatChuong();
  DongHo(now);

  if (choPhepKetNoiWifi)
  {
    // Kiem tra het thoi gian cho cam bien / khoi dong
    if (!dhtDaOnDinh && (millis() - thoiGianKhoiDongDht > THOI_GIAN_CHO_CAM_BIEN_MS))
    {
      dhtDaOnDinh = true;
      TatWiFiHoanToan();
    }
    else
    {
      TheoDoiTrangThaiWiFi();
      XuLyDongBoSsidFirebase();
      XuLyAPServer();

      if (WiFi.status() == WL_CONNECTED && !firebaseDaKhoiTao && !apDangHoatDong)
      {
        KichHoatCauHinhFirebase();
      }

      XuLyDatGioFirebase();
      XuLyDinhDangGioFirebase();

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
        XuLyDoSangFirebase();
        buocFirebase = 3;
        break;
      case 3:
        XuLyThoiGianReoFirebase();
        buocFirebase = 4;
        break;
      case 4:
        XuLyChuongThuCongFirebase();
        buocFirebase = 0;
        break;
      default:
        buocFirebase = 0;
        break;
      }
    }
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}

void IRAM_ATTR triggerScan()
{
  if (!scanChoPhep)
    return;

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  TaskHandle_t h = (TaskHandle_t)hTaskScanLED; // đọc volatile handle 1 lần, an toàn hơn
  if (h)
    vTaskNotifyGiveFromISR(h, &xHigherPriorityTaskWoken);

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
    if (!dhtDaOnDinh && (millis() - thoiGianKhoiDongDht > THOI_GIAN_CHO_CAM_BIEN_MS))
    {
      dhtDaOnDinh = true;
      Serial.println("[DHT] Da on dinh! Bat dau hien thi nhiet do / do am.");
      TatWiFiHoanToan();
    }

    if (dhtDaOnDinh && (millis() - TimeDocDHT > CHU_KY_DOC_DHT_MS || TimeDocDHT == 0))
    {
      if (apDangHoatDong)
      {
        SafePrintln("[DHT] Bo qua lan doc, dang xu ly WiFi, se thu lai sau");
      }
      else
      {
        TimeDocDHT = millis();

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

    if (choPhepKetNoiWifi && canGuiDHTLenFirebase && !apDangHoatDong && firebaseDaKhoiTao)
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
  if (!choPhepKetNoiWifi)
    return;

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

  if (!taskDocBaoThucDaTao && choPhepKetNoiWifi)
  {
    if (xTaskCreatePinnedToCore(TaskDocBaoThucFirebase, "TaskDocBaoThuc", 12288, NULL, 1, NULL, 1) == pdPASS)
      taskDocBaoThucDaTao = true; // Stack 12288 (Firebase JSON 50 báo thức + SSL)
    else
      Serial.println("[Firebase] LOI: khong tao duoc task doc bao thuc");
  }
}

// Static FirebaseData dùng chung để tránh cấp phát/giải phóng SSL buffer trên heap mỗi lần gọi
static FirebaseData ssidData;
static bool ssidDataDaKhoiTao = false;

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

  if (!ssidDataDaKhoiTao)
  {
    ssidData.setBSSLBufferSize(4096, 1024);
    ssidDataDaKhoiTao = true;
  }
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
  if (apDangHoatDong || !choPhepKetNoiWifi)
    return;
  if (!ssidCanDongBoFirebase)
    return;
  if (millis() - TimeThuLaiSsidFirebase < 3000 && TimeThuLaiSsidFirebase != 0)
    return;

  TimeThuLaiSsidFirebase = millis();
  GhiWifiHienTaiLenFirebase();
}

void DocBaoThucTuFlash()
{
  if (prefMutex != NULL)
    xSemaphoreTake(prefMutex, portMAX_DELAY);
  preferences.begin("BaoThucNS", true);
  size_t kieuSize = sizeof(dsbaothuc);
  size_t readLen = preferences.getBytes("dsBT", dsbaothuc, kieuSize);
  preferences.end();
  if (prefMutex != NULL)
    xSemaphoreGive(prefMutex);

  if (readLen == 0)
  {
    for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
    {
      dsbaothuc[i].gio = 0;
      dsbaothuc[i].phut = 0;
      dsbaothuc[i].active = false;
    }
  }
  for (uint8_t i = 0; i < MAX_BAO_THUC; i++)
  {
    thuMaskBaoThuc[i] = 0xFF;
  }
}

void LuuBaoThucVaoFlash()
{
  if (prefMutex != NULL)
    xSemaphoreTake(prefMutex, portMAX_DELAY);
  TamDungQuetLED();
  preferences.begin("BaoThucNS", false);
  size_t kieuSize = sizeof(dsbaothuc);
  preferences.putBytes("dsBT", dsbaothuc, kieuSize);
  preferences.end();
  TiepTucQuetLED();
  if (prefMutex != NULL)
    xSemaphoreGive(prefMutex);
}

void DocCaiDatTuFlash()
{
  if (prefMutex != NULL)
    xSemaphoreTake(prefMutex, portMAX_DELAY);
  preferences.begin("CaiDatNS", true);
  doSangHienTai = preferences.getUChar("doSang", 150);
  ThoiGianReoGiay = preferences.getUShort("thoiGianReo", 5);
  DinhDang24Gio = preferences.getBool("dinhDang24h", true);
  preferences.end();
  if (prefMutex != NULL)
    xSemaphoreGive(prefMutex);

  dmd.setBrightness(doSangHienTai);
  Serial.printf("[Flash] Doc cai dat: DoSang=%u, ThoiGianReo=%u giay, DinhDang=%s\n",
                doSangHienTai, ThoiGianReoGiay, DinhDang24Gio ? "24h" : "12h");
}

void LuuCaiDatVaoFlash()
{
  if (prefMutex != NULL)
    xSemaphoreTake(prefMutex, portMAX_DELAY);
  TamDungQuetLED();
  preferences.begin("CaiDatNS", false);
  preferences.putUChar("doSang", doSangHienTai);
  preferences.putUShort("thoiGianReo", ThoiGianReoGiay);
  preferences.putBool("dinhDang24h", DinhDang24Gio);
  preferences.end();
  TiepTucQuetLED();
  if (prefMutex != NULL)
    xSemaphoreGive(prefMutex);
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

bool TatBaoThucMotLan(int index)
{
  if (index < 0 || index >= MAX_BAO_THUC)
    return false;
  if (apDangHoatDong || !choPhepKetNoiWifi)
    return false;
  if (!firebaseDaKhoiTao || !Firebase.ready())
    return false;
  char pathBuf[48];
  snprintf(pathBuf, sizeof(pathBuf), "/DongHo/dsBaoThuc/BaoThuc%d/active", index + 1);
  return Firebase.RTDB.setBool(&Data, pathBuf, false);
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
  if (apDangHoatDong || !choPhepKetNoiWifi)
  {
    Gio = now.hour();
    Phut = now.minute();
    Giay = now.second();
    return;
  }
  if (firebaseDaKhoiTao && Firebase.ready() && (millis() - TimeDocDS3231 >= CHU_KY_GHI_THOI_GIAN_MS || TimeDocDS3231 == 0))
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
  if (apDangHoatDong || !choPhepKetNoiWifi)
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

  while (choPhepKetNoiWifi)
  {
    if (!apDangHoatDong && firebaseDaKhoiTao && Firebase.ready())
    {
      if (Firebase.RTDB.getJSON(&baoThucData, F("/DongHo/dsBaoThuc")))
      {
        if (baoThucData.dataTypeEnum() == fb_esp_rtdb_data_type_json)
        {
          FirebaseJson &json = baoThucData.jsonObject();
          FirebaseJsonData resultBaoThuc, resultGio, resultPhut, resultActive;
          char pathBaoThuc[24];
          char pathSub[32];
          bool coThayDoi = false;

          if (xSemaphoreTake(baoThucMutex, pdMS_TO_TICKS(200)) == pdTRUE)
          {
            for (int i = 0; i < MAX_BAO_THUC; i++)
            {
              snprintf(pathBaoThuc, sizeof(pathBaoThuc), "/BaoThuc%d", i + 1);
              json.get(resultBaoThuc, pathBaoThuc);
              if (resultBaoThuc.success && resultBaoThuc.type == "object")
              {
                snprintf(pathSub, sizeof(pathSub), "/BaoThuc%d/gio", i + 1);
                json.get(resultGio, pathSub);
                snprintf(pathSub, sizeof(pathSub), "/BaoThuc%d/phut", i + 1);
                json.get(resultPhut, pathSub);
                snprintf(pathSub, sizeof(pathSub), "/BaoThuc%d/active", i + 1);
                json.get(resultActive, pathSub);
                snprintf(pathSub, sizeof(pathSub), "/BaoThuc%d/thu", i + 1);
                uint8_t g_moi = resultGio.success ? resultGio.intValue : 0;
                uint8_t p_moi = resultPhut.success ? resultPhut.intValue : 0;
                bool a_moi = resultActive.success ? resultActive.boolValue : false;
                uint8_t thuMoi = DocThuMaskTuFirebase(json, pathSub);
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

  Serial.println("[TaskDocBaoThuc] Da ngung task doc bao thuc vi Wi-Fi da tat.");
  taskDocBaoThucDaTao = false;
  vTaskDelete(NULL);
}

void XuLyDatNgayFirebase()
{
  if (apDangHoatDong || !rtcOk || !choPhepKetNoiWifi)
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
  if (apDangHoatDong || !rtcOk || !choPhepKetNoiWifi)
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
          Gio = gioMoi;
          Phut = phutMoi;
          Giay = giayMoi;
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
  if (apDangHoatDong || !choPhepKetNoiWifi)
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
        doSangHienTai = (uint8_t)doSang;
        dmd.setBrightness(doSangHienTai);
        LuuCaiDatVaoFlash();
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
  if (apDangHoatDong || !choPhepKetNoiWifi)
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
        LuuCaiDatVaoFlash();
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
  if (apDangHoatDong || !choPhepKetNoiWifi)
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
        LuuCaiDatVaoFlash();
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
  TaskHandle_t hScan = NULL;
  xTaskCreatePinnedToCore(TaskScanLED, "TaskScanLED", 4096, NULL, 5, &hScan, 1);
  hTaskScanLED = hScan;
  uint8_t cpuClock = ESP.getCpuFreqMHz();
  timer = timerBegin(0, cpuClock, true);
  timerAttachInterrupt(timer, &triggerScan, true);

  timerAlarmWrite(timer, 1200, true);
  timerAlarmEnable(timer);
  vTaskDelete(NULL);
}

void XuLyChuongThuCongFirebase()
{
  if (apDangHoatDong || !choPhepKetNoiWifi)
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
  static int8_t gioTruocDo = -1;
  static bool dinhDangTruocDo = true;
  uint8_t trangThaiBaoThuc = KiemTraTrangThaiIconBaoThuc(now);
  static unsigned long thoiGianToggle = 0;
  static bool dauHaiChamHien = true;
  static unsigned long thoiGianDoiCamBien = 0;
  static bool hienNhietDo = true;

  bool phutThayDoi = (Gio != gioTruocDo || Phut != phutTruocDo ||
                      DinhDang24Gio != dinhDangTruocDo ||
                      NhietDo != nhietDoTruocDo || DoAm != doAmTruocDo);
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
    gioTruocDo = Gio;
    dinhDangTruocDo = DinhDang24Gio;
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
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_camera.h>
#include <SPI.h>
#include <MFRC522.h>

// WiFi dan Telegram
const char* ssid = "NAMA_WIFI";
const char* password = "PASSWORD_WIFI";
String botToken = "BOT_TOKEN_KAMU";      // Ganti dengan token dari BotFather
String chatID = "CHAT_ID_KAMU";          // Ganti dengan chat ID kamu

// Konfigurasi server Telegram
String telegramApiURL = "https://api.telegram.org/bot" + botToken + "/sendPhoto";

// RC522 RFID
#define RST_PIN     15
#define SS_PIN      5
MFRC522 rfid(SS_PIN, RST_PIN);

// Pin
#define PIR_PIN         13
#define LDR_PIN         34
#define BUZZER_PIN      12
#define RELAY_PIN       14
#define FLASH_LED_PIN   4

// RFID list (kartu yang dianggap valid)
String allowedUIDs[] = {"12345678", "87654321"}; // Contoh UID RFID

// Threshold LDR
const int LDR_THRESHOLD = 1000;

// Timer debounce
unsigned long lastTriggerTime = 0;
const unsigned long debounceDelay = 10000; // 10 detik

// Inisialisasi Kamera
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5; config.pin_d1 = 18; config.pin_d2 = 19; config.pin_d3 = 21;
  config.pin_d4 = 36; config.pin_d5 = 39; config.pin_d6 = 34; config.pin_d7 = 35;
  config.pin_xclk = 0; config.pin_pclk = 22;
  config.pin_vsync = 25; config.pin_href = 23;
  config.pin_sscb_sda = 26; config.pin_sscb_scl = 27;
  config.pin_pwdn = 32; config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera gagal: 0x%x\n", err);
  }
}

// Cek apakah UID terdaftar
bool isUIDValid(String uid) {
  for (String valid : allowedUIDs) {
    if (uid == valid) return true;
  }
  return false;
}

// Baca RFID dan kembalikan UID sebagai string
String readRFID() {
  unsigned long timeout = millis() + 10000; // 10 detik tunggu
  while (millis() < timeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uidStr = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        uidStr += String(rfid.uid.uidByte[i], HEX);
      }
      uidStr.toUpperCase();
      rfid.PICC_HaltA();
      return uidStr;
    }
    delay(100);
  }
  return "";
}

// Kirim foto ke Telegram
void sendPhotoToTelegram() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Gagal ambil foto");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Non-SSL verification

  HTTPClient https;
  https.begin(client, telegramApiURL);
  https.addHeader("Content-Type", "multipart/form-data; boundary=123456789");

  String bodyStart = "--123456789\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + chatID + "\r\n";
  bodyStart += "--123456789\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\n";
  bodyStart += "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd = "\r\n--123456789--\r\n";

  int totalLen = bodyStart.length() + fb->len + bodyEnd.length();

  https.addHeader("Content-Length", String(totalLen));
  https.writeToStream(&client);
  client.print(bodyStart);
  client.write(fb->buf, fb->len);
  client.print(bodyEnd);

  int code = https.GET();
  Serial.print("Telegram response code: ");
  Serial.println(code);

  https.end();
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung");

  // Setup pin
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Kamera & RFID
  setupCamera();
  SPI.begin();
  rfid.PCD_Init();
}

void loop() {
  // Deteksi gerakan
  if (digitalRead(PIR_PIN) == HIGH && (millis() - lastTriggerTime > debounceDelay)) {
    lastTriggerTime = millis();
    Serial.println("Gerakan terdeteksi!");

    // Cek siang/malam
    int ldrVal = analogRead(LDR_PIN);
    bool isDark = (ldrVal < LDR_THRESHOLD);
    Serial.print("LDR: "); Serial.println(ldrVal);

    if (isDark) {
      digitalWrite(FLASH_LED_PIN, HIGH);
      delay(200);
    }

    // Scan RFID
    Serial.println("Menunggu scan RFID...");
    String uid = readRFID();

    if (uid == "") {
      Serial.println("RFID timeout!");
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1000);
      digitalWrite(BUZZER_PIN, LOW);
    } else if (isUIDValid(uid)) {
      Serial.println("Akses diterima!");
      digitalWrite(RELAY_PIN, HIGH);
      delay(5000);
      digitalWrite(RELAY_PIN, LOW);
    } else {
      Serial.println("Akses ditolak!");
      digitalWrite(BUZZER_PIN, HIGH);
      sendPhotoToTelegram();
      delay(2000);
      digitalWrite(BUZZER_PIN, LOW);
    }

    digitalWrite(FLASH_LED_PIN, LOW);
  }

  delay(200);
}

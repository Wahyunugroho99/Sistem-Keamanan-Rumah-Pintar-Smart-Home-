#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "esp_camera.h"

// Definisi pin ESP32-CAM (AI Thinker board)
#define PIR_PIN 13        // Bisa dipilih pin GPIO 13 (digital input)
#define DOOR_PIN 14       // Digital input GPIO 14 untuk saklar pintu
#define LDR_PIN 34        // ADC input GPIO 34 (ADC1 channel 6)
#define SIREN_PIN 12      // PWM output GPIO 12 untuk sirene

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Variables
volatile bool motionDetected = false;
volatile bool doorOpened = false;
int ldrValue = 0;

// Timer vars for PWM siren tone generation
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile int beepFreq = 1000;  // frequency in Hz for siren tone

// Forward declarations
void IRAM_ATTR pirISR();
void IRAM_ATTR doorISR();
void setupCamera();
void triggerAlarm();
void stopAlarm();

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize pins
  pinMode(PIR_PIN, INPUT);
  pinMode(DOOR_PIN, INPUT_PULLUP); // saklar pintu aktif LOW saat tertutup
  pinMode(SIREN_PIN, OUTPUT);

  // Attach interrupts
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
  attachInterrupt(digitalPinToInterrupt(DOOR_PIN), doorISR, CHANGE);

  // Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Smart Home Security");
  display.println("Initializing...");
  display.display();
  delay(2000);

  // Init camera (not used now but can be activated for photo capture)
  setupCamera();

  // Setup timer for PWM tone siren generation
  timer = timerBegin(0, 80, true); // prescale 80: 1 microsecond per tick
  timerAttachInterrupt(timer, &triggerAlarm, true);
  timerAlarmWrite(timer, 500, true); // 500us timer alarm (2kHz base)
  timerAlarmEnable(timer);

  Serial.println("Setup complete");
}

void loop() {
  // Read LDR analog value (0-4095)
  ldrValue = analogRead(LDR_PIN);

  // Update OLED display
  display.clearDisplay();
  display.setCursor(0,0);
  display.printf("Motion: %s\n", motionDetected ? "YES" : "NO");
  display.printf("Door: %s\n", doorOpened ? "OPEN" : "CLOSED");
  display.printf("LDR: %d\n", ldrValue);
  display.display();

  // Send status via Serial every 1 second
  static unsigned long lastSend = 0;
  if(millis() - lastSend > 1000) {
    lastSend = millis();
    Serial.printf("Motion: %s, Door: %s, LDR: %d\n",
      motionDetected ? "YES" : "NO",
      doorOpened ? "OPEN" : "CLOSED",
      ldrValue);
  }

  // Sirene akan beroperasi jika motion atau door opened
  if(!motionDetected && !doorOpened) {
    stopAlarm();
  }

  delay(200);
}

// Interrupt service for PIR
void IRAM_ATTR pirISR() {
  motionDetected = true;
  Serial.println("Interrupt: Motion Detected!");
}

// Interrupt service for door sensor
void IRAM_ATTR doorISR() {
  if(digitalRead(DOOR_PIN) == HIGH) {
    doorOpened = true;
    Serial.println("Interrupt: Door Opened!");
  } else {
    doorOpened = false;
    Serial.println("Interrupt: Door Closed!");
  }
}

// Timer ISR for siren PWM generation
void IRAM_ATTR triggerAlarm() {
  static bool pinState = false;
  static int counter = 0;
  static int freq = 1000;
  static bool increasing = true;

  portENTER_CRITICAL_ISR(&timerMux);
  if(motionDetected || doorOpened) {
    // Frequency modulated siren between 1000-2000 Hz
    counter++;
    if(counter >= (800000 / freq)) { // 800k = 1 sec at 1us tick, approx
      pinState = !pinState;
      counter = 0;
      digitalWrite(SIREN_PIN, pinState);
      if(increasing) {
        freq += 20;
        if(freq >= 2000) increasing = false;
      } else {
        freq -= 20;
        if(freq <= 1000) increasing = true;
      }
    }
  } else {
    digitalWrite(SIREN_PIN, LOW);
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

void stopAlarm() {
  motionDetected = false;
  doorOpened = false;
  digitalWrite(SIREN_PIN, LOW);
}

// Optional: Camera setup placeholder
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
  } else {
    Serial.println("Camera init OK");
  }
}

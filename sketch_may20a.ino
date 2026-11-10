#include "HX711.h"
#include <LiquidCrystal.h>                        

// ─── PIN ────────────────────────────────────────────────────
#define PIN_HX_DT     2
#define PIN_HX_SCK    3
#define PIN_BTN_POWER 4
#define PIN_BTN_SET   5
#define PIN_LED_RED   7
#define PIN_LED_GREEN 8
#define PIN_BUZZER    9
LiquidCrystal lcd(13, 12, 11, 10, 6, A0);

// ─── KONFIGURASI ────────────────────────────────────────────
const float         CAL_FACTOR    = 423;
const float         THRESHOLD_G   = 20.0;
const float         MIN_WEIGHT_G  = 30.0;
const unsigned long DEBOUNCE_MS   = 250UL;
const unsigned long READ_INTERVAL = 500UL;

// ─── STATE ──────────────────────────────────────────────────
enum State { S_OFF, S_IDLE, S_SETTING, S_MONITORING, S_ALARM };
State state     = S_OFF;
State prevState = S_OFF;                             

// ─── VARIABEL ───────────────────────────────────────────────
HX711 scale;
float refWeight = 0.0;

unsigned long lastRelease[2] = {0, 0};
bool          prevBtn[2]     = {HIGH, HIGH};
unsigned long lastReadMs     = 0;
unsigned long lastBlinkMs    = 0;
unsigned long suppressUntilMs = 0;
bool          blinkState     = false;
bool          setReadyInSetting = false;
bool prevBothDown = false;                           
bool buzzerMuted  = false;
//bool suppressAfterBoth = false;
uint8_t       powerTapCount   = 0;
unsigned long firstPowerTapMs = 0;                           

// ─── HELPER ─────────────────────────────────────────────────

bool released(uint8_t pin, uint8_t idx) {
  bool cur = digitalRead(pin);
  bool ok  = false;
  if (cur == HIGH && prevBtn[idx] == LOW) {
    unsigned long now = millis();
    if (now - lastRelease[idx] > DEBOUNCE_MS) {
      ok = true;
      lastRelease[idx] = now;
    }
  }
  prevBtn[idx] = cur;
  return ok;
}

float readWeight() {
  if (scale.is_ready()) return scale.get_units(3);
  return -9999.0;
}

void leds(bool green, bool red) {
  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,   red   ? HIGH : LOW);
}

void buzzerOff() {
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

void blinkPin(uint8_t pin, unsigned long interval) {
  if (millis() - lastBlinkMs >= interval) {
    lastBlinkMs = millis();
    blinkState  = !blinkState;
    digitalWrite(pin, blinkState ? HIGH : LOW);
  }
}

void bipBuzzer() {
  if (buzzerMuted) return;                           
  ((millis() / 350) % 2) ? tone(PIN_BUZZER, 2500) : noTone(PIN_BUZZER);
}

void printBar(const char* msg) {
  Serial.println(F("─────────────────────────"));
  Serial.println(msg);
  Serial.println(F("─────────────────────────"));
}

// ─── LCD HELPER ─────────────────────────────────────────────// 

void lcdRow(uint8_t row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t i = 0;
  while (text[i] && i < 16) { lcd.write(text[i]); i++; }
  while (i < 16)             { lcd.write(' ');     i++; }
}

void lcdRowF(uint8_t row, const __FlashStringHelper* fstr) {
  char buf[17];
  strncpy_P(buf, (const char*)fstr, 16);
  buf[16] = '\0';
  lcdRow(row, buf);
}
/*
void lcdShowWeight(uint8_t row, float w) {
  char line[17];
  if (w > -9999.0) snprintf(line, 17, "Berat:%6.1f g", w);
  else              snprintf(line, 17, "Berat:  --   g");
  lcdRow(row, line);
}
*/

void lcdUpdateState() {
  lcd.clear();
  switch (state) {
    case S_OFF:
      lcdRowF(0, F("Phone Trapper"));
      lcdRowF(1, F("Tekan POWER ON"));
      break;
    case S_IDLE:
      lcdRowF(0, F("Status: AKTIF"));
      lcdRowF(1, F("Tekan SET"));
      break;
    case S_SETTING:
      lcdRowF(0, F("Mode Setting"));
      lcdRowF(1, F("Taruh HP..."));
      break;
    case S_MONITORING:
      lcdRowF(0, F("Memantau HP..."));
      //lcdShowWeight(1, refWeight);
      break;
    case S_ALARM:
      lcdRowF(0, F("!!! ALARM !!!"));
      lcdRowF(1, F("HP Diangkat!"));
      break;
  }
}
// ─── AKHIR LCD HELPER ───────────────────────────────────────

// ─── SETUP ──────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_GREEN, OUTPUT); digitalWrite(PIN_LED_GREEN, LOW);
  pinMode(PIN_LED_RED,   OUTPUT); digitalWrite(PIN_LED_RED,   LOW);
  pinMode(PIN_BUZZER,    OUTPUT); digitalWrite(PIN_BUZZER,    LOW);

  pinMode(PIN_BTN_POWER, INPUT_PULLUP);
  pinMode(PIN_BTN_SET,   INPUT_PULLUP);

  lcd.begin(16, 2);                                  // ← TAMBAH
  lcdRowF(0, F("Phone Trapper"));                    // ← TAMBAH
  lcdRowF(1, F("Memulai..."));                       // ← TAMBAH

  delay(1500);

  prevBtn[0]     = digitalRead(PIN_BTN_POWER);
  prevBtn[1]     = digitalRead(PIN_BTN_SET);
  lastRelease[0] = millis();
  lastRelease[1] = millis();

  scale.begin(PIN_HX_DT, PIN_HX_SCK);
  scale.set_scale(CAL_FACTOR);
  scale.tare();

  state = S_OFF;
  lcdUpdateState();                                  // ← TAMBAH

  Serial.println(F("=============================="));
  Serial.println(F("   IoT Phone Trapper v7"));
  Serial.println(F("=============================="));
  Serial.print(F("  Pin4 (POWER) saat boot: "));
  Serial.println(digitalRead(PIN_BTN_POWER) == LOW ? F("DITEKAN (cek wiring!)") : F("OK (HIGH)"));
  Serial.print(F("  Pin5 (SET)   saat boot: "));
  Serial.println(digitalRead(PIN_BTN_SET)   == LOW ? F("DITEKAN (cek wiring!)") : F("OK (HIGH)"));
  Serial.println(F("  Status: MATI"));
  Serial.println(F("=============================="));
}

// ─── LOOP UTAMA ─────────────────────────────────────────────
void loop() {

  // ── CEK KEDUA TOMBOL BERSAMAAN ─────────────────────────  //
  //bool pwrDown  = (digitalRead(PIN_BTN_POWER) == LOW);
  //bool setDown  = (digitalRead(PIN_BTN_SET)   == LOW);
  bool bothDown = (digitalRead(PIN_BTN_POWER) == LOW) &&
                  (digitalRead(PIN_BTN_SET)   == LOW);

  if (bothDown) {
    if (!prevBothDown && state == S_ALARM) {
      buzzerOff();
      buzzerMuted = false;
      state       = S_MONITORING;
      lastReadMs  = 0;
      printBar("  [KEMBALI] Monitoring dilanjutkan");
    }
    prevBothDown = true;
    // prevBtn[0] dan prevBtn[1] TIDAK diset di sini lagi
    if (state == S_ALARM) {
      digitalWrite(PIN_LED_GREEN, LOW);
      blinkPin(PIN_LED_RED, 150);
      bipBuzzer();
    }
    return;
  }

  if (prevBothDown) {   // ← TAMBAH: clear state tombol setelah keduanya dilepas
    //prevBtn[0] = HIGH;
    //prevBtn[1] = HIGH;
    suppressUntilMs = millis() + 600;
  }
  prevBothDown = false;
  // ── AKHIR CEK KEDUA TOMBOL ──────────────────────────────

  //if (prevBothDown) suppressAfterBoth = true;
  //prevBothDown = false;

  bool rPower = released(PIN_BTN_POWER, 0);
  bool rSet   = released(PIN_BTN_SET,   1);

  if (millis() < suppressUntilMs) {
    rPower = false;
    rSet   = false;
  }

  /*if (suppressAfterBoth) {
    rPower = false;
    rSet   = false;
    suppressAfterBoth = false;
  }
  */

  // ── DETEKSI DOUBLE TAP POWER ──────────────────────────
  bool doubleTap = false;
  if (rPower) {
    unsigned long now = millis();
    if (powerTapCount == 0) {
      powerTapCount   = 1;
      firstPowerTapMs = now;
      // Feedback tap pertama di LCD (kecuali S_OFF)
      if (state != S_OFF) lcdRowF(1, F("Tekan lagi: OFF "));
    } else if (now - firstPowerTapMs <= 700UL) {
      doubleTap     = true;
      powerTapCount = 0;
    } else {
      // Terlalu lama → restart hitungan
      powerTapCount   = 1;
      firstPowerTapMs = now;
      if (state != S_OFF) lcdRowF(1, F("Tekan lagi: OFF "));
    }
  }
  // Reset jika window 700ms habis tanpa tap kedua
  if (powerTapCount == 1 && millis() - firstPowerTapMs > 700UL) {
    powerTapCount = 0;
  }

  if (state == S_SETTING && !setReadyInSetting) {
    if (digitalRead(PIN_BTN_SET) == HIGH) {
      setReadyInSetting = true;
    }
    rSet = false;
  }

  // ── UPDATE LCD SAAT STATE BERUBAH ──────────────────────  // ← TAMBAH
  if (state != prevState) {
    lcdUpdateState();
    prevState = state;
  }

  switch (state) {

    case S_OFF:
      leds(false, false);
      buzzerOff();
      buzzerMuted = false;                           // ← TAMBAH (reset mute saat OFF)
      if (rPower) {
        scale.tare();
        state = S_IDLE;
        printBar("  [ON] Alat menyala");
        Serial.println(F("  Tekan SET (biru) untuk setting"));
      }
      break;

    case S_IDLE:
      leds(true, false);
      if (doubleTap) {
        state = S_OFF;
        printBar("  [OFF] Dimatikan");
      } else if (rSet) {
        scale.tare();
        setReadyInSetting = false;
        lastReadMs = 0;
        state = S_SETTING;
        printBar("  [SET] Mode setting");
        Serial.println(F("  >> Letakkan HP di timbangan"));
        Serial.println(F("  >> LED merah = HP terdeteksi"));
        Serial.println(F("  >> Tekan SET lagi untuk simpan"));
      }
      break;

    case S_SETTING:
      digitalWrite(PIN_LED_GREEN, HIGH);
      buzzerOff();

      if (millis() - lastReadMs >= READ_INTERVAL) {
        lastReadMs = millis();
        float w = readWeight();
        if (w > -9999.0) {
          bool ada = (w > MIN_WEIGHT_G);
          digitalWrite(PIN_LED_RED, ada ? HIGH : LOW);
          if (ada) {                                 // ← TAMBAH blok LCD setting
            lcdRowF(0, F("HP Terdeteksi!"));
          } else {
            lcdRowF(0, F("Mode Setting"));
          }
          //lcdShowWeight(1, w);                       // ← TAMBAH
          Serial.print(F("  Preview: "));
          Serial.print(w, 1);
          Serial.print(F(" g "));
          Serial.println(ada ? F("[HP TERDETEKSI]") : F("[belum ada HP]"));
          if (!setReadyInSetting)
            Serial.println(F("  (menunggu tombol SET dilepas dulu...)"));
        }
      }

      if (doubleTap) {
        leds(false, false);
        state = S_OFF;
        printBar("  [OFF] Dimatikan dari setting");

      } else if (rSet) {
        float w = readWeight();
        if (w > MIN_WEIGHT_G) {
          Serial.println(F("  Menyimpan berat..."));
          float sum = 0; int cnt = 0;
          unsigned long tS = millis();
          while (cnt < 5 && millis() - tS < 2000) {
            if (scale.is_ready()) { sum += scale.get_units(1); cnt++; }
          }
          if (cnt > 0) {
            refWeight = sum / cnt;
            leds(true, true);
            lcd.clear();                             // ← TAMBAH konfirmasi LCD
            lcdRowF(0, F("Berat Tersimpan!"));
            //lcdShowWeight(1, refWeight);
            delay(1200);                             // ← TAMBAH tahan agar terbaca
            state = S_MONITORING;
            lastReadMs = 0;
            Serial.print(F("  [MONITOR] Berat: "));
            Serial.print(refWeight, 1);
            Serial.println(F(" g — memantau..."));
          }
        } else {
          lcdRowF(1, F("Taruh HP dulu!"));           // ← TAMBAH
          Serial.println(F("  ! Belum ada HP — letakkan HP dulu"));
          delay(800);                                // ← TAMBAH beri waktu baca LCD
        }
      }
      break;

    case S_MONITORING:
      digitalWrite(PIN_LED_GREEN, HIGH);

      if (doubleTap) {
        leds(false, false);
        state = S_OFF;
        printBar("  [OFF] Dimatikan");
        break;
      }
      if (rSet) {
        scale.tare();
        digitalWrite(PIN_LED_RED, LOW);
        setReadyInSetting = false;
        lastReadMs = 0;
        state = S_SETTING;
        printBar("  [RESET] Set ulang berat");
        break;
      }

      if (millis() - lastReadMs >= READ_INTERVAL) {
        lastReadMs = millis();
        float cur = readWeight();
        if (cur > -9999.0) {
          float diff = abs(cur - refWeight);
          digitalWrite(PIN_LED_RED, (cur > MIN_WEIGHT_G) ? HIGH : LOW);
          //lcdShowWeight(1, cur);                     // ← TAMBAH update berat live
          Serial.print(F("MON | "));
          Serial.print(cur, 1);
          Serial.print(F("g | Ref:"));
          Serial.print(refWeight, 1);
          Serial.print(F("g | Δ:"));
          Serial.print(diff, 1);
          Serial.println(F("g"));
          if (diff > THRESHOLD_G) {
            buzzerMuted = false;                     // ← TAMBAH reset mute saat alarm baru
            state = S_ALARM;
            Serial.println(F("!!! ALARM !!!"));
          }
        }
      }
      break;

    case S_ALARM:
      digitalWrite(PIN_LED_GREEN, LOW);
      blinkPin(PIN_LED_RED, 150);
      bipBuzzer();
      if (doubleTap) {
        buzzerOff();
        buzzerMuted = false;                         // ← TAMBAH reset mute
        leds(false, false);
        state = S_OFF;
        printBar("  [OFF] Alarm dimatikan");
      }
      break;
  }
}
// XIAO RP2040: ESC制御 + RPM計測 + ロック検知 統合スケッチ
// モーター: 2204 / 260KV, DC12V-1A
// センサ  : LBR-127HLD（受光側 3.3V-6kΩ-P分岐、テープ通過でLOW）

#include "Servo.h"
#include "Adafruit_NeoPixel.h"

// ---------- ピン ----------
const int ESC_PIN = D0;     // GPIO26 ➔ ESC信号線
const int SW_PIN = D1;      // GPIO27 ➔ タクトスイッチ
const int SENSOR_PIN = D2;  // GPIO28 ➔ LBR-127HLD 受光出力（旧D0から配線を移す）

// XIAO RP2040 オンボードRGB LED
#define RGB_PWR 11
#define RGB_PIN 12
#define NUMPIXELS 1

// ---------- ESC パルス幅 (us) ----------
const int PULSE_OFF = 900;
const int PULSE_LOW = 1263;
const int PULSE_MED = 1630;
const int PULSE_HIGH = 2000;

// ---------- 計測・検知パラメータ ----------
const uint32_t MIN_PULSE_US = 2000;     // これより短い間隔のパルスは無視（チャタリング対策）
const uint32_t SPINUP_GRACE_MS = 1500;  // 段階切替後、この間はロック判定しない（始動待ち）
const uint32_t RUN_STALL_MS = 250;      // 回転中、この時間パルスが来なければロック
const uint32_t MAX_PERIOD_US = 150000;  // 1回転が150ms超（約400RPM未満）ならロック（ガクガク対策）
const uint32_t REPORT_MS = 500;         // RPM算出・表示の周期

Adafruit_NeoPixel pixels(NUMPIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);
Servo esc;

// 状態管理: 0=停止, 1=弱, 2=中, 3=強
int stepState = 0;
bool lastSwState = HIGH;
bool stallLatched = false;  // ロック停止中（ボタンで再開）
uint32_t stateChangedMs = 0;

// ISRで更新する変数
volatile uint32_t pulseCnt = 0;
volatile uint32_t firstUs = 0;
volatile uint32_t lastUs = 0;
volatile uint32_t lastPeriodUs = 0;
volatile uint32_t lastPulseMs = 0;

uint32_t lastReportMs = 0;
float rpm = 0;

// ---------- LED ----------
uint32_t lastColor = 0xFFFFFFFF;
void setColor(byte r, byte g, byte b) {
  uint32_t c = pixels.Color(r, g, b);
  if (c == lastColor) return;  // 変化がなければ書き込まない
  lastColor = c;
  pixels.setPixelColor(0, c);
  pixels.show();
}

// ---------- センサ割り込み ----------
void sensorIsr() {
  uint32_t now = micros();
  uint32_t period = now - lastUs;
  if (period < MIN_PULSE_US) return;
  lastPeriodUs = period;
  lastUs = now;
  if (pulseCnt == 0) firstUs = now;
  pulseCnt++;
  lastPulseMs = millis();
}

// ---------- RPM算出（パルス間隔ベース） ----------
void updateRpm() {
  noInterrupts();
  uint32_t cnt = pulseCnt;
  uint32_t t0 = firstUs;
  uint32_t t1 = lastUs;
  uint32_t lp = lastPulseMs;
  pulseCnt = 0;
  interrupts();

  if (cnt >= 2 && t1 != t0) {
    // (n-1)区間の合計時間から平均回転数を求める（1回転1パルス）
    rpm = (float)(cnt - 1) * 60000000.0f / (float)(t1 - t0);
  } else if (millis() - lp > RUN_STALL_MS) {
    rpm = 0;
  }
  // それ以外（低回転でパルスが1つ以下）は前回値を保持
}

// ---------- ロック判定 ----------
bool isStalled() {
  if (stepState == 0) return false;                               // 停止指令中は対象外
  if (millis() - stateChangedMs < SPINUP_GRACE_MS) return false;  // 始動待ち
  noInterrupts();
  uint32_t lp = lastPulseMs;
  uint32_t period = lastPeriodUs;
  interrupts();
  if (millis() - lp > RUN_STALL_MS) return true;  // パルスが途絶えた
  if (period > MAX_PERIOD_US) return true;        // 回転が極端に遅い（ガクガク）
  return false;
}

// ---------- ESC出力更新 ----------
void applyStateChange() {
  switch (stepState) {
    case 0: esc.writeMicroseconds(PULSE_OFF); break;
    case 1: esc.writeMicroseconds(PULSE_LOW); break;
    case 2: esc.writeMicroseconds(PULSE_MED); break;
    case 3: esc.writeMicroseconds(PULSE_HIGH); break;
  }
  stateChangedMs = millis();
}

void setup() {
  Serial.begin(115200);

  pinMode(RGB_PWR, OUTPUT);
  digitalWrite(RGB_PWR, HIGH);
  pixels.begin();
  pixels.setBrightness(40);

  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT);  // 外付け6kΩでプルアップ済み
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), sensorIsr, FALLING);

  esc.attach(ESC_PIN, 900, 2000);

  // 【初期化：赤色点滅】ESCにも停止信号を送り続ける
  for (int i = 0; i < 4; i++) {
    setColor(255, 0, 0);
    esc.writeMicroseconds(PULSE_OFF);
    delay(250);
    setColor(0, 0, 0);
    delay(250);
  }

  lastPulseMs = millis();
  applyStateChange();  // 初期状態: 停止
}

void loop() {
  uint32_t now = millis();

  // --- スイッチ入力 ---
  bool currentSwState = digitalRead(SW_PIN);
  if (lastSwState == HIGH && currentSwState == LOW) {
    if (stallLatched) {
      // ロック停止後：まず緑のスタンバイ（停止）に戻す。次に押すと弱から始まる
      stallLatched = false;
      stepState = 0;
    } else {
      stepState = (stepState + 1) % 4;
    }
    applyStateChange();
    delay(50);  // チャタリング防止
  }
  lastSwState = currentSwState;

  // --- ロック検知：ESC停止を最優先し、即座に赤表示 ---
  if (!stallLatched && isStalled()) {
    int lockedState = stepState;
    stepState = 0;
    applyStateChange();
    stallLatched = true;
    setColor(255, 0, 0);
    Serial.print("[WARNING] Motor LOCKED at state ");
    Serial.print(lockedState);
    Serial.println(" -> ESC OFF. Press button to return to STANDBY.");
  }

  // --- RPM算出・表示 ---
  if (now - lastReportMs >= REPORT_MS) {
    lastReportMs = now;
    updateRpm();
    Serial.print("State: ");
    Serial.print(stepState);
    Serial.print("  RPM: ");
    Serial.println(rpm, 0);
  }

  // --- LEDアニメーション ---
  if (stallLatched) {
    // ロック停止：赤色高速パルス
    if ((now / 150) % 2 == 0) setColor(255, 0, 0);
    else setColor(0, 0, 0);
  } else {
    switch (stepState) {
      case 0:  // 停止：緑色常時点灯
        setColor(0, 255, 0);
        break;
      case 1:  // 弱：1秒に1回点滅（点灯100ms／消灯900msで消えている方が長い）
        if ((now % 1000) < 100) setColor(0, 0, 255);
        else setColor(0, 0, 0);
        break;
      case 2:
        {  // 中：ヘリのライト風（高速2回パルス→消灯、1秒周期）
          uint32_t t = now % 1000;
          if (t < 80 || (t >= 160 && t < 240)) setColor(0, 0, 255);  // 点灯80ms・間80ms・点灯80ms
          else setColor(0, 0, 0);                                    // 残りは消灯
          break;
        }
      case 3:  // 強：青色常時点灯
        setColor(0, 0, 255);
        break;
    }
  }

  delay(10);
}

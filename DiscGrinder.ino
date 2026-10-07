// Xiao RP2040
// 2204/260KV
// DC12V-1A

#include "Servo.h"
#include "Adafruit_NeoPixel.h"

const int ESC_PIN = D0;  // GP0 (D0) ➔ ESC信号線
const int SW_PIN = D1;   // GP1 (D1) ➔ タクトスイッチ

// XIAO RP2040 オンボードRGB LEDの設定
#define RGB_PWR 11  // RGB LED Power
#define RGB_PIN 12  // RGB LED Data
#define NUMPIXELS 1

Adafruit_NeoPixel pixels(NUMPIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);
Servo esc;

// 状態管理: 0=停止, 1=弱, 2=中, 3=強
int stepState = 0;
bool lastSwState = HIGH;

// 各段階のパルス幅 (us)
const int PULSE_OFF  = 900;
const int PULSE_LOW  = 1263;  // (2000-900)/3 * 1 + 900
const int PULSE_MED  = 1630;  // (2000-900)/3 * 2 + 900
const int PULSE_HIGH = 2000;

// 色設定ヘルパー
void setColor(byte r, byte g, byte b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void setup() {
  pinMode(RGB_PWR, OUTPUT);
  digitalWrite(RGB_PWR, HIGH);
  pixels.begin();
  pixels.setBrightness(40); // 最大輝度（パターン制御側で調整）

  pinMode(SW_PIN, INPUT_PULLUP);
  esc.attach(ESC_PIN, 900, 2000);

  // 【初期化：赤色点滅】
  for (int i = 0; i < 4; i++) {
    setColor(255, 0, 0);
    esc.writeMicroseconds(PULSE_OFF);
    delay(250);
    setColor(0, 0, 0);
    delay(250);
  }

  // 初期状態: 停止
  applyStateChange();
}

// 状態が切り替わった瞬間にモータ出力を更新
void applyStateChange() {
  switch (stepState) {
    case 0: esc.writeMicroseconds(PULSE_OFF); break;
    case 1: esc.writeMicroseconds(PULSE_LOW); break;
    case 2: esc.writeMicroseconds(PULSE_MED); break;
    case 3: esc.writeMicroseconds(PULSE_HIGH); break;
  }
}

void loop() {
  // --- スイッチ入力検知 ---
  bool currentSwState = digitalRead(SW_PIN);
  if (lastSwState == HIGH && currentSwState == LOW) {
    stepState = (stepState + 1) % 4;
    applyStateChange();
    delay(50);  // チャタリング防止
  }
  lastSwState = currentSwState;

  // --- LEDアニメーション制御 (ノンブロッキング) ---
  unsigned long currentMillis = millis();

  switch (stepState) {
    case 0: // 停止：緑色常時点灯
      setColor(0, 255, 0);
      break;

    case 1: { // 弱：ホタルのような柔らかな青色ブリージング（サイン波風）
      // 3000ms周期で明るさを0〜255の間で滑らかに変化
      float angle = (currentMillis % 3000) * (2.0 * 3.14159 / 3000.0);
      byte brightness = (sin(angle - 1.5708) + 1.0) / 2.0 * 200 + 10; // 10〜210
      setColor(0, 0, brightness);
      break;
    }

    case 2: // 中：1秒周期フラッシュ（0.5秒ON / 0.5秒OFF）
      if ((currentMillis / 500) % 2 == 0) {
        setColor(0, 0, 255); // 青点灯
      } else {
        setColor(0, 0, 0);   // 消灯
      }
      break;

    case 3: // 強：青色常時点灯
      setColor(0, 0, 255);
      break;
  }

  delay(10); // ループ周期
}

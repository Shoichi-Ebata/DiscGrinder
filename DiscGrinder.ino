//Xiao RP2040
//2204/260KV
//DC12V-1A


#include "Servo.h"
#include "Adafruit_NeoPixel.h"

const int ESC_PIN = D0;  // GP0 (D0) ピン ➔ ESCの信号線(S)へ
const int SW_PIN = D1;   // GP1 (D1) ピン ➔ タクトスイッチへ

// XIAO RP2040 オンボードRGB LEDの設定
#define RGB_PWR 11  // RGB LEDの電源ピン (GP11)
#define RGB_PIN 12  // RGB LEDのデータピン (GP12)
#define NUMPIXELS 1

Adafruit_NeoPixel pixels(NUMPIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

Servo esc;

bool isRunning = false;
bool lastSwState = HIGH;

// 色設定関数（RGB順/明るさ調整済み）
void setColor(byte r, byte g, byte b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void setup() {
  // RGB LEDの電源有効化と初期化
  pinMode(RGB_PWR, OUTPUT);
  digitalWrite(RGB_PWR, HIGH);  // RGB LEDへ給電ON
  pixels.begin();
  pixels.setBrightness(30);  // 明るさを抑制（眩しさ防止）

  pinMode(SW_PIN, INPUT_PULLUP);

  // 下限を 900us 〜 上限 2000us に広げて割り当て
  esc.attach(ESC_PIN, 900, 2000);

  // 【フェーズ1：起動中（赤色点滅）】
  // 約2秒間の初期化待ち中に赤色を点滅（0.25秒×4回）
  for (int i = 0; i < 4; i++) {
    setColor(255, 0, 0);         // 赤点灯
    esc.writeMicroseconds(900);  // ロック解除のため900usを継続送信
    delay(250);
    setColor(0, 0, 0);  // 消灯
    delay(250);
  }

  // 【フェーズ2：準備OK（緑色点灯）】
  setColor(0, 255, 0);  // 緑点灯（待機中）
}

void loop() {
  bool currentSwState = digitalRead(SW_PIN);

  if (lastSwState == HIGH && currentSwState == LOW) {
    isRunning = !isRunning;

    if (isRunning) {
      // 【フェーズ3：動作中（青色点灯）】
      setColor(0, 0, 255);          // 青点灯
      esc.writeMicroseconds(2000);  // 100% 全開
    } else {
      // 【フェーズ2に戻る：準備OK（緑色点灯）】
      setColor(0, 255, 0);         // 緑点灯
      esc.writeMicroseconds(900);  // 停止
    }

    delay(50);  // チャタリング防止
  }

  lastSwState = currentSwState;
  delay(10);
}

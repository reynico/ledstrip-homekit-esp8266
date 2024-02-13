#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "wifi_info.h"
#include "AiEsp32RotaryEncoder.h"

#define LOG_D(fmt, ...) printf_P(PSTR(fmt "\n"), ##__VA_ARGS__);

bool received_sat = false;
bool received_hue = false;
bool received_brightness = false;

// RGB Led Strip outputs
int r_pin = 13;  // Wemos - D7
int g_pin = 12;  // Wemos - D6
int b_pin = 14;  // Wemos - D5

// Rotary encoder inputs
int clk_pin = 2;  // Wemos - D4
int dt_pin = 0;   // Wemos - D3
int sw_pin = 4;   // Wemos - D2
int steps = 8;

// Local states
bool is_on = false;
float current_brightness = 100.0;
float current_sat = 0.0;
float current_hue = 0.0;
int rgb_colors[3];

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(clk_pin, dt_pin, sw_pin, -1, steps);

void IRAM_ATTR readEncoderISR() {
  rotaryEncoder.readEncoder_ISR();
}

void setup() {
  Serial.begin(115200);
  wifi_connect();  // in wifi_info.h

  pinMode(r_pin, OUTPUT);
  pinMode(g_pin, OUTPUT);
  pinMode(b_pin, OUTPUT);

  pinMode(clk_pin, INPUT);
  pinMode(dt_pin, INPUT);
  pinMode(sw_pin, INPUT);

  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  rotaryEncoder.setBoundaries(0, 100, false);
  rotaryEncoder.disableAcceleration();
  rotaryEncoder.setEncoderValue(current_brightness);

  rgb_colors[0] = 255;
  rgb_colors[1] = 255;
  rgb_colors[2] = 255;
  analogWrite(r_pin, 255);
  analogWrite(g_pin, 255);
  analogWrite(b_pin, 255);
  my_homekit_setup();
}

void rotary_loop() {
  if (rotaryEncoder.encoderChanged()) {
    current_brightness = rotaryEncoder.readEncoder();
    received_brightness = true;
    homekit_value_t v;
    v.int_value = current_brightness;
    set_bright(v);
  }
  // handle_rotary_button();
}

void loop() {
  rotary_loop();
  my_homekit_loop();
  delay(10);
}

//==============================
// HomeKit setup and loop
//==============================

// access your HomeKit characteristics defined in my_accessory.c

extern "C" homekit_server_config_t accessory_config;
extern "C" homekit_characteristic_t cha_on;
extern "C" homekit_characteristic_t cha_bright;
extern "C" homekit_characteristic_t cha_sat;
extern "C" homekit_characteristic_t cha_hue;

static uint32_t next_heap_millis = 0;

void my_homekit_setup() {

  cha_on.setter = set_on;
  cha_bright.setter = set_bright;
  cha_sat.setter = set_sat;
  cha_hue.setter = set_hue;

  arduino_homekit_setup(&accessory_config);

  // report the switch value to HomeKit if it is changed (e.g. by a physical button)
  cha_bright.value.int_value = current_brightness;
  homekit_characteristic_notify(&cha_bright, cha_bright.value);
}

void my_homekit_loop() {
  arduino_homekit_loop();
  const uint32_t t = millis();
  if (t > next_heap_millis) {
    // show heap info every 5 seconds
    next_heap_millis = t + 5 * 1000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
          ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
  }
}

void set_on(const homekit_value_t v) {
  bool on = v.bool_value;
  cha_on.value.bool_value = on;  // sync the value

  if (on) {
    is_on = true;
    LOG_D("Lamp is on");
  } else {
    is_on = false;
    LOG_D("Lamp is off");
  }

  updateColor();
}

void set_hue(const homekit_value_t v) {
  float hue = v.float_value;
  cha_hue.value.float_value = hue;  // sync the value
  LOG_D("Set HUE to %.1f", hue);

  current_hue = hue;
  received_hue = true;

  updateColor();
}

void set_sat(const homekit_value_t v) {
  float sat = v.float_value;
  cha_sat.value.float_value = sat;  // sync the value
  LOG_D("Set saturation to %.1f", sat);

  current_sat = sat;
  received_sat = true;

  updateColor();
}

void set_bright(const homekit_value_t v) {
  int bright = v.int_value;
  cha_bright.value.int_value = bright;  // sync the value
  LOG_D("Set brightness to %d", bright);

  current_brightness = bright;
  rotaryEncoder.setEncoderValue(current_brightness);
  homekit_characteristic_notify(&cha_bright, cha_bright.value);  // sync the value
  received_brightness = true;

  updateColor();
}

void updateColor() {
  if (is_on) {
    if (received_hue && received_sat || received_brightness) {
      HSV2RGB(current_hue, current_sat, current_brightness);
      received_hue = false;
      received_sat = false;
      received_brightness = false;
    }

    LOG_D("Update color to rgb(%d, %d, %d)", rgb_colors[0], rgb_colors[1], rgb_colors[2]);
    analogWrite(r_pin, 255 - rgb_colors[0]);
    analogWrite(g_pin, 255 - rgb_colors[1]);
    analogWrite(b_pin, 255 - rgb_colors[2]);
  } else if (!is_on) {  // lamp - switch to off
    LOG_D("Lamp is powered off");
    analogWrite(r_pin, 255 - 0);
    analogWrite(g_pin, 255 - 0);
    analogWrite(b_pin, 255 - 0);
  }
}

void HSV2RGB(float h, float s, float v) {
  int i;
  float m, n, f;

  s /= 100;
  v /= 100;

  if (s == 0) {
    rgb_colors[0] = rgb_colors[1] = rgb_colors[2] = round(v * 255);
    return;
  }

  h /= 60;
  i = floor(h);
  f = h - i;

  if (!(i & 1)) {
    f = 1 - f;
  }

  m = v * (1 - s);
  n = v * (1 - s * f);

  switch (i) {
    case 0:
    case 6:
      rgb_colors[0] = round(v * 255);
      rgb_colors[1] = round(n * 255);
      rgb_colors[2] = round(m * 255);
      break;

    case 1:
      rgb_colors[0] = round(n * 255);
      rgb_colors[1] = round(v * 255);
      rgb_colors[2] = round(m * 255);
      break;

    case 2:
      rgb_colors[0] = round(m * 255);
      rgb_colors[1] = round(v * 255);
      rgb_colors[2] = round(n * 255);
      break;

    case 3:
      rgb_colors[0] = round(m * 255);
      rgb_colors[1] = round(n * 255);
      rgb_colors[2] = round(v * 255);
      break;

    case 4:
      rgb_colors[0] = round(n * 255);
      rgb_colors[1] = round(m * 255);
      rgb_colors[2] = round(v * 255);
      break;

    case 5:
      rgb_colors[0] = round(v * 255);
      rgb_colors[1] = round(m * 255);
      rgb_colors[2] = round(n * 255);
      break;
  }
}
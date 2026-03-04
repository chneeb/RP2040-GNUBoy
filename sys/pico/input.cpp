#include <stdio.h>

#include "rc.h"

#include "input.h"
#include "input.hpp"
#include "sys.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "pico/binary_info.h"

// NES Mini Classic controller via I2C1 on GPIO26 (SDA) / GPIO27 (SCL)
#define NUNCHUCK_I2C  i2c1
#define NUNCHUCK_SDA  26
#define NUNCHUCK_SCL  27
#define NUNCHUCK_ADDR 0x52

static bool nunchuck_up, nunchuck_down, nunchuck_left, nunchuck_right;
static bool nunchuck_btn_a, nunchuck_btn_b, nunchuck_btn_start, nunchuck_btn_select;

static void nunchuck_init() {
  i2c_init(NUNCHUCK_I2C, 100000);
  gpio_set_function(NUNCHUCK_SDA, GPIO_FUNC_I2C);
  gpio_set_function(NUNCHUCK_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(NUNCHUCK_SDA);
  gpio_pull_up(NUNCHUCK_SCL);

  uint8_t buf[2] = {0xF0, 0x55};
  i2c_write_blocking(NUNCHUCK_I2C, NUNCHUCK_ADDR, buf, 2, false);
  sleep_ms(1);
  buf[0] = 0xFB; buf[1] = 0x00;
  i2c_write_blocking(NUNCHUCK_I2C, NUNCHUCK_ADDR, buf, 2, false);
  sleep_ms(1);
  buf[0] = 0xFE; buf[1] = 0x03;
  i2c_write_blocking(NUNCHUCK_I2C, NUNCHUCK_ADDR, buf, 2, false);
  sleep_ms(100);


}

static bool nunchuck_read(uint8_t *data) {
  uint8_t req = 0x00;
  if (i2c_write_blocking(NUNCHUCK_I2C, NUNCHUCK_ADDR, &req, 1, false) < 0)
    return false;
  sleep_us(200);
  return i2c_read_blocking(NUNCHUCK_I2C, NUNCHUCK_ADDR, data, 8, false) == 8;
}

static void nunchuck_post(bool &state, int code, bool new_state) {
  if (state != new_state) {
    event_t ev;
    ev.type = new_state ? EV_PRESS : EV_RELEASE;
    ev.code = code;
    ev_postevent(&ev);
    state = new_state;
  }
}

static void update_nunchuck() {
  static uint32_t last_poll = 0;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (now - last_poll < 16)
    return;
  last_poll = now;

  uint8_t data[8];
  if (!nunchuck_read(data))
    return;

  // 8-bit format: bytes 0-5 are joystick/axis data (unused for NES Mini),
  // buttons are active low in bytes 6 and 7
  nunchuck_post(nunchuck_up,         K_JOYUP,    !(data[7] & 0x01));
  nunchuck_post(nunchuck_down,       K_JOYDOWN,  !(data[6] & 0x40));
  nunchuck_post(nunchuck_left,       K_JOYLEFT,  !(data[7] & 0x02));
  nunchuck_post(nunchuck_right,      K_JOYRIGHT, !(data[6] & 0x80));
  nunchuck_post(nunchuck_btn_b,      K_JOY0,     !(data[7] & 0x40));
  nunchuck_post(nunchuck_btn_a,      K_JOY1,     !(data[7] & 0x10));
  nunchuck_post(nunchuck_btn_select, K_JOY2,     !(data[6] & 0x10));
  nunchuck_post(nunchuck_btn_start,  K_JOY3,     !(data[6] & 0x04));
}

rcvar_t joy_exports[] =
{
	RCV_END
};

static bool left, right, up, down, a_btn, b_btn, x_btn, y_btn; 

enum class ButtonIO {
#ifdef PIMORONI_PICOSYSTEM
  UP = 23,
  DOWN = 20,
  LEFT = 22,
  RIGHT = 21,

  A = 18,
  B = 19,
  X = 17,
  Y = 16,
#else
  // UP = 2,
  // DOWN = 3,
  // LEFT = 4,
  // RIGHT = 5,

  // A = 12,
  // B = 13,
  // X = 14,
  // Y = 15,

  UP = 2,
  DOWN = 18,
  LEFT = 16,
  RIGHT = 20,

  A = 15,
  B = 17,
  X = 19,
  Y = 21,
#endif
};

static void init_button(ButtonIO b) {
  int gpio = static_cast<int>(b);
  gpio_set_function(gpio, GPIO_FUNC_SIO);
  gpio_set_dir(gpio, GPIO_IN);
  gpio_pull_up(gpio);
}

inline bool get_button(ButtonIO b) {
  return !gpio_get(static_cast<int>(b));
}

void update_button(ButtonIO b, int code, bool& state) {
  bool new_state = get_button(b);
  if (state != new_state) {
    event_t ev;
    ev.type = new_state ? EV_PRESS : EV_RELEASE;
    ev.code = code;
    printf("Button %03x %s\n", code, new_state ? "up" : "down");
    ev_postevent(&ev);
    state = new_state;
  }
}

void init_input() {
  nunchuck_init();

  init_button(ButtonIO::UP);
  init_button(ButtonIO::DOWN);
  init_button(ButtonIO::LEFT);
  init_button(ButtonIO::RIGHT);
  init_button(ButtonIO::A);
  init_button(ButtonIO::B);
  init_button(ButtonIO::X);
  init_button(ButtonIO::Y);

  #define BUTTON_DECL(btn) bi_decl(bi_1pin_with_name(static_cast<int>(ButtonIO::btn), #btn" Button"));
  BUTTON_DECL(UP)
  BUTTON_DECL(DOWN)
  BUTTON_DECL(LEFT)
  BUTTON_DECL(RIGHT)
  BUTTON_DECL(A)
  BUTTON_DECL(B)
  BUTTON_DECL(X)
  BUTTON_DECL(Y)
  #undef BUTTON_DECL
}

void update_input() {
  update_button(ButtonIO::LEFT,   K_JOYLEFT,  left  );
  update_button(ButtonIO::RIGHT,  K_JOYRIGHT, right );
  update_button(ButtonIO::UP,     K_JOYUP,    up    );
  update_button(ButtonIO::DOWN,   K_JOYDOWN,  down  );

  update_button(ButtonIO::A, K_JOY1, a_btn);
  update_button(ButtonIO::B, K_JOY0, b_btn);
  update_button(ButtonIO::X, K_JOY2, x_btn);
  update_button(ButtonIO::Y, K_JOY3, y_btn);
}

extern "C" void ev_poll()
{
  update_input();
  update_nunchuck();
}

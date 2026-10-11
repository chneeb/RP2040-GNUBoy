#include <stdio.h>
#include <string.h>

#include "rc.h"

#include "input.h"
#include "input.hpp"
#include "sys.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include "pico/binary_info.h"

// NES Mini Classic controller via I2C0 on GPIO4 (SDA) / GPIO5 (SCL), the same
// pins as rp2040-ili9341-infones on the ResTouch (GP26/27 are taken by an I2S DAC)
#define NUNCHUCK_I2C  i2c0
#define NUNCHUCK_SDA  4
#define NUNCHUCK_SCL  5
#define NUNCHUCK_ADDR 0x52

static bool nunchuck_up, nunchuck_down, nunchuck_left, nunchuck_right;
static bool nunchuck_btn_a, nunchuck_btn_b, nunchuck_btn_start, nunchuck_btn_select;

#define NUNCHUCK_I2C_HZ 100000

// Every transfer gives up after this, so a misbehaving device on the bus can't
// hang the emulator (8 bytes at 100kHz take under 1ms)
#define NUNCHUCK_TIMEOUT_US 10000

// A reset in the middle of a read leaves the device that was sending (e.g. a
// CardKB that stays powered on the same bus) holding SDA low. Clock SCL until
// it lets go, then send a STOP.
static void i2c_bus_recover() {
  gpio_init(NUNCHUCK_SDA);
  gpio_init(NUNCHUCK_SCL);
  gpio_pull_up(NUNCHUCK_SDA);
  gpio_pull_up(NUNCHUCK_SCL);
  // Open drain: only ever drive low, let the pull-ups take the line high
  gpio_set_dir(NUNCHUCK_SDA, GPIO_IN);
  gpio_set_dir(NUNCHUCK_SCL, GPIO_IN);
  gpio_put(NUNCHUCK_SDA, 0);
  gpio_put(NUNCHUCK_SCL, 0);
  sleep_us(10);

  for (int i = 0; i < 9 && !gpio_get(NUNCHUCK_SDA); i++) {
    gpio_set_dir(NUNCHUCK_SCL, GPIO_OUT);
    sleep_us(5);
    gpio_set_dir(NUNCHUCK_SCL, GPIO_IN);
    sleep_us(5);
  }

  // STOP: SDA low -> high while SCL is high
  gpio_set_dir(NUNCHUCK_SDA, GPIO_OUT);
  sleep_us(5);
  gpio_set_dir(NUNCHUCK_SDA, GPIO_IN);
  sleep_us(5);
}

// True once the controller accepted the init sequence. Until then (or after a
// failed read) nunchuck_read() repeats the init every NUNCHUCK_RETRY_MS, so a
// controller that missed it at startup, or is plugged in later, still works.
static bool nunchuck_ready = false;
#define NUNCHUCK_RETRY_MS 500

// The LCD's DMA interrupt (DMA_IRQ_0, once per scanline with the 1.5x scaling)
// is held back while the init sequence is sent. With it running, the
// controller refused most init writes. Held back, the LCD waits at the end of
// the current line and goes on afterwards.
static void lcd_irq_hold(bool hold) {
  irq_set_enabled(DMA_IRQ_0, !hold);
}

// A refused write is repeated a few times before the init gives up
#define NUNCHUCK_WRITE_TRIES 5

static bool nunchuck_write2(uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  for (int i = 0; i < NUNCHUCK_WRITE_TRIES; i++) {
    int rc = i2c_write_timeout_us(NUNCHUCK_I2C, NUNCHUCK_ADDR, buf, 2, false, NUNCHUCK_TIMEOUT_US);
    if (rc == 2)
      return true;
#if NUNCHUCK_DEBUG
    printf("nunchuck write %02x %02x: %d\n", reg, value, rc);
#endif
    sleep_ms(10);
  }
  return false;
}

// Unencrypted init, then data format 3 (8 byte reports, buttons in bytes 6
// and 7). The controller needs time between the writes; 1 ms was too short
// for some of them, and a missed write left it in another format for good.
static void nunchuck_init() {
  i2c_bus_recover();
  i2c_init(NUNCHUCK_I2C, NUNCHUCK_I2C_HZ);
  gpio_set_function(NUNCHUCK_SDA, GPIO_FUNC_I2C);
  gpio_set_function(NUNCHUCK_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(NUNCHUCK_SDA);
  gpio_pull_up(NUNCHUCK_SCL);

  lcd_irq_hold(true);
  nunchuck_ready = false;
  if (nunchuck_write2(0xF0, 0x55)) {
    sleep_ms(10);
    if (nunchuck_write2(0xFB, 0x00)) {
      sleep_ms(10);
      nunchuck_ready = nunchuck_write2(0xFE, 0x03);
    }
  }
  lcd_irq_hold(false);
#if NUNCHUCK_DEBUG
  printf("nunchuck init %s\n", nunchuck_ready ? "done" : "failed");
#endif
}

static bool nunchuck_read(uint8_t *data) {
  if (!nunchuck_ready) {
    static uint32_t retry_at = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now - retry_at) < 0)
      return false;
    retry_at = now + NUNCHUCK_RETRY_MS;
    nunchuck_init();
    if (!nunchuck_ready)
      return false;
  }
  uint8_t req = 0x00;
  bool ok = i2c_write_timeout_us(NUNCHUCK_I2C, NUNCHUCK_ADDR, &req, 1, false, NUNCHUCK_TIMEOUT_US) == 1;
  if (ok) {
    sleep_us(200);
    ok = i2c_read_timeout_us(NUNCHUCK_I2C, NUNCHUCK_ADDR, data, 8, false, NUNCHUCK_TIMEOUT_US) == 8;
  }
  if (!ok) {
    nunchuck_ready = false;
#if NUNCHUCK_DEBUG
    printf("nunchuck read failed\n");
#endif
  }
  return ok;
}

#if NUNCHUCK_DEBUG
// Counted in RAM and printed every 2 s, so the output doesn't change the
// timing around a button press
static unsigned dbg_reads, dbg_read_fails, dbg_up_reads, dbg_up_events, dbg_dropped;
static uint8_t dbg_up_raw[8];
#endif

static void nunchuck_post(bool &state, int code, bool new_state) {
  if (state != new_state) {
    event_t ev;
    ev.type = new_state ? EV_PRESS : EV_RELEASE;
    ev.code = code;
    int posted = ev_postevent(&ev);
#if NUNCHUCK_DEBUG
    if (!posted) dbg_dropped++;
    if (code == K_JOYUP && new_state) dbg_up_events++;
#else
    (void)posted;
#endif
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
  bool ok = nunchuck_read(data);

#if NUNCHUCK_DEBUG
  if (ok) {
    dbg_reads++;
    if (!(data[7] & 0x01)) {
      dbg_up_reads++;
      memcpy(dbg_up_raw, data, sizeof dbg_up_raw);
    }
  } else {
    dbg_read_fails++;
  }
  static uint32_t dbg_print_at = 0;
  if ((int32_t)(now - dbg_print_at) >= 0) {
    dbg_print_at = now + 2000;
    printf("nunchuck: %u reads, %u failed, %u with Up, %u Up presses posted, %u events dropped",
           dbg_reads, dbg_read_fails, dbg_up_reads, dbg_up_events, dbg_dropped);
    if (dbg_up_reads)
      printf(" | last Up read %02x %02x %02x %02x %02x %02x b6 %02x b7 %02x", dbg_up_raw[0],
             dbg_up_raw[1], dbg_up_raw[2], dbg_up_raw[3], dbg_up_raw[4], dbg_up_raw[5],
             dbg_up_raw[6], dbg_up_raw[7]);
    printf("\n");
  }
#endif
  if (!ok)
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

// Buttons wired straight to GPIOs. Off by default: on the ResTouch these pins
// carry the LCD reset, the touch controller and the SD card's SDIO lines, which
// showed up as spurious presses next to the NES Mini controller.
#ifndef INPUT_GPIO_BUTTONS
#define INPUT_GPIO_BUTTONS 0
#endif

#if INPUT_GPIO_BUTTONS
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

#endif

void init_input() {
  nunchuck_init();

#if INPUT_GPIO_BUTTONS
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
#endif
}

#if INPUT_GPIO_BUTTONS
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
#endif

extern "C" void ev_poll()
{
#if INPUT_GPIO_BUTTONS
  update_input();
#endif
  update_nunchuck();
}

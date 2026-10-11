// NES Mini controller test for the ResTouch (i2c0, SDA GP4, SCL GP5), no LCD.
// Looks for a data format or read timing without the Left -> Up blips: cycles
// every 20 s through the configurations below, prints the button bytes when
// they change, and after each phase the totals of all configurations so far.
// Press only Left (as in a game) and compare "Up+Left" between them.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define SDA 4
#define SCL 5
#define BUS i2c0
#define T_US 5000
#define PHASE_MS 20000
#define POLL_MS 16

typedef struct {
  const char *name;
  uint8_t format;     // value written to register 0xFE
  uint8_t len;        // bytes per report
  uint8_t btn;        // first of the two button bytes; the second is btn+1
  unsigned delay_us;  // between the request and the read
  bool pipelined;     // read the answer to the previous poll's request
} config_t;

// The button bytes have the same layout in formats 1 and 3: first byte
// Right 0x80, Down 0x40, Select 0x10, Start 0x04; second byte B 0x40,
// A 0x10, Left 0x02, Up 0x01, all active low.
static const config_t configs[] = {
  {"format 3, 200 us (current)", 3, 8, 6, 200, false},
  {"format 3, 1000 us", 3, 8, 6, 1000, false},
  {"format 3, pipelined", 3, 8, 6, 0, true},
  // Format 1 isn't in the list: the NES Mini clones answer it with format 3's
  // first 6 bytes (81 81 81 81 00 00), so every button reads as pressed.
};
#define NCONFIGS (sizeof configs / sizeof configs[0])

typedef struct {
  unsigned reads, fails, left, up_left, up_only;
} totals_t;
static totals_t totals[NCONFIGS];

static bool write2(uint8_t reg, uint8_t value)
{
  uint8_t buf[2] = {reg, value};
  for (int i = 0; i < 5; i++) {
    if (i2c_write_timeout_us(BUS, 0x52, buf, 2, false, T_US) == 2)
      return true;
    sleep_ms(10);
  }
  return false;
}

static bool init_pad(uint8_t format)
{
  i2c_init(BUS, 100 * 1000);
  gpio_set_function(SDA, GPIO_FUNC_I2C); gpio_set_function(SCL, GPIO_FUNC_I2C);
  gpio_pull_up(SDA); gpio_pull_up(SCL);
  bool ok = write2(0xF0, 0x55);
  sleep_ms(10);
  ok = ok && write2(0xFB, 0x00);
  sleep_ms(10);
  ok = ok && write2(0xFE, format);
  sleep_ms(10);
  return ok;
}

static bool request(void)
{
  uint8_t r = 0;
  return i2c_write_timeout_us(BUS, 0x52, &r, 1, false, T_US) == 1;
}

static const char *names(uint8_t b1, uint8_t b2)
{
  static char s[64];
  s[0] = 0;
  if (!(b2 & 0x01)) strcat(s, " UP");
  if (!(b1 & 0x40)) strcat(s, " DOWN");
  if (!(b2 & 0x02)) strcat(s, " LEFT");
  if (!(b1 & 0x80)) strcat(s, " RIGHT");
  if (!(b2 & 0x10)) strcat(s, " A");
  if (!(b2 & 0x40)) strcat(s, " B");
  if (!(b1 & 0x10)) strcat(s, " SELECT");
  if (!(b1 & 0x04)) strcat(s, " START");
  return s;
}

int main(void)
{
  stdio_init_all();
  sleep_ms(3000);
  for (unsigned phase = 0;; phase++) {
    unsigned c = phase % NCONFIGS;
    const config_t *cfg = &configs[c];
    totals_t *t = &totals[c];
    bool ok = init_pad(cfg->format);
    printf("\n=== %s, init %s\n", cfg->name, ok ? "ok" : "FAILED");

    uint8_t buf[8], last[8];
    memset(last, 0xEE, sizeof last);
    bool pending = cfg->pipelined && request();
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < PHASE_MS) {
      bool good;
      if (cfg->pipelined) {
        good = pending && i2c_read_timeout_us(BUS, 0x52, buf, cfg->len, false, T_US) == cfg->len;
        pending = request();
      } else {
        good = request();
        sleep_us(cfg->delay_us);
        good = good && i2c_read_timeout_us(BUS, 0x52, buf, cfg->len, false, T_US) == cfg->len;
      }
      if (!good) {
        t->fails++;
      } else {
        uint8_t b1 = buf[cfg->btn], b2 = buf[cfg->btn + 1];
        bool up = !(b2 & 0x01), left = !(b2 & 0x02);
        t->reads++;
        if (left) t->left++;
        if (up && left) t->up_left++;
        if (up && !left) t->up_only++;
        if (memcmp(buf, last, cfg->len)) {
          printf("%6lu ms ", (unsigned long)to_ms_since_boot(get_absolute_time()));
          for (int i = 0; i < cfg->len; i++) printf(" %02x", buf[i]);
          printf(" %s\n", names(b1, b2));
          memcpy(last, buf, cfg->len);
        }
      }
      sleep_ms(POLL_MS);
    }
    i2c_deinit(BUS);

    printf("=== totals: reads, failed, with Left, Up+Left, Up alone\n");
    for (unsigned i = 0; i < NCONFIGS; i++)
      printf("    %-28s %6u %4u %6u %5u %5u\n", configs[i].name, totals[i].reads,
             totals[i].fails, totals[i].left, totals[i].up_left, totals[i].up_only);
  }
}

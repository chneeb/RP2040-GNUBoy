// NES Mini controller test for the ResTouch (i2c0, SDA GP4, SCL GP5), no LCD.
// Polls every 16 ms like GNUBoy and prints buttons bytes b6/b7 whenever they
// change. The I2C speed alternates every 20 s between 100 kHz and 20 kHz.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define SDA 4
#define SCL 5
#define BUS i2c0
#define T_US 5000

static bool init_pad(unsigned hz)
{
  i2c_init(BUS, hz);
  gpio_set_function(SDA, GPIO_FUNC_I2C); gpio_set_function(SCL, GPIO_FUNC_I2C);
  gpio_pull_up(SDA); gpio_pull_up(SCL);
  uint8_t i1[] = {0xF0, 0x55}, i2[] = {0xFB, 0x00}, i3[] = {0xFE, 0x03};
  bool ok = i2c_write_timeout_us(BUS, 0x52, i1, 2, false, T_US) == 2;
  sleep_ms(10);
  ok = ok && i2c_write_timeout_us(BUS, 0x52, i2, 2, false, T_US) == 2;
  sleep_ms(10);
  ok = ok && i2c_write_timeout_us(BUS, 0x52, i3, 2, false, T_US) == 2;
  sleep_ms(10);
  return ok;
}

static const char *names(uint8_t b6, uint8_t b7)
{
  static char s[64];
  s[0] = 0;
  if (!(b7 & 0x01)) strcat(s, " UP");
  if (!(b6 & 0x40)) strcat(s, " DOWN");
  if (!(b7 & 0x02)) strcat(s, " LEFT");
  if (!(b6 & 0x80)) strcat(s, " RIGHT");
  if (!(b7 & 0x10)) strcat(s, " A");
  if (!(b7 & 0x40)) strcat(s, " B");
  if (!(b6 & 0x10)) strcat(s, " SELECT");
  if (!(b6 & 0x04)) strcat(s, " START");
  return s;
}


int main(void)
{
  stdio_init_all();
  sleep_ms(3000);
  // Totals over all phases, [0] = 100 kHz, [1] = 20 kHz
  unsigned total_reads[2] = {0, 0}, total_up[2] = {0, 0}, total_left[2] = {0, 0};
  for (int phase = 0;; phase++) {
    unsigned hz = (phase & 1) ? 20000 : 100000;
    bool ok = init_pad(hz);
    printf("\n=== %u kHz, init %s\n", hz / 1000, ok ? "ok" : "FAILED");
    uint8_t last6 = 0xff, last7 = 0xff;
    unsigned reads = 0, fails = 0, up = 0;
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < 20000) {
      uint8_t r = 0, buf[8];
      bool good = i2c_write_timeout_us(BUS, 0x52, &r, 1, false, T_US) == 1;
      sleep_us(200);
      good = good && i2c_read_timeout_us(BUS, 0x52, buf, 8, false, T_US) == 8;
      if (!good) {
        fails++;
      } else {
        reads++;
        if (!(buf[7] & 0x01)) up++;
        if (!(buf[7] & 0x02)) total_left[phase & 1]++;
        if (buf[6] != last6 || buf[7] != last7) {
          printf("%6lu ms  b6 %02x b7 %02x %s\n",
                 (unsigned long)to_ms_since_boot(get_absolute_time()), buf[6], buf[7],
                 names(buf[6], buf[7]));
          last6 = buf[6]; last7 = buf[7];
        }
      }
      sleep_ms(16);
    }
    printf("=== %u kHz: %u reads, %u failed, %u with Up\n", hz / 1000, reads, fails, up);
    total_reads[phase & 1] += reads;
    total_up[phase & 1] += up;
    printf("=== TOTAL 100 kHz: %u reads, %u with Left, %u with Up | 20 kHz: %u reads, %u with Left, %u with Up\n",
           total_reads[0], total_left[0], total_up[0], total_reads[1], total_left[1], total_up[1]);
    i2c_deinit(BUS);
  }
}

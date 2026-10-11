// I2C check for the ResTouch controller bus (i2c0, SDA GP4, SCL GP5).
// Prints over USB serial every 2 s: line levels, devices found, and the raw
// bytes of a NES/Wii Classic controller at 0x52.
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define SDA 4
#define SCL 5
#define BUS i2c0
#define T_US 2000

static void lines(void)
{
  gpio_init(SDA); gpio_init(SCL);
  gpio_set_dir(SDA, GPIO_IN); gpio_set_dir(SCL, GPIO_IN);
  gpio_disable_pulls(SDA); gpio_disable_pulls(SCL);
  sleep_us(50);
  int sda_ext = gpio_get(SDA), scl_ext = gpio_get(SCL);
  gpio_pull_up(SDA); gpio_pull_up(SCL);
  sleep_us(50);
  printf("lines, no pulls: SDA %d SCL %d | internal pull-ups: SDA %d SCL %d\n",
         sda_ext, scl_ext, gpio_get(SDA), gpio_get(SCL));
  // free a stuck bus
  for (int i = 0; i < 9 && !gpio_get(SDA); i++) {
    gpio_put(SCL, 0); gpio_set_dir(SCL, GPIO_OUT); sleep_us(5);
    gpio_set_dir(SCL, GPIO_IN); sleep_us(5);
  }
}

int main(void)
{
  stdio_init_all();
  for (int round = 0;; round++) {
    sleep_ms(2000);
    printf("\n--- round %d\n", round);
    lines();
    i2c_init(BUS, 100 * 1000);
    gpio_set_function(SDA, GPIO_FUNC_I2C); gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA); gpio_pull_up(SCL);

    printf("scan:");
    int found = 0;
    for (int a = 0x08; a < 0x78; a++) {
      uint8_t b;
      if (i2c_read_timeout_us(BUS, a, &b, 1, false, T_US) >= 0) { printf(" %02x", a); found++; }
    }
    printf(found ? "\n" : " nothing\n");

    uint8_t i1[] = {0xF0, 0x55}, i2[] = {0xFB, 0x00}, i3[] = {0xFE, 0x03}, r = 0, buf[8];
    int e1 = i2c_write_timeout_us(BUS, 0x52, i1, 2, false, T_US); sleep_ms(10);
    int e2 = i2c_write_timeout_us(BUS, 0x52, i2, 2, false, T_US); sleep_ms(10);
    int e3 = i2c_write_timeout_us(BUS, 0x52, i3, 2, false, T_US); sleep_ms(10);
    printf("0x52 init: %d %d %d (2 = ok, negative = no answer)\n", e1, e2, e3);
    for (int n = 0; n < 3; n++) {
      for (int i = 0; i < 8; i++) buf[i] = 0xEE;
      int w = i2c_write_timeout_us(BUS, 0x52, &r, 1, false, T_US);
      sleep_us(300);
      int rd = i2c_read_timeout_us(BUS, 0x52, buf, 8, false, T_US);
      printf("read: w %d r %d:", w, rd);
      for (int i = 0; i < 8; i++) printf(" %02x", buf[i]);
      printf("\n");
      sleep_ms(100);
    }
    i2c_deinit(BUS);
  }
}

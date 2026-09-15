#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  pin_t vcc;
  pin_t gnd;
  pin_t rx;
  pin_t tx;
  uart_dev_t uart;
  timer_t timer;
  uint32_t speed_attr;
} chip_state_t;

static void on_timer(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  uint32_t speed_kmh = attr_read(chip->speed_attr);
  float speed_knots = (float)speed_kmh / 1.852f;

  char rmc[128];
  snprintf(rmc, sizeof(rmc), "$GPRMC,123519.00,A,5027.0060,N,03031.4040,E,%05.1f,084.4,230326,003.1,W", speed_knots);

  uint8_t cs = 0;
  for (int i = 1; rmc[i] != '\0'; i++) {
    cs ^= (uint8_t)rmc[i];
  }
  char buf[160];
  int len = snprintf(buf, sizeof(buf), "%s*%02X\r\n", rmc, cs);
  uart_write(chip->uart, (uint8_t *)buf, len);

  char gga[128];
  snprintf(gga, sizeof(gga), "$GPGGA,123519.00,5027.0060,N,03031.4040,E,1,08,0.9,150.0,M,46.9,M,,");
  cs = 0;
  for (int i = 1; gga[i] != '\0'; i++) {
    cs ^= (uint8_t)gga[i];
  }
  len = snprintf(buf, sizeof(buf), "%s*%02X\r\n", gga, cs);
  uart_write(chip->uart, (uint8_t *)buf, len);
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));

  chip->vcc = pin_init("VCC", INPUT);
  chip->gnd = pin_init("GND", INPUT);
  chip->rx = pin_init("RX", INPUT);
  chip->tx = pin_init("TX", OUTPUT_HIGH);

  chip->speed_attr = attr_init("speed", 45);

  const uart_config_t uart_cfg = {
    .rx = chip->rx,
    .tx = chip->tx,
    .baud_rate = 9600,
    .rx_data = NULL,
    .write_done = NULL,
    .user_data = chip,
  };
  chip->uart = uart_init(&uart_cfg);

  const timer_config_t timer_cfg = {
    .callback = on_timer,
    .user_data = chip,
  };
  chip->timer = timer_init(&timer_cfg);
  timer_start(chip->timer, 1000000, true);
}
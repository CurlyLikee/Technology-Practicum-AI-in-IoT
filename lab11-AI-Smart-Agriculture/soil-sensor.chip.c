#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  pin_t pin_a0;
  uint32_t moisture_attr;
} chip_data_t;

void chip_timer_callback(void *data) 
{
  chip_data_t *chip_data = (chip_data_t*)data;
  float moisture = attr_read_float(chip_data->moisture_attr);
  float volts = 5.0 * (moisture / 4095.0);
  
  pin_dac_write(chip_data->pin_a0, volts);
}

void chip_init() 
{
  chip_data_t *chip_data = (chip_data_t*)malloc(sizeof(chip_data_t));
  
  chip_data->moisture_attr = attr_init_float("moisture", 2650.0f);
  
  pin_init("GND", INPUT);
  pin_init("VCC", INPUT);
  chip_data->pin_a0 = pin_init("A0", ANALOG);

  const timer_config_t config = 
  {
    .callback = chip_timer_callback,
    .user_data = chip_data,
  };

  timer_t timer_id = timer_init(&config);
  timer_start(timer_id, 100000, true);
}
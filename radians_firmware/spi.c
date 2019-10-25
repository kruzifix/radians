/*
 * spi.c
 *
 * Created: 25-Oct-19 12:11:00 PM
 *  Author: david
 */

#include "spi.h"

void setup_leds(void)
{
    DDRD |= (1 << TLC_LE_PIN) | (1 << TLC_OE_PIN);

    LEDS_OFF;
    set_leds(0x00);
}

void set_leds(uint8_t bits)
{
    REVERSE_SPI;
    send_data(bits);
    REVERSE_SPI;
    PULSE_LE;
}

void setup_dac(void)
{
    DDRB |= (1 << DAC_CS_PIN);
    DAC_CS_HIGH;

    set_dac_rand(0x00);
    set_dac_quant(0x00);
}

#define DAC_A 0
#define DAC_B 1

static inline void set_dac(uint8_t dac, uint8_t voltage)
{
    DAC_CS_LOW;
    _delay_us(10);
    uint8_t cmd_high = ((dac & 0x1) << 7) | 0x10 | (voltage >> 4);
    send_data(cmd_high);
    uint8_t cmd_low = (voltage & 0x0F) << 4;
    send_data(cmd_low);
    _delay_us(10);
    DAC_CS_HIGH;
}

void set_dac_rand(uint8_t voltage)
{
    set_dac(DAC_B, voltage);
}

void set_dac_quant(uint8_t voltage)
{
    set_dac(DAC_A, voltage);
}

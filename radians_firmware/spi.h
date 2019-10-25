/*
 * spi.h
 *
 * Created: 25-Oct-19 12:10:24 PM
 *  Author: david
 */

#ifndef SPI_H_
#define SPI_H_

#include "config.h"

#define REVERSE_SPI (SPCR ^= (1 << DORD))

#define TLC_LE_PIN PD6
#define TLC_OE_PIN PD7

#define LEDS_OFF (PORTD |= (1 << TLC_OE_PIN))
#define LEDS_ON (PORTD &= ~(1 << TLC_OE_PIN))

#define TLC_LE_LOW (PORTD &= ~(1 << TLC_LE_PIN))
#define TLC_LE_HIGH (PORTD |= (1 << TLC_LE_PIN))

#define PULSE_LE { TLC_LE_HIGH; _delay_us(5); TLC_LE_LOW; }

#define DAC_CS_PIN PB0
#define DAC_CS_LOW (PORTB &= ~(1 << DAC_CS_PIN))
#define DAC_CS_HIGH (PORTB |= (1 << DAC_CS_PIN))

void setup_leds(void);
void set_leds(uint8_t bits);

void setup_dac(void);
void set_dac_rand(uint8_t voltage);
void set_dac_quant(uint8_t voltage);

inline void setup_spi(void)
{
    // MOSI and SCK as output
    DDRB = (1 << PB3) | (1 << PB5) | (1 << PB2);
    PORTB = 0x00;
    // enable SPI, Master, set clock rate fck/64
    SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR1);

    setup_leds();
    setup_dac();
}

inline void send_data(uint8_t data)
{
    SPDR = data;
    while (!(SPSR & (1<<SPIF)));
}

#endif /* SPI_H_ */

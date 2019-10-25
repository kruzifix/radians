/*
 * adc.c
 *
 * Created: 25-Oct-19 12:11:55 PM
 *  Author: david
 */

#include "adc.h"

typedef enum
{
    ADC_QUANT_IN,
    ADC_CHANGE_CV_IN,
    ADC_LOOP_LENGTH_IN,
    ADC_NUM_CHANNELS
} adc_channel_t;

static const uint8_t adc_channel_mux[ADC_NUM_CHANNELS] = { 0, 1, 5 };

static volatile uint16_t adc_values[ADC_NUM_CHANNELS];

void setup_adc(void)
{
    // AVcc with external capacitor at Aref, right adjust
    ADMUX = (1 << REFS0) | adc_channel_mux[ADC_QUANT_IN];

    // enable ADC, Interrupt enabled, Prescaler: 128 => 125kHz
    ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

    // disable digital input
    DIDR0 = (1 << ADC0D) | (1 << ADC1D) | (1 << ADC5D);

    // start first conversion
    ADCSRA |= (1 << ADSC);
}

uint16_t get_adc_change_cv(void)
{
    return adc_values[ADC_CHANGE_CV_IN];
}

uint16_t get_adc_quant(void)
{
    return adc_values[ADC_QUANT_IN];
}

uint8_t get_adc_steps_index(void)
{
    // when switching adc input jumps to 5 volt between steps!!
    // mask too high adc value, or only step to other value when enough values after another were same
    uint8_t index = adc_values[ADC_LOOP_LENGTH_IN] >> 7;
    return index & 0x07;
}

ISR(ADC_vect)
{
    static uint8_t current_channel = ADC_QUANT_IN;

    // TODO: maybe? average?
    adc_values[current_channel] = ADC;
    current_channel = (current_channel + 1) % ADC_NUM_CHANNELS;

    ADMUX &= 0xF0;
    ADMUX += adc_channel_mux[current_channel];

    ADCSRA |= (1 << ADSC);
}

/*
 * adc.h
 *
 * Created: 25-Oct-19 12:11:48 PM
 *  Author: david
 */

#ifndef ADC_H_
#define ADC_H_

#include "config.h"

void setup_adc(void);

// 10 bit
uint16_t get_adc_change_cv(void);
// 10 bit
uint16_t get_adc_quant(void);

uint8_t get_adc_steps_index(void);

#endif /* ADC_H_ */

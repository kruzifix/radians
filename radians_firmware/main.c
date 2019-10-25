/*
 * radians_firmware.c
 *
 * Created: 25-Oct-19 12:01:12 PM
 * Author : david
 */

#include "spi.h"
#include "adc.h"
#include "prng_lcg.h"

#include "config.h"
#include <stdlib.h>

typedef struct {
    uint16_t pressed;
} button_t;

#define SW_QUANT_MODE 0
#define SW_RAND_MODE 1
#define SW_CLEAR 2

#define SW_QUANT_MODE_PIN PC2
#define SW_RAND_MODE_PIN PC3
#define SW_CLEAR_PIN PC4

#define SW_DOWN(sw) (!(PINC & (1 << (sw##_PIN))))

#define CLK_IN_PIN PD2
#define CLK_OUT_PIN PD3

#define CLK_IN_ACTIVE (!(PIND & (1 << CLK_IN_PIN)))

#define CLK_OUT_LOW (PORTD &= ~(1 << CLK_OUT_PIN))
#define CLK_OUT_HIGH (PORTD |= (1 << CLK_OUT_PIN))

// control mode
typedef enum
{
    CMODE_NORMAL,
    CMODE_VARIGATE
} control_mode_t;

control_mode_t current_mode = CMODE_NORMAL;

// random looping sequence
#define MAX_STEPS 16
#define THRESHOLD_FOR_CHANGE 0x0F

static const uint8_t loop_lengths[8] = { 2, 3, 4, 5, 6, 8, 12, 16 };

uint8_t steps[MAX_STEPS];
uint8_t step_index;
uint8_t step_length;
uint8_t step_change_cv;
uint8_t change_cv_edit;

// varigate
#define MAX_VARIGATE_STEPS 8
uint8_t gate_probability[MAX_VARIGATE_STEPS];

// quantizer!
#define QUANTIZER_SCALES 8
uint8_t current_scale;
uint16_t note_scales[QUANTIZER_SCALES] = {
    0b111111111111, // chromatic
    0b010110101101, // Aeolian mode (natural minor) t-s-t-t-s-t-t
    0b010101101011, // Locrian mode s-t-t-s-t-t-t
    0b101010110101, // Ionian mode (major) t-t-s-t-t-t-s
    0b011010101101, // Dorian mode t-s-t-t-t-s-t
    0b010110101011, // Phrygian mode s-t-t-t-s-t-t
    0b101011010101, // Lydian mode t-t-t-s-t-t-s
    0b011010110101, // Mixolydian mode t-t-s-t-t-s-t
};

volatile uint8_t led_override_time;
volatile uint8_t leds_after_override;

#define LEDS(val) { if (led_override_time > 0) leds_after_override = val; else set_leds(val); }
#define OVERRIDE_LEDS(val, t) { led_override_time = t; set_leds(val); }

uint8_t get_enabled_note(uint8_t note)
{
    while (note > 0 && !(note_scales[current_scale] & (1 << (note % 12))))
    note--;
    return note;
}

void setup_timer()
{
    // 16ms period
    TCCR2A = 0x00; // Normal 8 Bit Mode
    TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20); // Clock prescaler 1024 --> 60Hz
    TIMSK2 = (1 << TOIE2); // Overflow Int active
}

int main(void)
{
    DDRC &= ~((1 << SW_QUANT_MODE_PIN) | (1 << SW_RAND_MODE_PIN) | (1 << SW_CLEAR_PIN));
    PORTC = (1 << SW_QUANT_MODE_PIN) | (1 << SW_RAND_MODE_PIN) | (1 << SW_CLEAR_PIN);

    DDRD = (1 << CLK_OUT_PIN) | (0 << CLK_IN_PIN);
    PORTD = 0x00;

    for (uint8_t i = 0; i < MAX_STEPS; i++)
    {
        steps[i] = 0;
    }
    step_index = 0;
    step_length = loop_lengths[get_adc_steps_index()];
    step_change_cv = get_adc_change_cv() >> 2;
    change_cv_edit = 0;

    for (uint8_t i = 0; i < MAX_VARIGATE_STEPS; i++)
    {
        gate_probability[i] = 0xFF; // 100%!
    }

    current_scale = 0;

    led_override_time = 0;
    leds_after_override = 0x00;

    setup_spi();
    setup_adc();
    setup_timer();

    sei();

    LEDS_ON;

    uint8_t last_clk = 0x00;

    while (1)
    {
        uint8_t clk = CLK_IN_ACTIVE;
        // rising edge
        if (!last_clk && clk)
        {
            if (current_mode == CMODE_NORMAL)
            {
                step_length = loop_lengths[get_adc_steps_index()];

                uint8_t change_cv = get_adc_change_cv() >> 2;

                if (change_cv < THRESHOLD_FOR_CHANGE)
                change_cv_edit = 1;

                if (change_cv_edit)
                {
                    step_change_cv = change_cv;
                }
            }

            step_index = (step_index + 1) % step_length;

            // decide if new random value!
            // if under threshold, no change at all!
            if (step_change_cv > THRESHOLD_FOR_CHANGE)
            {
                if ((rand_lcg() >> 7) < step_change_cv)
                {
                    steps[step_index] = rand_lcg() & 0xFF;
                }
            }

            // varigate!
            {
                uint8_t prob = gate_probability[step_index % MAX_VARIGATE_STEPS];

                // output gate?
                if (prob > THRESHOLD_FOR_CHANGE && (rand_lcg() >> 8) < prob)
                CLK_OUT_HIGH;
            }

            uint8_t bits = steps[step_index];
            set_dac_rand(bits);
            if (current_mode == CMODE_NORMAL)
            {
                LEDS(bits);
            }
        }

        // falling edge
        if (last_clk && !clk)
        {
            CLK_OUT_LOW;
        }

        last_clk = clk;

        // do quantization
        {
            uint8_t quant_input = get_adc_quant() >> 2;

            // quantize!
            uint8_t note = get_enabled_note(quant_input / 5);

            // output!
            set_dac_quant(note * 5);
        }

        _delay_ms(1);
    }
}

// 60Hz ~ 16ms
ISR(TIMER2_OVF_vect)
{
    // do buttons here
    static button_t rand_btn;
    static button_t clear_btn;
    static button_t quant_btn;

    static uint8_t vg_step_index = 0;
    static uint16_t vg_index_time = 0;
    static uint8_t vg_last_prob = 0;
    static uint8_t vg_edit_prob = 0;

    if (SW_DOWN(SW_RAND_MODE))
    {
        rand_btn.pressed = rand_btn.pressed == 0xFFFF ? 0xFFFF : rand_btn.pressed + 1;

        // held 1 second
        if (rand_btn.pressed == 60)
        {
            current_mode = current_mode == CMODE_NORMAL ? CMODE_VARIGATE : CMODE_NORMAL;
            change_cv_edit = 0;
            LEDS(0x00);
        }
    }
    else
    {
        rand_btn.pressed = 0;
    }

    if (SW_DOWN(SW_CLEAR))
    {
        clear_btn.pressed = clear_btn.pressed == 0xFFFF ? 0xFFFF : clear_btn.pressed + 1;
    }
    else
    {
        if (clear_btn.pressed > 0 && clear_btn.pressed < 30)
        {
            switch (current_mode)
            {
                case CMODE_NORMAL:
                for (uint8_t i = 0; i < MAX_STEPS; i++)
                {
                    steps[i] = 0;
                }
                change_cv_edit = 0;
                break;
                case CMODE_VARIGATE:
                for (uint8_t i = 0; i < MAX_VARIGATE_STEPS; i++)
                {
                    gate_probability[i] = 0xFF; // 100%!
                }
                vg_last_prob = get_adc_change_cv() >> 2;
                vg_step_index = 0xFF;
                vg_index_time = 0;
                vg_edit_prob = 0;
                break;
            }
            LEDS(0x00);
        }

        clear_btn.pressed = 0;
    }

    if (SW_DOWN(SW_QUANT_MODE))
    {
        quant_btn.pressed = quant_btn.pressed == 0xFFFF ? 0xFFFF : quant_btn.pressed + 1;
    }
    else
    {
        if (quant_btn.pressed > 0 && quant_btn.pressed < 30)
        {
            current_scale = (current_scale + 1) % QUANTIZER_SCALES;
            OVERRIDE_LEDS(1 << (7 - current_scale), 40);
        }

        quant_btn.pressed = 0;
    }

    // varigate input
    if (current_mode == CMODE_VARIGATE)
    {
        uint8_t current_step = get_adc_steps_index();

        if (current_step == vg_step_index)
        {
            vg_index_time = vg_index_time == 0xFF ? 0xFF : vg_index_time + 1;
        }
        else
        {
            vg_last_prob = get_adc_change_cv() >> 2;
            vg_step_index = current_step;
            vg_index_time = 0;
            vg_edit_prob = 0;
            LEDS(0x00);
        }

        if (vg_index_time > 12)
        {
            LEDS(1 << (7 - vg_step_index));

            uint8_t prob = get_adc_change_cv() >> 2;

            if (vg_edit_prob)
            {
                gate_probability[vg_step_index] = prob;
                LEDS(prob <= THRESHOLD_FOR_CHANGE ? 0x00 : prob);
            }
            else if (abs(prob - vg_last_prob) > 30)
            {
                vg_edit_prob = 1;
            }
        }
    }

    if (led_override_time > 0)
    {
        led_override_time--;
        if (led_override_time == 0)
        {
            set_leds(leds_after_override);
            leds_after_override = 0x00;
        }
    }
}

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
    CMODE_NORMAL = 0,
    CMODE_VARIGATE,
    CMODE_VARIGATE_SET_PROB,
    CMODE_QUANTIZER_OUTPUT
} control_mode_t;

volatile uint8_t current_mode = CMODE_NORMAL;


// random looping sequence
#define MAX_STEPS 16
#define THRESHOLD_FOR_CHANGE 0x0F

static const uint8_t loop_lengths[8] = { 2, 3, 4, 5, 6, 8, 12, 16 };

uint8_t steps[MAX_STEPS];
uint8_t step_index;
uint8_t enabled_normal_mode_editing;

uint16_t shift_value;

typedef enum 
{
    RMODE_TOTAL_RANDOMNESS = 0,
    RMODE_SHIFT,
    RMODE_COUNT
} random_mode_t;

volatile uint8_t current_random_mode = RMODE_TOTAL_RANDOMNESS;

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

volatile uint8_t current_leds;
volatile uint8_t next_leds;

volatile uint8_t led_override_time;
volatile uint8_t flash_leds;

void LEDS(uint8_t value)
{
    if (led_override_time == 0)
    {
        next_leds = value;
    }
}

// time unit is ticks (1/60 s)
void OVERRIDE_LEDS(uint8_t value, uint8_t time)
{
    led_override_time = time;
    next_leds = value;
    flash_leds = 0;
}

void FLASH_LEDS(uint8_t value, uint8_t time)
{
    led_override_time = time;
    next_leds = value;
    flash_leds = value;
}

uint8_t get_enabled_note(uint8_t note)
{
    while (note > 0 && !(note_scales[current_scale] & (1 << (note % 12))))
    {
        note--;
    }

    return note;
}

uint8_t get_bar_value(uint8_t value)
{
    uint8_t bit_count = value >> 5;
    if (value > 15)
    {
        bit_count += 1;
    }

    uint8_t led_value = 0x00;
    for (uint8_t i = 0; i <= 7; ++i)
    {
        if (i < bit_count)
        {
            led_value |= 1 << (7 - i);
        }
    }

    return led_value;
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

    enabled_normal_mode_editing = 1;
    shift_value = 0;

    for (uint8_t i = 0; i < MAX_VARIGATE_STEPS; i++)
    {
        gate_probability[i] = 0xFF; // 100%!
    }

    current_scale = 0;

    current_leds = 0;
    next_leds = 0;
    led_override_time = 0;
    flash_leds = 0;

    setup_spi();
    setup_adc();
    setup_timer();

    sei();

    LEDS_ON;

    uint8_t last_clk = 0;
    uint8_t current_loop_length = loop_lengths[0];
    uint8_t current_change_cv = 0;
    uint8_t current_step_index = 0;

    while (1)
    {
        if (next_leds != current_leds)
        {
            set_leds(next_leds);
            current_leds = next_leds;
        }

        uint8_t clk = CLK_IN_ACTIVE;
        // rising edge
        if (!last_clk && clk)
        {
            if (current_mode == CMODE_NORMAL)
            {
                uint8_t change_cv = get_adc_change_cv() >> 2;
                uint8_t loop_length = loop_lengths[get_adc_steps_index()];

                if (enabled_normal_mode_editing)
                {
                    current_change_cv = change_cv;
                    current_loop_length = loop_length;
                }
                else
                {
                    if (change_cv < THRESHOLD_FOR_CHANGE &&
                    loop_length == current_loop_length)
                    {
                        enabled_normal_mode_editing = 1;
                    }
                }
            }

            current_step_index = (current_step_index + 1) % current_loop_length;

            // decide if new random value!
            // if under threshold, no change at all!
            uint8_t apply_change = current_change_cv > THRESHOLD_FOR_CHANGE &&
                (uint8_t)((rand_lcg() >> 7) & 0xFF) < current_change_cv;

            uint8_t current_random_bits = 0;
            switch (current_random_mode)
            {
            case RMODE_TOTAL_RANDOMNESS:
            {
                if (apply_change)
                {
                    steps[current_step_index] = rand_lcg() & 0xFF;
                }

                if (SW_DOWN(SW_CLEAR))
                {
                    steps[current_step_index] = 0x00;
                }

                current_random_bits = steps[current_step_index];
                break;
            }
            case RMODE_SHIFT:
            {
                uint8_t new_value_index = 15 - (current_loop_length - 1);
                uint8_t new_value = (shift_value >> new_value_index) & 0x01;

                if (apply_change)
                {
                    new_value ^= 0x01;
                }

                if (SW_DOWN(SW_CLEAR))
                {
                    new_value = 0x00;
                }

                shift_value = (shift_value >> 1) | (new_value << 15);
                current_random_bits =  shift_value & 0xFF;
                break;
            }
            default:
                // wtf?! should not happen!
                current_random_mode = RMODE_TOTAL_RANDOMNESS;
                break;
            }

            set_dac_rand(current_random_bits);
            if (current_mode == CMODE_NORMAL && enabled_normal_mode_editing)
            {
                LEDS(current_random_bits);
            }

            // varigate!
            {
                uint8_t prob = gate_probability[current_step_index % MAX_VARIGATE_STEPS];

                // output gate?
                if (prob > THRESHOLD_FOR_CHANGE && (rand_lcg() >> 8) < prob)
                {
                    CLK_OUT_HIGH;
                }
            }
        }
        // falling edge
        else if (last_clk && !clk)
        {
            CLK_OUT_LOW;
        }

        last_clk = clk;

        // do quantization
        {
            uint8_t quant_input = get_adc_quant() >> 2;

            // quantize!
            uint8_t note = get_enabled_note(quant_input / 5);

            if (current_mode == CMODE_QUANTIZER_OUTPUT)
            {
                LEDS(note);
            }

            // output!
            set_dac_quant(note * 5);
        }
    }
}

// 60Hz ~ 16ms
ISR(TIMER2_OVF_vect)
{
    // do buttons here
    static button_t rand_btn;
    static button_t clear_btn;
    static button_t quant_btn;

    static uint8_t vg_editing_index = 0;

    static uint8_t time_counter = 0;

    ++time_counter;

    if (SW_DOWN(SW_RAND_MODE))
    {
        rand_btn.pressed = rand_btn.pressed == 0xFFFF ? 0xFFFF : rand_btn.pressed + 1;

        // held 1.5 seconds
        if (rand_btn.pressed == 90)
        {
            if (current_mode == CMODE_VARIGATE)
            {
                current_mode = CMODE_NORMAL;
            }
            else if (current_mode == CMODE_NORMAL)
            {
                current_mode = CMODE_VARIGATE;
                enabled_normal_mode_editing = 0;
            }

            FLASH_LEDS(current_mode == CMODE_VARIGATE ? 0x0F : 0xF0, 60);
        }
    }
    else
    {
        if (rand_btn.pressed > 0 && rand_btn.pressed < 30)
        {
            switch (current_mode)
            {
            case CMODE_NORMAL:
                current_random_mode = (current_random_mode + 1) % RMODE_COUNT;
                FLASH_LEDS(1 << (7 - current_random_mode), 60);
                break;
            case CMODE_VARIGATE:
                current_mode = CMODE_VARIGATE_SET_PROB;
                vg_editing_index = get_adc_steps_index();
                break;
            case CMODE_VARIGATE_SET_PROB:
                current_mode = CMODE_VARIGATE;
                gate_probability[vg_editing_index] = get_adc_change_cv() >> 2;
                break;
            default:
                break;
            }
        }

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
            case CMODE_VARIGATE:
                gate_probability[get_adc_steps_index()] = 0xFF;
                break;
            case CMODE_VARIGATE_SET_PROB:
                current_mode = CMODE_VARIGATE;
                break;
            default:
                break;
            }
        }

        clear_btn.pressed = 0;
    }

    if (SW_DOWN(SW_QUANT_MODE))
    {
        quant_btn.pressed = quant_btn.pressed == 0xFFFF ? 0xFFFF : quant_btn.pressed + 1;

        // held 1 second
        if (quant_btn.pressed == 60)
        {
            if (current_mode == CMODE_QUANTIZER_OUTPUT)
            {
                current_mode = CMODE_NORMAL;
            }
            else
            {
                current_mode = CMODE_QUANTIZER_OUTPUT;
            }

            LEDS(0x00);
        }
    }
    else
    {
        if (quant_btn.pressed > 0 && quant_btn.pressed < 30)
        {
            current_scale = (current_scale + 1) % QUANTIZER_SCALES;
            FLASH_LEDS(1 << (7 - current_scale), 60);
        }

        quant_btn.pressed = 0;
    }

    if (current_mode == CMODE_VARIGATE)
    {
        uint8_t selected_step = get_adc_steps_index();

        LEDS(get_bar_value(gate_probability[selected_step]));
    }
    else if (current_mode == CMODE_VARIGATE_SET_PROB)
    {
        if ((time_counter % 12) == 0)
        {
            LEDS(get_bar_value(get_adc_change_cv() >> 2));
        }
        else if ((time_counter % 12) == 6)
        {
            next_leds = 0x00;
        }
    }
    else if (current_mode == CMODE_NORMAL &&
        !enabled_normal_mode_editing &&
        led_override_time == 0)
    {
        if ((time_counter % 12) == 0)
        {
            next_leds = 0xC3;
        }
        else if ((time_counter % 12) == 6)
        {
            next_leds = 0x00;
        }
    }

    if (led_override_time > 0)
    {
        led_override_time--;

        if (flash_leds != 0)
        {
            if ((led_override_time % 12) == 0)
            {
                next_leds = 0;
            }
            else if ((led_override_time % 12) == 6)
            {
                next_leds = flash_leds;
            }
        }

        if (led_override_time == 0)
        {
            next_leds = 0x00;
        }
    }
}

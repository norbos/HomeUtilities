/* HomeRoofDrain - Bare-metal firmware for ATmega328P
 * Implements 6-hour interval + rain-triggered pump checks with flow sensing.
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>
#include "project.h"

/* Timing constants */
#define SIX_HOURS_IN_SEC 21600UL

/* Flow/behavior thresholds (tune as needed) */
#define PRIME_SECONDS 30
#define PRIME_FLOW_MIN_PULSES 10
#define FLOW_CHECK_WINDOW_SEC 5
#define FLOW_KEEPALIVE_MIN_PULSES 2

volatile uint32_t flow_pulse_count = 0;
volatile bool rain_detected = false;

/* Initialize I/O registers */
void io_init(void) {
    /* Pump control output */
    PUMP_DDR |= (1 << PUMP_PIN);
    PUMP_PORT &= ~(1 << PUMP_PIN); /* Ensure pump off (assumes active-high relay) */

    /* Flow and Rain as inputs */
    DDRD &= ~((1 << FLOW_PIN) | (1 << RAIN_PIN));

    /* Optionally enable internal pull-up for rain sensor if it's open-drain/active-low */
    /* PORTD |= (1 << RAIN_PIN); */
}

/* Initialize external interrupts: INT0 (FLOW) rising, INT1 (RAIN) falling */
void interrupt_init(void) {
    /* INT0 (PD2) - rising edge */
    EICRA |= (1 << ISC01) | (1 << ISC00);

    /* INT1 (PD3) - falling edge: ISC11 = 1, ISC10 = 0 */
    EICRA |= (1 << ISC11);
    EICRA &= ~(1 << ISC10);

    /* Enable INT0 and INT1 */
    EIMSK |= (1 << INT0) | (1 << INT1);
}

/* ISR: flow pulses increment counter */
ISR(INT0_vect) {
    flow_pulse_count++;
}

/* ISR: rain detected (edge) */
ISR(INT1_vect) {
    rain_detected = true;
}

/* Pump control helpers */
void turn_pump_on(void) {
    PUMP_PORT |= (1 << PUMP_PIN);
}

void turn_pump_off(void) {
    PUMP_PORT &= ~(1 << PUMP_PIN);
}

/* Delay wrapper (use _delay_ms in loop) */
void delay_seconds(uint16_t seconds) {
    for (uint16_t i = 0; i < seconds; i++) {
        _delay_ms(1000);
    }
}

int main(void) {
    io_init();
    interrupt_init();

    /* Enable global interrupts */
    sei();

    uint32_t timer_counter = 0;

    while (1) {
        /* Base state tracking: increment counter every 1 second */
        delay_seconds(1);
        timer_counter++;

        /* Trigger condition: 6 hours elapsed OR rain detected */
        if (timer_counter >= SIX_HOURS_IN_SEC || rain_detected) {
            rain_detected = false;
            timer_counter = 0;

            /* Prime the pump for a short burst */
            turn_pump_on();
            flow_pulse_count = 0;
            delay_seconds(PRIME_SECONDS);

            /* Evaluate initial flow */
            if (flow_pulse_count < PRIME_FLOW_MIN_PULSES) {
                /* No significant flow detected -> stop immediately */
                turn_pump_off();
            } else {
                /* Keep pumping while flow continues */
                while (1) {
                    flow_pulse_count = 0;
                    delay_seconds(FLOW_CHECK_WINDOW_SEC);

                    if (flow_pulse_count < FLOW_KEEPALIVE_MIN_PULSES) {
                        break;
                    }
                }
                turn_pump_off();
            }
        }
    }

    return 0;
}

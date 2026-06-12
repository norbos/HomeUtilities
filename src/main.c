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

#define UART_BAUD 9600UL
#define UBRR_VAL ((F_CPU / 16UL / UART_BAUD) - 1UL)

/* Minimal UART helpers (TX only for debug prints) */
void uart_init(void) {
    /* Set baud */
    UBRR0H = (uint8_t)(UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(UBRR_VAL & 0xFF);
    /* Enable transmitter only (RX optional) */
    UCSR0B = (1 << TXEN0);
    /* Set frame: 8 data bits, no parity, 1 stop bit */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = (uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

/* print unsigned decimal (simple) */
void uart_putu32(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0 && i < (int)sizeof(buf)-1) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i--) uart_putc(buf[i]);
}

void uart_putln(const char *s) { uart_puts(s); uart_puts("\r\n"); }

/* Timing constants */
/* For testing we use a 5-minute interval (300s). Change back to 21600UL for 6 hours. */
#define CHECK_INTERVAL_SEC 300UL

/* Flow/behavior thresholds (tune as needed) */
#define PRIME_SECONDS 30
#define PRIME_FLOW_MIN_PULSES 10
#define FLOW_CHECK_WINDOW_SEC 5
#define FLOW_KEEPALIVE_MIN_PULSES 2

volatile uint32_t flow_pulse_count = 0;
volatile bool rain_detected = false;
volatile uint16_t rain_event_count = 0; /* count ISR events to debounce/log in main */

/* Initialize I/O registers */
void io_init(void) {
    /* Pump control output */
    PUMP_DDR |= (1 << PUMP_PIN);
    PUMP_PORT &= ~(1 << PUMP_PIN); /* Ensure pump off (assumes active-high relay) */

    /* Flow and Rain as inputs */
    DDRD &= ~((1 << FLOW_PIN) | (1 << RAIN_PIN));

    /* Enable internal pull-ups for inputs so floating pins are stable when
       sensors are disconnected. This prevents spurious interrupts.
       If your sensor actively drives the line high/low, disable pull-up as needed. */
    PORTD |= (1 << FLOW_PIN) | (1 << RAIN_PIN);
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
    /* Record event and set flag; actual logging/debounce handled in main loop */
    rain_event_count++;
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
    uart_init();
    interrupt_init();

    /* Enable global interrupts */
    sei();

    uart_putln("BOOT");
    uart_putln("HomeRoofDrain starting");

    uint32_t timer_counter = 0;

    /* Perform an immediate startup check */
    uart_putln("STARTUP: performing initial check");
    {
        /* reuse trigger logic below by wrapping into a small block */
        uart_putln("TRIGGER (startup)");
        rain_detected = false;
        /* Prime the pump */
        uart_putln("PRIME: ON");
        turn_pump_on();
        flow_pulse_count = 0;
        delay_seconds(PRIME_SECONDS);
        uart_puts("PRIME: pulses="); uart_putu32(flow_pulse_count); uart_putln("");
        if (flow_pulse_count < PRIME_FLOW_MIN_PULSES) {
            uart_putln("PRIME: no flow, OFF");
            turn_pump_off();
        } else {
            uart_putln("FLOW: detected, keep pumping");
            while (1) {
                flow_pulse_count = 0;
                delay_seconds(FLOW_CHECK_WINDOW_SEC);
                uart_puts("WINDOW: pulses="); uart_putu32(flow_pulse_count); uart_putln("");
                if (flow_pulse_count < FLOW_KEEPALIVE_MIN_PULSES) {
                    uart_putln("FLOW: stopped, OFF");
                    break;
                }
            }
            turn_pump_off();
        }
    }

    /* After initial check, start normal periodic behavior */
    timer_counter = 0;

    while (1) {
        /* Base state tracking: increment counter every 1 second */
        delay_seconds(1);
        timer_counter++;

        /* If ISR recorded rain events, log them (with uptime) and keep flag for handler */
        if (rain_event_count) {
            uart_puts("RAIN event(s)="); uart_putu32(rain_event_count); uart_puts(" uptime="); uart_putu32(timer_counter); uart_putln("s");
            /* consume events and leave rain_detected=true so trigger logic runs */
            rain_event_count = 0;
        }

        /* Trigger condition: interval elapsed OR rain detected */
        if (timer_counter >= CHECK_INTERVAL_SEC || rain_detected) {
            uart_puts("TRIGGER at uptime="); uart_putu32(timer_counter); uart_putln("s");
            rain_detected = false;
            timer_counter = 0;

            /* Prime the pump for a short burst */
            uart_putln("PRIME: ON");
            turn_pump_on();
            flow_pulse_count = 0;
            delay_seconds(PRIME_SECONDS);

            uart_puts("PRIME: pulses="); uart_putu32(flow_pulse_count); uart_putln("");

            /* Evaluate initial flow */
            if (flow_pulse_count < PRIME_FLOW_MIN_PULSES) {
                /* No significant flow detected -> stop immediately */
                uart_putln("PRIME: no flow, OFF");
                turn_pump_off();
            } else {
                uart_putln("FLOW: detected, keep pumping");
                /* Keep pumping while flow continues */
                while (1) {
                    flow_pulse_count = 0;
                    delay_seconds(FLOW_CHECK_WINDOW_SEC);

                    uart_puts("WINDOW: pulses="); uart_putu32(flow_pulse_count); uart_putln("");

                    if (flow_pulse_count < FLOW_KEEPALIVE_MIN_PULSES) {
                        uart_putln("FLOW: stopped, OFF");
                        break;
                    }
                }
                turn_pump_off();
            }
        }
    }

    return 0;
}

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

/* ESP-01S default factory baud rate is 115200 */
#define UART_BAUD 115200UL
#define UBRR_VAL ((F_CPU / 16UL / UART_BAUD) - 1UL)

/* Target Pi Server Configuration & Heartbeat Intervals */
#define PI_SERVER_IP           "192.168.1.104"
#define PI_SERVER_PORT         "8080"
#define HEARTBEAT_INTERVAL_SEC 300UL

/* Monotonic uptime counter (seconds since boot) */

/* Minimal UART helpers (TX only for debug prints) */
void uart_init(void) {
    /* Set baud */
    UBRR0H = (uint8_t)(UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(UBRR_VAL & 0xFF);
    /* Enable transmitter and receiver */
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
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

/* Prefix logs with uptime: <seconds>s: <message> */
extern volatile uint32_t uptime_seconds;
void uart_prefix_uptime(void) {
    uart_puts("uptime: ");
    uart_putu32(uptime_seconds);
    uart_puts("s: ");
}

/* Minimal string composition helpers to prevent bulky stdio.h bloated compiled sizes */
int build_json(char *buf, uint32_t upt, uint32_t pls, const char* status) {
    int idx = 0;
    const char *p1 = "{\"uptime\":";
    while (*p1) buf[idx++] = *p1++;
    char tmp[11]; int i = 0; uint32_t val = upt;
    if (val == 0) { buf[idx++] = '0'; }
    else {
        while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
        while (i--) buf[idx++] = tmp[i];
    }
    const char *p2 = ",\"pulses\":";
    while (*p2) buf[idx++] = *p2++;
    i = 0; val = pls;
    if (val == 0) { buf[idx++] = '0'; }
    else {
        while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
        while (i--) buf[idx++] = tmp[i];
    }
    const char *p3 = ",\"status\":\"";
    while (*p3) buf[idx++] = *p3++;
    while (*status) buf[idx++] = *status++;
    buf[idx++] = '"'; buf[idx++] = '}'; buf[idx] = '\0';
    return idx;
}

int build_http_packet(char *buf, const char *json, int json_len) {
    int idx = 0;
    const char *h1 = "POST /api/telemetry HTTP/1.1\r\nHost: " PI_SERVER_IP "\r\nContent-Type: application/json\r\nContent-Length: ";
    while (*h1) buf[idx++] = *h1++;
    char tmp[6]; int i = 0; int len_cpy = json_len;
    if (len_cpy == 0) { buf[idx++] = '0'; }
    else {
        while (len_cpy > 0) { tmp[i++] = '0' + (len_cpy % 10); len_cpy /= 10; }
        while (i--) buf[idx++] = tmp[i];
    }
    const char *h2 = "\r\nConnection: close\r\n\r\n";
    while (*h2) buf[idx++] = *h2++;
    while (*json) buf[idx++] = *json++;
    buf[idx] = '\0';
    return idx;
}

/* Wi-Fi commands transmission routines */
void esp_wifi_init(void) {
    /* 1. Flush any power-on junk characters out of the line first */
    uart_putln(""); 
    _delay_ms(500);
    
    
    uart_putln("AT+RST");
    _delay_ms(3000);

    uart_putln("ATE0");  // disable echo
    _delay_ms(500);

    uart_putln("AT+CWMODE=1");
    _delay_ms(1000);

    uart_putln("AT+CWJAP=\"Norbi24\",\"\"");
    _delay_ms(10000);
}

void esp_send_http_post(uint32_t current_uptime, uint32_t pulses, const char* status_msg) {
    uart_putln("AT+CIPSTART=\"TCP\",\"" PI_SERVER_IP "\"," PI_SERVER_PORT);
    _delay_ms(600);

    char json_payload[128];
    int json_len = build_json(json_payload, current_uptime, pulses, status_msg);

    char http_request[256];
    int http_len = build_http_packet(http_request, json_payload, json_len);

    uart_puts("AT+CIPSEND=");
    uart_putu32(http_len);
    uart_puts("\r\n");
    _delay_ms(300);

    uart_puts(http_request);
    _delay_ms(1500); 

    uart_putln("AT+CIPCLOSE");
    _delay_ms(200);
}

/* Monotonic uptime counter (seconds since boot) */
volatile uint32_t uptime_seconds = 0;

/* Timing constants */
/* For testing we use a 60-seconds interval (300s). Change back to 21600UL for 6 hours. */
#define CHECK_INTERVAL_SEC 60UL

/* Flow/behavior thresholds (tune as needed) */
#define PRIME_SECONDS 10
#define PRIME_FLOW_MIN_PULSES 650
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

/* Initialize external interrupts: INT0 (FLOW) falling, INT1 (RAIN) falling */
void interrupt_init(void) {
    /* INT0 (PD2) - Changed to FALLING edge: ISC01 = 1, ISC00 = 0 */
    EICRA |= (1 << ISC01);   // Set ISC01 to 1
    EICRA &= ~(1 << ISC00);  // Clear ISC00 to 0

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
        uptime_seconds++;
    }
}

int main(void) {
    /* ⏳ NEW: Give the parallel buck converters 4 full seconds to stabilize 
       their voltage outputs completely before initializing any peripherals */
    for(uint8_t i = 0; i < 4; i++) {
        _delay_ms(1000);
    }

    io_init();
    uart_init();
    interrupt_init();
    
    /* Initialize Wi-Fi connection profile before running interrupts */
    esp_wifi_init();

    /* Enable global interrupts */
    sei();

    uint32_t timer_counter = 0;
    uint32_t heartbeat_counter = 0;

    /* Initialize uptime by counting the first second to make initial logs meaningful */
    delay_seconds(1);

    uart_prefix_uptime(); uart_putln("BOOT");
    uart_prefix_uptime(); uart_putln("HomeRoofDrain starting");
    
    /* Boot notification via HTTP Post */
    esp_send_http_post(uptime_seconds, 0, "BOOT_CONNECTED");

    /* Perform an immediate startup check */
    uart_prefix_uptime(); uart_putln("STARTUP: performing initial check");
    {
        /* reuse trigger logic below by wrapping into a small block */
        uart_prefix_uptime(); uart_putln("TRIGGER (startup)");
        rain_detected = false;
        /* Prime the pump */
        uart_prefix_uptime(); uart_putln("PRIME: ON");
        turn_pump_on();
        flow_pulse_count = 0;
        delay_seconds(PRIME_SECONDS);
        uart_prefix_uptime(); uart_puts("PRIME: pulses="); uart_putu32(flow_pulse_count); uart_putln("");
        if (flow_pulse_count < PRIME_FLOW_MIN_PULSES) {
            uart_prefix_uptime(); uart_putln("PRIME: no flow, OFF");
            turn_pump_off();
            esp_send_http_post(uptime_seconds, flow_pulse_count, "STARTUP_CHECK_DRY");
        } else {
            uart_prefix_uptime(); uart_putln("FLOW: detected, keep pumping");
            while (1) {
                flow_pulse_count = 0;
                delay_seconds(FLOW_CHECK_WINDOW_SEC);
                uart_prefix_uptime(); uart_puts("WINDOW: pulses="); uart_putu32(flow_pulse_count); uart_putln("");
                if (flow_pulse_count < FLOW_KEEPALIVE_MIN_PULSES) {
                    uart_prefix_uptime(); uart_putln("FLOW: stopped, OFF");
                    break;
                }
            }
            turn_pump_off();
            esp_send_http_post(uptime_seconds, flow_pulse_count, "STARTUP_DRAIN_COMPLETE");
        }
    }

    /* After initial check, start normal periodic behavior */
    timer_counter = 0;
    heartbeat_counter = 0;

    while (1) {
        /* Base state tracking: increment counter every 1 second */
        delay_seconds(1);
        timer_counter++;
        heartbeat_counter++;

        /* ⏳ PERIODIC TELEMETRY HEARTBEAT WINDOW */
        if (heartbeat_counter >= HEARTBEAT_INTERVAL_SEC) {
            heartbeat_counter = 0;
            esp_send_http_post(uptime_seconds, 0, "HEARTBEAT");
        }

        /* If ISR recorded rain events, log them (with uptime) and keep flag for handler */
        if (rain_event_count) {
            uart_prefix_uptime(); uart_puts("RAIN event(s)="); uart_putu32(rain_event_count); uart_putln("");
            /* consume events and leave rain_detected=true so trigger logic runs */
            rain_event_count = 0;
        }

        /* Trigger condition: interval elapsed OR rain detected */
        if (timer_counter >= CHECK_INTERVAL_SEC || rain_detected) {
            uart_prefix_uptime(); uart_putln("TRIGGER");
            rain_detected = false;
            timer_counter = 0;

            /* Prime the pump for a short burst */
            uart_prefix_uptime(); uart_putln("PRIME: ON");
            turn_pump_on();
            flow_pulse_count = 0;
            delay_seconds(PRIME_SECONDS);

            uart_prefix_uptime(); uart_puts("PRIME: pulses="); uart_putu32(flow_pulse_count); uart_putln("");

            /* Evaluate initial flow */
            if (flow_pulse_count < PRIME_FLOW_MIN_PULSES) {
                /* No significant flow detected -> stop immediately */
                uart_prefix_uptime(); uart_putln("PRIME: no flow, OFF");
                turn_pump_off();
                esp_send_http_post(uptime_seconds, flow_pulse_count, "CHECK_DRY");
            } else {
                uart_prefix_uptime(); uart_putln("FLOW: detected, keep pumping");
                /* Keep pumping while flow continues */
                while (1) {
                    flow_pulse_count = 0;
                    delay_seconds(FLOW_CHECK_WINDOW_SEC);
                    uart_prefix_uptime(); uart_puts("WINDOW: pulses="); uart_putu32(flow_pulse_count); uart_putln("");

                    if (flow_pulse_count < FLOW_KEEPALIVE_MIN_PULSES) {
                        uart_prefix_uptime(); uart_putln("FLOW: stopped, OFF");
                        break;
                    }
                }
                turn_pump_off();
                esp_send_http_post(uptime_seconds, flow_pulse_count, "DRAIN_COMPLETE");
            }
            
            /* Synchronize counters so a regular heartbeat isn't fired right after a cycle completes */
            heartbeat_counter = 0;
        }
    }

    return 0;
}

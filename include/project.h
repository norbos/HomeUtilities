/* Project-wide definitions and declarations for HomeRoofDrain */

#ifndef PROJECT_H
#define PROJECT_H

#include <avr/io.h>

/* Pump control (relay input) */
#define PUMP_PORT PORTB
#define PUMP_DDR  DDRB
#define PUMP_PIN  PB0 /* Arduino digital pin 8 */

/* Flow sensor (pulse output) */
#define FLOW_PIN  PD2 /* INT0, Arduino digital pin 2 */

/* Rain sensor (digital out) */
#define RAIN_PIN  PD3 /* INT1, Arduino digital pin 3 */

#endif /* PROJECT_H */

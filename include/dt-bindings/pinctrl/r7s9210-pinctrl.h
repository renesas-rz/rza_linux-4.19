/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Defines macros and constants for Renesas RZ/A2 pin controller pin
 * muxing functions.
 */
#ifndef __DT_BINDINGS_PINCTRL_RENESAS_RZA2_H
#define __DT_BINDINGS_PINCTRL_RENESAS_RZA2_H

#define RZA2_PINS_PER_PORT	8

/* Port names as labeled in the Hardware Manual */
#define P0 0
#define P1 1
#define P2 2
#define P3 3
#define P4 4
#define P5 5
#define P6 6
#define P7 7
#define P8 8
#define P9 9
#define PA 10
#define PB 11
#define PC 12
#define PD 13
#define PE 14
#define PF 15
#define PG 16
#define PH 17
/* No I */
#define PJ 18
#define PK 19
#define PL 20
#define PM 21

/*
 * Create the pin index from its bank and position numbers and store in
 * the upper 8 bits the alternate function identifier
 */
#define RZA2_PINMUX(b, p, f)	((b) * RZA2_PINS_PER_PORT + (p) | (f << 16))

/*
 * Convert a port and pin label to its global pin index
 */
 #define RZA2_PIN(port, pin)	((port) * RZA2_PINS_PER_PORT + (pin))

#endif /* __DT_BINDINGS_PINCTRL_RENESAS_RZA2_H */

/* ============================================================================
 *  board_pins.h  -  ESP32-C6 GPIO assignment for this firmware (Front lighting)
 *
 *  All numbers are GPIO numbers (digitalRead(2) == GPIO2).  Change ONLY this file
 *  when the PCB routing differs.
 *
 *  Avoid : GPIO4,5,8,9,15 (strapping)  GPIO12,13 (USB)  GPIO16,17 (UART0)  GPIO24..30 (flash)
 *  Usable: GPIO0,1,2,3,6,7,10,11,14,18,19,20,21,22,23   (ADC1 = GPIO0..6)
 *
 *  NOTE: LED_PIN = GPIO6 (unified for all lighting boards).  GPIO5 was tried first but did NOT drive
 *        the strip on the SuperMini C6 (GPIO5 is a strapping pin); GPIO6 works.
 *
 *  ESP32-C6 I/O is 3.3 V and NOT 5 V tolerant (RF receiver outputs, WS2812 data -> level shift).
 * ========================================================================== */
#pragma once

#define LED_PIN        6          /* NeoPixel data (all lighting boards unified to GPIO6, see note) */

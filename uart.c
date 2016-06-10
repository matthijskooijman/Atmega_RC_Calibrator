/*
 * Copyright 2011 Mika Tuupola
 * Copyright 2016 Matthijs Kooijman <matthijs@stdin.nl>
 *
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Taken from http://appelsiini.net/2011/simple-usart-with-avr-libc
 *
 * Modified by Matthijs Kooijman to add uart_flush() and
 * uart_shutdown().
 */
#include "uart.h"
#include <avr/io.h>

#ifndef BAUD
#define BAUD 9600
#endif
#include <util/setbaud.h>

FILE uart_output = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
FILE uart_input = FDEV_SETUP_STREAM(NULL, uart_getchar, _FDEV_SETUP_READ);

/* http://www.cs.mun.ca/~rod/Winter2007/4723/notes/serial/serial.html */

void uart_init(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;

#if USE_2X
    UCSR0A |= _BV(U2X0);
#else
    UCSR0A &= ~(_BV(U2X0));
#endif

    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); /* 8-bit data */
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);   /* Enable RX and TX */
}

void uart_putchar(char c, FILE *stream) {
    if (c == '\n') {
        uart_putchar('\r', stream);
    }
    loop_until_bit_is_set(UCSR0A, UDRE0);
    UDR0 = c;
    // Clear TX complete bit
    UCSR0A |= (1 << TXC0);
}

char uart_getchar(FILE *stream) {
    loop_until_bit_is_set(UCSR0A, RXC0);
    return UDR0;
}

void uart_shutdown(void) {
    UCSR0B = 0;
}

void uart_flush(void) {
    loop_until_bit_is_set(UCSR0A, TXC0);
}

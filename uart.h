#pragma once
#include <stdio.h>

void uart_putchar(char c, FILE *stream);
char uart_getchar(FILE *stream);
void uart_flush(void);

void uart_init(void);
void uart_shutdown(void);

/* http://www.ermicro.com/blog/?p=325 */

extern FILE uart_output;
extern FILE uart_input;

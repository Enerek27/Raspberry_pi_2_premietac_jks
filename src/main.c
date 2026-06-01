// main.c
#include <stdio.h>
#include <stdlib.h>

#include "premietac.h"
#include "uart.h"

int main(void) {
  int fd = uart_init();
  if (fd < 0) {
    return EXIT_FAILURE;
  }

  // Obrázok, ktorý sa má zobraziť mimo prezentácie
  premietac_run_raylib(fd, "pozadie.png");

  uart_close(fd);
  return EXIT_SUCCESS;
}
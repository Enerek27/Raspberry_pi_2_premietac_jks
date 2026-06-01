#include <stdlib.h>

#include "premietac.h"
#include "uart.h"

int main(void) {
  int fd = uart_init();
  if (fd < 0)
    return EXIT_FAILURE;

  premietac_run_raylib(fd, "../pozadie.png"); // alebo "pozadie.png" podľa cesty
  uart_close(fd);
  return EXIT_SUCCESS;
}
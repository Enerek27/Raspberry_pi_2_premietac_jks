#include <stdlib.h>

#include "premietac.h"
#include "wifi.h"

int main(void) {
  int fd = uart_init();
  if (fd < 0)
    return EXIT_FAILURE;

  premietac_run_raylib("../pozadie.png"); // 
  // cesty
  // testing_ray("../pozadie.png");
  uart_close(fd);
  return EXIT_SUCCESS;
}
// premietac.h
#ifndef PREMIETAC_H
#define PREMIETAC_H

#include "protokol.h"

// Spustí raylib fullscreen premietanie na danom UART fd.
// background_path – cesta k úvodnému obrázku (napr. "background.png").
void premietac_run_raylib(int uart_fd, const char *background_path);

#endif
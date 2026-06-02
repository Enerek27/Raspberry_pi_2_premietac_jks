// premietac.c
#include "premietac.h"
#include "uart.h"

#include "raylib.h" // potrebuješ mať nainštalovaný raylib
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pomocný výpočet obdĺžnika na fullscreen obrázok so zachovaním pomeru strán
static void compute_fullscreen_dest(int texW, int texH, int screenW,
                                    int screenH, Rectangle *src,
                                    Rectangle *dest) {
  float texAspect = (float)texW / (float)texH;
  float screenAspect = (float)screenW / (float)screenH;

  if (texAspect > screenAspect) {
    // obraz je širší ako displej -> prispôsob výšku
    float scale = (float)screenH / (float)texH;
    float newW = texW * scale;
    dest->width = newW;
    dest->height = (float)screenH;
    dest->x = (screenW - newW) / 2.0f;
    dest->y = 0.0f;
  } else {
    // obraz je vyšší -> prispôsob šírku
    float scale = (float)screenW / (float)texW;
    float newH = texH * scale;
    dest->width = (float)screenW;
    dest->height = newH;
    dest->x = 0.0f;
    dest->y = (screenH - newH) / 2.0f;
  }

  src->x = 0.0f;
  src->y = 0.0f;
  src->width = (float)texW;
  src->height = (float)texH;
}

// Vráti text aktuálnej slohy (alebo prázdny string).
static const char *get_current_sloha_text(StavPremietania *stav) {
  if (!stav->bezi || stav->blackscreen)
    return "";

  Sloha *sloha = stav_get_sloha(stav, stav->cislo_piesne, stav->cislo_slohy);
  if (!sloha)
    return "";
  return sloha->text;
}

void premietac_run_raylib(int uart_fd, const char *background_path) {
  // Rovnaký trik čo fungoval - UNDECORATED je spoľahlivejší na RPi
  SetConfigFlags(FLAG_WINDOW_UNDECORATED);

  InitWindow(800, 600, "Premietac"); // dočasná veľkosť

  // Monitor query AŽ PO InitWindow
  int monitor = GetCurrentMonitor();
  int screenWidth = GetMonitorWidth(monitor);
  int screenHeight = GetMonitorHeight(monitor);

  if (screenWidth == 0 || screenHeight == 0) {
    screenWidth = GetScreenWidth();
    screenHeight = GetScreenHeight();
  }

  SetWindowSize(screenWidth, screenHeight);
  SetWindowPosition(0, 0);

  SetTargetFPS(60);

  printf("[Premietac] Rozlisenie: %d x %d\n", screenWidth, screenHeight);

  // Načítaj pozadie
  Texture2D background = LoadTexture(background_path);
  if (background.id == 0) {
    printf("[Premietac] CHYBA: Nepodarilo sa nacitat obrazok '%s'\n",
           background_path);
  } else {
    printf("[Premietac] OK: nacitane pozadie %s (%d x %d)\n", background_path,
           background.width, background.height);
  }

  // Stav
  StavPremietania *stav = malloc(sizeof(StavPremietania));
  if (!stav) {
    fprintf(stderr, "[Premietac] malloc(StavPremietania) zlyhal\n");
    if (background.id != 0)
      UnloadTexture(background);
    CloseWindow();
    return;
  }
  stav_init(stav);

  // Debug - skontroluj počiatočný stav
  printf("[Premietac] stav->bezi po init = %d\n", stav->bezi);

  char line[UART_BUF_SIZE];
  char reasm_buf[REASM_BUF_SIZE];
  int reasm_len = 0;
  reasm_buf[0] = '\0';
  ParseovanyPrikaz prikaz;

  int fontSize = 48;

  while (!WindowShouldClose()) {
    int len = uart_read_line_nonblock(uart_fd, line, sizeof(line));
    if (len < 0)
      break;
    if (len > 0) {
      if (reasm_pridaj_chunk(reasm_buf, &reasm_len, line, &prikaz)) {
        stav_aplikuj(stav, &prikaz);
      }
    }

    BeginDrawing();
    ClearBackground(BLACK); // jeden ClearBackground na začiatku stačí

    if (!stav->bezi) {
      // Pozadie
      if (background.id != 0) {
        DrawTexturePro(
            background,
            (Rectangle){0, 0, (float)background.width,
                        (float)background.height},
            (Rectangle){0, 0, (float)screenWidth, (float)screenHeight},
            (Vector2){0, 0}, 0.0f, WHITE);
      }
    } else {
      // Prezentácia
      if (!stav->blackscreen) {
        const char *txt = get_current_sloha_text(stav);
        int textWidth = MeasureText(txt, fontSize);
        int x = (screenWidth - textWidth) / 2;
        int y = (screenHeight - fontSize) / 2;
        DrawText(txt, x, y, fontSize, WHITE);
      }
      // blackscreen = len čierna, ClearBackground(BLACK) už bolo vyššie
    }

    EndDrawing();
  }

  if (background.id != 0)
    UnloadTexture(background);
  CloseWindow();
  free(stav);
}

void testing_ray(const char *background_path) {
  SetConfigFlags(FLAG_WINDOW_UNDECORATED);

  InitWindow(800, 600, "Testing");

  int monitor = GetCurrentMonitor();
  int w = GetMonitorWidth(monitor);
  int h = GetMonitorHeight(monitor);

  if (w == 0 || h == 0) {
    w = GetScreenWidth();
    h = GetScreenHeight();
  }

  SetWindowSize(w, h);
  SetWindowPosition(0, 0);
  SetTargetFPS(60);

  Texture2D texture = LoadTexture(background_path);
  if (texture.id == 0) {
    printf("[Testing] CHYBA: Nepodarilo sa nacitat '%s'\n", background_path);
  } else {
    printf("[Testing] OK: %dx%d\n", texture.width, texture.height);
  }

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(BLACK);

    if (texture.id != 0) {
      Rectangle src, dest;
      compute_fullscreen_dest(texture.width, texture.height, w, h, &src, &dest);
      DrawTexturePro(texture, src, dest, (Vector2){0, 0}, 0.0f, WHITE);
    }

    EndDrawing();
  }

  UnloadTexture(texture);
  CloseWindow();
}
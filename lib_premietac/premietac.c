// premietac.c
#include "premietac.h"
#include "uart.h"

#include "raylib.h" // potrebuješ mať nainštalovaný raylib
#include <stdio.h>
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
  // ── raylib init na celý aktuálny monitor ─────────────────────────────
  int monitor = GetCurrentMonitor();
  int screenWidth = GetMonitorWidth(monitor);
  int screenHeight = GetMonitorHeight(monitor); //[web:13][web:16]

  InitWindow(screenWidth, screenHeight, "Premietac");
  SetTargetFPS(60);
  ToggleFullscreen(); //[web:10][web:16]

  // Načítaj obrázok na pozadie (úvodný / medzi prezentáciami)
  Texture2D background = LoadTexture(background_path);
  if (background.id == 0) {
    printf("[Premietac] Nepodarilo sa načítať obrázok '%s'\n", background_path);
  }

  Rectangle bgSrc = {0};
  Rectangle bgDst = {0};
  if (background.id != 0) {
    compute_fullscreen_dest(background.width, background.height, screenWidth,
                            screenHeight, &bgSrc, &bgDst); //[web:20]
  }

  // ── Stav protokolu / piesní ──────────────────────────────────────────
  StavPremietania stav;
  stav_init(&stav);

  char line[UART_BUF_SIZE];
  char reasm_buf[REASM_BUF_SIZE];
  int reasm_len = 0;
  reasm_buf[0] = '\0';
  ParseovanyPrikaz prikaz;

  // Font nastavenia
  int fontSize = 48;
  int lineSpacing = 8;

  while (!WindowShouldClose()) {
    // ── 1) UART: neblokujúce čítanie, spracovanie príkazov ───────────
    int len = uart_read_line_nonblock(uart_fd, line, sizeof(line));
    if (len < 0) {
      printf("[Premietac] Chyba čítania UART, končím\n");
      break;
    }
    if (len > 0) {
#ifdef DEBUG
      printf("[RAW] %s\n", line);
#endif
      if (reasm_pridaj_chunk(reasm_buf, &reasm_len, line, &prikaz)) {
        // Máme komplet správu – zmení stav, vrátane
        // SPUSTI/VYPNI/ZATMAV/ODTMAV/POSLI_PIESEN/...
        stav_aplikuj(&stav, &prikaz);
      }
    }

    // ── 2) Kreslenie ────────────────────────────────────────────────
    BeginDrawing();

    // Ak nebeží premietanie, zobraz úvodný obrázok
    if (!stav.bezi) {
      ClearBackground(BLACK);
      if (background.id != 0) {
        DrawTexturePro(background, bgSrc, bgDst, (Vector2){0, 0}, 0.0f, WHITE);
      } else {
        DrawText("CHYBA: background nenajdeny", 20, 20, 30, RED);
      }
    } else {
      // Premietanie beží
      if (stav.blackscreen) {
        ClearBackground(BLACK);
      } else {
        ClearBackground(BLACK);

        const char *txt = get_current_sloha_text(&stav);

        // jednoduché centrovanie textu horizontálne – MeasureText v
        // pixeloch[web:15][web:18]
        int textWidth = MeasureText(txt, fontSize);
        int x = (screenWidth - textWidth) / 2;
        int y = screenHeight / 2 - fontSize / 2;

        // Ak máš viacriadkový text, môžeš ho rozdeliť na riadky a kresliť po
        // riadkoch.
        DrawText(txt, x, y, fontSize, WHITE);
      }
    }

    EndDrawing();
  }

  // Upratovanie
  if (background.id != 0)
    UnloadTexture(background);
  CloseWindow();
}
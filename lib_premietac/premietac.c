// premietac.c
#include "premietac.h"
#include "protokol.h"
#include "uart.h"

#include "raylib.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // nanosleep

// ─────────────────────────────────────────────────────────────
// Pomocné výpočty a typy
// ─────────────────────────────────────────────────────────────

// Pomocný výpočet obdĺžnika na fullscreen obrázok so zachovaním pomeru strán
static void compute_fullscreen_dest(int texW, int texH, int screenW,
                                    int screenH, Rectangle *src,
                                    Rectangle *dest) {
  float texAspect = (float)texW / (float)texH;
  float screenAspect = (float)screenW / (float)screenH;

  if (texAspect > screenAspect) {
    // obraz je širší -> prispôsob výšku
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

// Zdieľaný stav medzi UART vláknom a render vláknom
typedef struct {
  StavPremietania stav;
  pthread_mutex_t mutex;
  int uart_fd;
  int stop; // 1 = ukonči UART vlákno
} ZdielanyStav;

// ─────────────────────────────────────────────────────────────
// UART vlákno – číta sériovku a aplikuje príkazy na stav
// ─────────────────────────────────────────────────────────────

static void *uart_thread(void *arg) {
  ZdielanyStav *zs = (ZdielanyStav *)arg;

  char line[UART_BUF_SIZE];
  char reasm_buf[REASM_BUF_SIZE];
  int reasm_len = 0;
  reasm_buf[0] = '\0';

  ParseovanyPrikaz prikaz;

  printf("[UART vlákno] Štartujem, fd=%d\n", zs->uart_fd);

  while (!zs->stop) {
    int len = uart_read_line_nonblock(zs->uart_fd, line, sizeof(line));

    if (len < 0) {
      fprintf(stderr, "[UART vlákno] Chyba čítania (len=%d), končím\n", len);
      break;
    }

    if (len > 0) {
      // odstráň koncový \n/\r pre krajší výpis
      if (line[len - 1] == '\n' || line[len - 1] == '\r') {
        line[len - 1] = '\0';
      }

      printf("[UART] chunk(len=%d): \"%s\"\n", len, line);

      int kompletne = reasm_pridaj_chunk(reasm_buf, &reasm_len, line, &prikaz);
      printf("[UART] reasm_len=%d, kompletne=%d\n", reasm_len, kompletne);

      if (kompletne) {
        printf("[UART] >>> PARSOVANY PRIKAZ: typ=%s param1=%d param2=%d "
               "text_len=%zu\n",
               typ_prikazu_str(prikaz.typ), prikaz.param1, prikaz.param2,
               strlen(prikaz.text));

        pthread_mutex_lock(&zs->mutex);
        stav_aplikuj(&zs->stav, &prikaz);
        printf("[UART] Stav po aplikovani: bezi=%d, blackscreen=%d, piesen=%d, "
               "sloha=%d\n",
               zs->stav.bezi, zs->stav.blackscreen, zs->stav.cislo_piesne,
               zs->stav.cislo_slohy);
        pthread_mutex_unlock(&zs->mutex);
      }
    } else {
      // nič neprišlo, krátky spánok aby CPU nelietalo na 100 %
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 5 * 1000000L; // 5 ms
      nanosleep(&ts, NULL);
    }
  }

  printf("[UART vlákno] Koniec\n");
  return NULL;
}

// ─────────────────────────────────────────────────────────────
// Font s podporou slovenčiny + viacriadkový text
// ─────────────────────────────────────────────────────────────

// Načítanie fontu s ASCII + slovenské diakritiky
static Font LoadSlovakFont(const char *path, int fontSize) {
  int codepoints[256];
  int count = 0;

  // Základný ASCII rozsah
  for (int c = 32; c <= 126; c++) {
    codepoints[count++] = c;
  }

  // Slovenské znaky (malé + veľké)
  int extra[] = {
      0x010D, // č
      0x010C, // Č
      0x0161, // š
      0x0160, // Š
      0x017E, // ž
      0x017D, // Ž
      0x013E, // ľ
      0x013D, // Ľ
      0x0165, // ť
      0x0164, // Ť
      0x0148, // ň
      0x0147, // Ň
      0x00E1, // á
      0x00C1, // Á
      0x00E9, // é
      0x00C9, // É
      0x00ED, // í
      0x00CD, // Í
      0x00F3, // ó
      0x00D3, // Ó
      0x00FA, // ú
      0x00DA, // Ú
      0x00FD, // ý
      0x00DD, // Ý
  };
  int extraCount = sizeof(extra) / sizeof(extra[0]);
  for (int i = 0; i < extraCount &&
                  count < (int)(sizeof(codepoints) / sizeof(codepoints[0]));
       i++) {
    codepoints[count++] = extra[i];
  }

  Font f = LoadFontEx(path, fontSize, codepoints, count);
  if (f.texture.id == 0) {
    // fallback – ak by sa font nenačítal
    return GetFontDefault();
  }
  return f;
}

// Nakreslí text s viacerými riadkami (oddelenými \n), centrovaný na obrazovke
static void DrawCenteredMultilineText(Font font, const char *text,
                                      int screenWidth, int screenHeight,
                                      float fontSize, float lineSpacing,
                                      Color color) {
  if (!text || !text[0])
    return;

  // Spočítaj riadky
  int lineCount = 1;
  for (const char *p = text; *p; p++) {
    if (*p == '\n')
      lineCount++;
  }

  float lineHeight = fontSize + lineSpacing;
  float totalHeight = lineCount * lineHeight - lineSpacing;
  float startY = (screenHeight - totalHeight) / 2.0f;

  char lineBuf[MAX_TEXT_LEN];
  int lineIdx = 0;
  float y = startY;

  const char *p = text;
  while (*p) {
    if (*p == '\n') {
      lineBuf[lineIdx] = '\0';

      Vector2 size = MeasureTextEx(font, lineBuf, fontSize, 0);
      float x = (screenWidth - size.x) / 2.0f;

      DrawTextEx(font, lineBuf, (Vector2){x, y}, fontSize, 0, color);

      y += lineHeight;
      lineIdx = 0;
      p++;
      continue;
    }

    if (lineIdx < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineIdx++] = *p;
    }
    p++;
  }

  if (lineIdx > 0) {
    lineBuf[lineIdx] = '\0';
    Vector2 size = MeasureTextEx(font, lineBuf, fontSize, 0);
    float x = (screenWidth - size.x) / 2.0f;
    DrawTextEx(font, lineBuf, (Vector2){x, y}, fontSize, 0, color);
  }
}

// ─────────────────────────────────────────────────────────────
// Premietanie – hlavná funkcia s Raylib loopom
// ─────────────────────────────────────────────────────────────

void premietac_run_raylib(int uart_fd, const char *background_path) {
  // ZDIELANÝ STAV NA HEAPE (dynamicky), nie na zásobníku
  ZdielanyStav *zs = malloc(sizeof(ZdielanyStav));
  if (!zs) {
    fprintf(stderr, "[Premietac] malloc(ZdielanyStav) zlyhal\n");
    return;
  }

  stav_init(&zs->stav);
  if (pthread_mutex_init(&zs->mutex, NULL) != 0) {
    fprintf(stderr, "[Premietac] pthread_mutex_init zlyhal\n");
    free(zs);
    return;
  }
  zs->uart_fd = uart_fd;
  zs->stop = 0;

  // Spusti UART vlákno
  pthread_t tid;
  if (pthread_create(&tid, NULL, uart_thread, zs) != 0) {
    fprintf(stderr, "[Premietac] Nepodarilo sa vytvoriť UART vlákno\n");
    pthread_mutex_destroy(&zs->mutex);
    free(zs);
    return;
  }

  // Raylib okno (hlavné vlákno)
  SetConfigFlags(FLAG_WINDOW_UNDECORATED);
  InitWindow(800, 600, "Premietac");

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

  // Font so slovenskými znakmi – súbor daj vedľa binárky (napr. DejaVuSans.ttf)
  int fontSize = 48;
  Font uiFont = LoadSlovakFont("../NotoSans-Regular.ttf", fontSize);

  Texture2D background = LoadTexture(background_path);
  if (background.id == 0) {
    printf("[Premietac] CHYBA: Nepodarilo sa nacitat obrazok '%s'\n",
           background_path);
  } else {
    printf("[Premietac] OK: nacitane pozadie %s (%d x %d)\n", background_path,
           background.width, background.height);
  }

  // Buffer na text, aby sme nekreslili priamo pointer do zdieľaného stavu
  char txt_buf[MAX_TEXT_LEN];

  while (!WindowShouldClose()) {
    // Lokálna kópia základných hodnôt stavu + text v bufri
    int bezi;
    int blackscreen;
    int32_t cislo_piesne;
    int32_t cislo_slohy;

    txt_buf[0] = '\0';

    // Čítanie stavu pod mutexom – čo najkratšie
    pthread_mutex_lock(&zs->mutex);

    bezi = zs->stav.bezi;
    blackscreen = zs->stav.blackscreen;
    cislo_piesne = zs->stav.cislo_piesne;
    cislo_slohy = zs->stav.cislo_slohy;

    if (bezi && !blackscreen) {
      Sloha *sloha = stav_get_sloha(&zs->stav, cislo_piesne, cislo_slohy);
      if (sloha) {
        snprintf(txt_buf, sizeof(txt_buf), "%s", sloha->text);
      }
    }

    pthread_mutex_unlock(&zs->mutex);

    // DEBUG: vypíš stav každých pár frame-ov
    static int frameCounter = 0;
    frameCounter++;
    if (frameCounter % 60 == 0) {
      printf(
          "[RENDER] bezi=%d blackscreen=%d piesen=%d sloha=%d txt_empty=%d\n",
          bezi, blackscreen, cislo_piesne, cislo_slohy, (txt_buf[0] == '\0'));
    }

    // Render
    BeginDrawing();
    ClearBackground(BLACK);

    if (!bezi) {
      // Pozadie
      if (background.id != 0) {
        Rectangle src, dest;
        compute_fullscreen_dest(background.width, background.height,
                                screenWidth, screenHeight, &src, &dest);
        DrawTexturePro(background, src, dest, (Vector2){0, 0}, 0.0f, WHITE);
      }
    } else {
      // Prezentácia
      if (!blackscreen && txt_buf[0] != '\0') {
        DrawCenteredMultilineText(uiFont, txt_buf, screenWidth, screenHeight,
                                  (float)fontSize,
                                  8.0f, // medzera medzi riadkami
                                  WHITE);
      }
      // blackscreen == 1 => len čierna obrazovka
    }

    EndDrawing();
  }

  // Upratanie
  zs->stop = 1;
  pthread_join(tid, NULL);
  pthread_mutex_destroy(&zs->mutex);

  if (background.id != 0)
    UnloadTexture(background);

  if (uiFont.texture.id != 0 &&
      uiFont.texture.id != GetFontDefault().texture.id)
    UnloadFont(uiFont);

  CloseWindow();
  free(zs);
}

// ─────────────────────────────────────────────────────────────
// Jednoduchý test bez UART – len fullscreen obrázok
// ─────────────────────────────────────────────────────────────

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
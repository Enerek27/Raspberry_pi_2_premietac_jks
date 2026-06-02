// premietac.c
#include "premietac.h"
#include "protokol.h"
#include "uart.h"

#include "raylib.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────
// Konštanty
// ─────────────────────────────────────────────────────────────

#define FONT_SIZE_MIN 20
#define FONT_SIZE_MAX 100 // RPi GPU nezvláda 300 – textura by bola 8192x8192
#define SCREEN_PADDING 60
#define LINE_SPACING 0.15f

// ─────────────────────────────────────────────────────────────
// Pomocné výpočty
// ─────────────────────────────────────────────────────────────

static void compute_fullscreen_dest(int texW, int texH, int screenW,
                                    int screenH, Rectangle *src,
                                    Rectangle *dest) {
  float texAspect = (float)texW / (float)texH;
  float screenAspect = (float)screenW / (float)screenH;

  if (texAspect > screenAspect) {
    float scale = (float)screenH / (float)texH;
    float newW = texW * scale;
    dest->width = newW;
    dest->height = (float)screenH;
    dest->x = (screenW - newW) / 2.0f;
    dest->y = 0.0f;
  } else {
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

// ─────────────────────────────────────────────────────────────
// Zdieľaný stav
// ─────────────────────────────────────────────────────────────

typedef struct {
  StavPremietania stav;
  pthread_mutex_t mutex;
  int uart_fd;
  int stop;
} ZdielanyStav;

// ─────────────────────────────────────────────────────────────
// UART vlákno
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
      if (line[len - 1] == '\n' || line[len - 1] == '\r')
        line[len - 1] = '\0';

      printf("[UART] chunk(len=%d): \"%s\"\n", len, line);

      int kompletne = reasm_pridaj_chunk(reasm_buf, &reasm_len, line, &prikaz);
      printf("[UART] reasm_len=%d, kompletne=%d\n", reasm_len, kompletne);

      if (kompletne) {
        printf("[UART] >>> PRIKAZ: typ=%s param1=%d param2=%d text_len=%zu\n",
               typ_prikazu_str(prikaz.typ), prikaz.param1, prikaz.param2,
               strlen(prikaz.text));

        pthread_mutex_lock(&zs->mutex);
        stav_aplikuj(&zs->stav, &prikaz);
        printf("[UART] Stav: bezi=%d black=%d piesen=%d sloha=%d\n",
               zs->stav.bezi, zs->stav.blackscreen, zs->stav.cislo_piesne,
               zs->stav.cislo_slohy);
        pthread_mutex_unlock(&zs->mutex);
      }
    } else {
      struct timespec ts = {.tv_sec = 0, .tv_nsec = 5 * 1000000L};
      nanosleep(&ts, NULL);
    }
  }

  printf("[UART vlákno] Koniec\n");
  return NULL;
}

// ─────────────────────────────────────────────────────────────
// Font so slovenskou diakritikou
// ─────────────────────────────────────────────────────────────

static Font LoadSlovakFont(const char *path, int fontSize) {
  int codepoints[300];
  int count = 0;

  for (int c = 32; c <= 126; c++)
    codepoints[count++] = c;

  int extra[] = {
      0x010D, 0x010C, // č Č
      0x0161, 0x0160, // š Š
      0x017E, 0x017D, // ž Ž
      0x013E, 0x013D, // ľ Ľ
      0x0165, 0x0164, // ť Ť
      0x0148, 0x0147, // ň Ň
      0x010F, 0x010E, // ď Ď
      0x0155, 0x0154, // ŕ Ŕ
      0x013A, 0x0139, // ĺ Ĺ
      0x00E1, 0x00C1, // á Á
      0x00E9, 0x00C9, // é É
      0x00ED, 0x00CD, // í Í
      0x00F3, 0x00D3, // ó Ó
      0x00FA, 0x00DA, // ú Ú
      0x00FD, 0x00DD, // ý Ý
      0x00F4, 0x00D4, // ô Ô
      0x00E4, 0x00C4, // ä Ä
      0x2013,         // –
      0x2014,         // —
      0x201E,         // „
      0x201C,         // "
      0x00AB,         // «
      0x00BB,         // »
  };

  int extraCount = sizeof(extra) / sizeof(extra[0]);
  for (int i = 0; i < extraCount &&
                  count < (int)(sizeof(codepoints) / sizeof(codepoints[0]));
       i++)
    codepoints[count++] = extra[i];

  Font f = LoadFontEx(path, fontSize, codepoints, count);
  if (f.texture.id == 0) {
    fprintf(stderr, "[Font] Nepodarilo sa načítať '%s', používam default\n",
            path);
    return GetFontDefault();
  }

  printf("[Font] Načítaný: %s | texture: %dx%d | glyfov: %d\n", path,
         f.texture.width, f.texture.height, count);

  SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
  return f;
}

// ─────────────────────────────────────────────────────────────
// Auto-veľkosť – binárne hľadanie
// ─────────────────────────────────────────────────────────────

static int split_lines(const char *text, const char *lines_start[],
                       int lines_len[], int max_lines) {
  int count = 0;
  const char *p = text;

  while (*p && count < max_lines) {
    lines_start[count] = p;
    const char *nl = strchr(p, '\n');
    if (nl) {
      lines_len[count] = (int)(nl - p);
      p = nl + 1;
    } else {
      lines_len[count] = (int)strlen(p);
      p += lines_len[count];
    }
    count++;
  }

  return count;
}

static void measure_text_block(Font font, const char *lines_start[],
                               const int lines_len[], int line_count,
                               float fontSize, float *out_max_width,
                               float *out_total_height) {
  float spacing = fontSize * LINE_SPACING;
  float lineHeight = fontSize + spacing;
  float maxW = 0.0f;
  char tmp[MAX_TEXT_LEN];

  for (int i = 0; i < line_count; i++) {
    int len = lines_len[i];
    if (len >= (int)sizeof(tmp))
      len = (int)sizeof(tmp) - 1;
    memcpy(tmp, lines_start[i], len);
    tmp[len] = '\0';

    Vector2 sz = MeasureTextEx(font, tmp, fontSize, 0);
    if (sz.x > maxW)
      maxW = sz.x;
  }

  *out_max_width = maxW;
  *out_total_height = line_count * lineHeight - spacing;
}

static float find_optimal_font_size(Font font, const char *text, int screenW,
                                    int screenH) {
  const char *lines_start[512];
  int lines_len[512];
  int line_count = split_lines(text, lines_start, lines_len, 512);

  if (line_count == 0)
    return (float)FONT_SIZE_MIN;

  float maxW = (float)(screenW - 2 * SCREEN_PADDING);
  float maxH = (float)(screenH - 2 * SCREEN_PADDING);

  int lo = FONT_SIZE_MIN;
  int hi = FONT_SIZE_MAX;
  int best = FONT_SIZE_MIN;

  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    float w, h;
    measure_text_block(font, lines_start, lines_len, line_count, (float)mid, &w,
                       &h);

    if (w <= maxW && h <= maxH) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }

  return (float)best;
}

// ─────────────────────────────────────────────────────────────
// Kreslenie textu
// ─────────────────────────────────────────────────────────────

static void DrawCenteredMultilineText(Font font, const char *text,
                                      int screenWidth, int screenHeight,
                                      float fontSize, Color color) {
  if (!text || !text[0])
    return;

  float spacing = fontSize * LINE_SPACING;
  float lineHeight = fontSize + spacing;

  const char *lines_start[512];
  int lines_len[512];
  int line_count = split_lines(text, lines_start, lines_len, 512);

  float totalHeight = line_count * lineHeight - spacing;
  float y = (screenHeight - totalHeight) / 2.0f;

  char tmp[MAX_TEXT_LEN];

  for (int i = 0; i < line_count; i++) {
    int len = lines_len[i];
    if (len >= (int)sizeof(tmp))
      len = (int)sizeof(tmp) - 1;
    memcpy(tmp, lines_start[i], len);
    tmp[len] = '\0';

    Vector2 sz = MeasureTextEx(font, tmp, fontSize, 0);
    float x = (screenWidth - sz.x) / 2.0f;

    DrawTextEx(font, tmp, (Vector2){x, y}, fontSize, 0, color);
    y += lineHeight;
  }
}

// ─────────────────────────────────────────────────────────────
// Hlavná funkcia
// ─────────────────────────────────────────────────────────────

void premietac_run_raylib(int uart_fd, const char *background_path) {
  ZdielanyStav *zs = malloc(sizeof(ZdielanyStav));
  if (!zs) {
    fprintf(stderr, "[Premietac] malloc zlyhal\n");
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

  pthread_t tid;
  if (pthread_create(&tid, NULL, uart_thread, zs) != 0) {
    fprintf(stderr, "[Premietac] Nepodarilo sa vytvoriť UART vlákno\n");
    pthread_mutex_destroy(&zs->mutex);
    free(zs);
    return;
  }

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

  Font uiFont = LoadSlovakFont("../NotoSans-Bold.ttf", FONT_SIZE_MAX);

  Texture2D background = LoadTexture(background_path);
  if (background.id == 0)
    printf("[Premietac] CHYBA pozadie: '%s'\n", background_path);
  else
    printf("[Premietac] OK pozadie: %s (%dx%d)\n", background_path,
           background.width, background.height);

  char txt_buf[MAX_TEXT_LEN];
  char last_txt[MAX_TEXT_LEN] = "";
  float cached_fontSize = (float)FONT_SIZE_MIN;

  int debug_overlay = 1; // zmeň na 0 keď bude fungovať

  while (!WindowShouldClose()) {
    int bezi, blackscreen;
    int32_t cislo_piesne, cislo_slohy;
    txt_buf[0] = '\0';

    pthread_mutex_lock(&zs->mutex);
    bezi = zs->stav.bezi;
    blackscreen = zs->stav.blackscreen;
    cislo_piesne = zs->stav.cislo_piesne;
    cislo_slohy = zs->stav.cislo_slohy;

    if (bezi && !blackscreen) {
      Sloha *sloha = stav_get_sloha(&zs->stav, cislo_piesne, cislo_slohy);
      if (sloha)
        snprintf(txt_buf, sizeof(txt_buf), "%s", sloha->text);
    }
    pthread_mutex_unlock(&zs->mutex);

    if (strcmp(txt_buf, last_txt) != 0) {
      strncpy(last_txt, txt_buf, sizeof(last_txt) - 1);
      last_txt[sizeof(last_txt) - 1] = '\0';

      if (txt_buf[0] != '\0')
        cached_fontSize =
            find_optimal_font_size(uiFont, txt_buf, screenWidth, screenHeight);
      printf("[RENDER] Novy text (fontSize=%.0f): \"%.80s\"\n", cached_fontSize,
             txt_buf);
    }

    BeginDrawing();
    ClearBackground(BLACK);

    if (!bezi) {
      if (background.id != 0) {
        Rectangle src, dest;
        compute_fullscreen_dest(background.width, background.height,
                                screenWidth, screenHeight, &src, &dest);
        DrawTexturePro(background, src, dest, (Vector2){0, 0}, 0.0f, WHITE);
      }
    } else {
      if (!blackscreen && txt_buf[0] != '\0') {
        DrawCenteredMultilineText(uiFont, txt_buf, screenWidth, screenHeight,
                                  cached_fontSize, WHITE);
      }
    }

    // DEBUG OVERLAY
    if (debug_overlay) {
      DrawTextEx(uiFont, "Font OK: sClôäÁÉ", (Vector2){10, 10}, 36, 0, RED);

      char dbg[256];
      snprintf(dbg, sizeof(dbg),
               "bezi=%d | black=%d | piesen=%d | sloha=%d | fs=%.0f", bezi,
               blackscreen, cislo_piesne, cislo_slohy, cached_fontSize);
      DrawTextEx(uiFont, dbg, (Vector2){10, 56}, 28, 0, YELLOW);

      if (txt_buf[0] != '\0') {
        char preview[128];
        snprintf(preview, sizeof(preview), "txt: \"%.60s\"", txt_buf);
        DrawTextEx(uiFont, preview, (Vector2){10, 94}, 24, 0, GREEN);
      } else {
        DrawTextEx(uiFont, "txt: (prazdny)", (Vector2){10, 94}, 24, 0, ORANGE);
      }
    }

    EndDrawing();
  }

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
// Test bez UART
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
  if (texture.id == 0)
    printf("[Testing] CHYBA: '%s'\n", background_path);
  else
    printf("[Testing] OK: %dx%d\n", texture.width, texture.height);

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
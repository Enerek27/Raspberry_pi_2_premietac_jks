// premietac.c
#include "premietac.h"
#include "raylib.h"
#include "uart.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─── Zdieľaná štruktúra medzi vláknami ───────────────────────────────────────

typedef struct {
  StavPremietania stav;
  pthread_mutex_t mutex;
  int uart_fd;
  int stop; // 1 = vlákno má skončiť
} ZdielanyStav;

// ─── UART vlákno ─────────────────────────────────────────────────────────────

static void *uart_thread(void *arg) {
  ZdielanyStav *zs = (ZdielanyStav *)arg;

  char line[UART_BUF_SIZE];
  char reasm_buf[REASM_BUF_SIZE];
  int reasm_len = 0;
  reasm_buf[0] = '\0';
  ParseovanyPrikaz prikaz;

  while (!zs->stop) {
    // Blokujúce čítanie je OK tu – kreslenie beží v hlavnom vlákne
    int len = uart_read_line_nonblock(zs->uart_fd, line, sizeof(line));

    if (len < 0) {
      fprintf(stderr, "[UART vlákno] Chyba čítania, končím\n");
      break;
    }

    if (len > 0) {
      printf("[UART] Prijaté: '%s'\n", line);

      if (reasm_pridaj_chunk(reasm_buf, &reasm_len, line, &prikaz)) {
        printf("[UART] Kompletný príkaz: %s\n", typ_prikazu_str(prikaz.typ));

        pthread_mutex_lock(&zs->mutex);
        stav_aplikuj(&zs->stav, &prikaz);
        pthread_mutex_unlock(&zs->mutex);
      }
    } else {
      // Nič neprišlo – krátka pauza aby sme nezahlcovali CPU
      struct timespec ts = {0, 5 * 1000000}; // 5 ms
      nanosleep(&ts, NULL);
    }
  }

  printf("[UART vlákno] Skončilo\n");
  return NULL;
}

// ─── Pomocné funkcie
// ──────────────────────────────────────────────────────────

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

static const char *get_current_sloha_text(StavPremietania *stav) {
  if (!stav->bezi || stav->blackscreen)
    return "";
  Sloha *sloha = stav_get_sloha(stav, stav->cislo_piesne, stav->cislo_slohy);
  if (!sloha)
    return "";
  return sloha->text;
}

// ─── Hlavná funkcia
// ───────────────────────────────────────────────────────────

void premietac_run_raylib(int uart_fd, const char *background_path) {

  // Inicializuj zdieľaný stav
  ZdielanyStav zs;
  stav_init(&zs.stav);
  pthread_mutex_init(&zs.mutex, NULL);
  zs.uart_fd = uart_fd;
  zs.stop = 0;

  // Spusti UART vlákno
  pthread_t tid;
  if (pthread_create(&tid, NULL, uart_thread, &zs) != 0) {
    fprintf(stderr, "[Premietac] Nepodarilo sa vytvoriť UART vlákno\n");
    pthread_mutex_destroy(&zs.mutex);
    return;
  }

  // ── Raylib init ──
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

  printf("[Premietac] Rozlíšenie: %d x %d\n", screenWidth, screenHeight);

  Texture2D background = LoadTexture(background_path);
  if (background.id == 0)
    printf("[Premietac] CHYBA: Nenačítané pozadie '%s'\n", background_path);
  else
    printf("[Premietac] OK: pozadie %dx%d\n", background.width,
           background.height);

  int fontSize = 48;

  // Lokálna kópia stavu pre kreslenie – aby sme držali mutex čo najkratšie
  StavPremietania lokalny_stav;

  // ── Render loop ──
  while (!WindowShouldClose()) {

    // Skopíruj stav pod mutexom
    pthread_mutex_lock(&zs.mutex);
    memcpy(&lokalny_stav, &zs.stav, sizeof(StavPremietania));
    pthread_mutex_unlock(&zs.mutex);

    BeginDrawing();
    ClearBackground(BLACK);

    if (!lokalny_stav.bezi) {
      // Pozadie
      if (background.id != 0) {
        Rectangle src, dest;
        compute_fullscreen_dest(background.width, background.height,
                                screenWidth, screenHeight, &src, &dest);
        DrawTexturePro(background, src, dest, (Vector2){0, 0}, 0.0f, WHITE);
      }
    } else {
      if (!lokalny_stav.blackscreen) {
        const char *txt = get_current_sloha_text(&lokalny_stav);
        if (txt && txt[0] != '\0') {
          int textWidth = MeasureText(txt, fontSize);
          int x = (screenWidth - textWidth) / 2;
          int y = (screenHeight - fontSize) / 2;
          DrawText(txt, x, y, fontSize, WHITE);
        }
      }
    }

    EndDrawing();
  }

  // Upratanie
  zs.stop = 1;
  pthread_join(tid, NULL);
  pthread_mutex_destroy(&zs.mutex);

  if (background.id != 0)
    UnloadTexture(background);
  CloseWindow();
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
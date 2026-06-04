// premietac.c
#include "premietac.h"
#include "protokol.h"
#include "raylib.h"
#include "uart.h"

#include <libgen.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FONT_SIZE_MIN 20
#define FONT_SIZE_MAX 200

typedef struct {
  char **riadky; // pole reťazcov
  int pocet;
  float font_size;
  float line_h;
  float start_y;
  bool platna; // či je cache aktuálna
} RenderCache;

static const char *get_exe_dir(void) {
  static char exe_path[4096];
  static char exe_dir[4096];

  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len < 0) {
    // fallback – použi aktuálny adresár
    return ".";
  }
  exe_path[len] = '\0';
  // dirname môže modifikovať buffer, preto kopírujeme
  strncpy(exe_dir, dirname(exe_path), sizeof(exe_dir) - 1);
  return exe_dir;
}

// Zostav cestu relatívnu k exe
static void build_path(char *out, size_t out_size, const char *filename) {
  snprintf(out, out_size, "%s/%s", get_exe_dir(), filename);
}

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

static Font LoadSlovakFont(const char *path, int fontSize) {
  int codepoints[300];
  int count = 0;

  for (int c = 32; c <= 126; c++)
    codepoints[count++] = c;

  int extra[] = {
      0x010D, 0x010C, 0x0161, 0x0160, 0x017E, 0x017D, 0x013E, 0x013D,
      0x0165, 0x0164, 0x0148, 0x0147, 0x010F, 0x010E, 0x0155, 0x0154,
      0x013A, 0x0139, 0x00E1, 0x00C1, 0x00E9, 0x00C9, 0x00ED, 0x00CD,
      0x00F3, 0x00D3, 0x00FA, 0x00DA, 0x00FD, 0x00DD, 0x00F4, 0x00D4,
      0x00E4, 0x00C4, 0x2013, 0x2014, 0x201E, 0x201C, 0x00AB, 0x00BB,
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

static void cache_update(RenderCache *c, const char *text, Font font,
                         int screenW, int screenH) {
  for (int i = 0; i < c->pocet; i++)
    free(c->riadky[i]);
  free(c->riadky);
  c->riadky = NULL;
  c->pocet = 0;

  if (!text) {
    c->platna = false;
    return;
  }

  // Rozdeľ na riadky
  char *tmp = strdup(text);
  char *sp, *tok = strtok_r(tmp, "\n", &sp);
  int cap = 8;
  c->riadky = malloc(cap * sizeof(char *));

  while (tok) {
    if (c->pocet >= cap) {
      cap *= 2;
      c->riadky = realloc(c->riadky, cap * sizeof(char *));
    }
    c->riadky[c->pocet++] = strdup(tok);
    tok = strtok_r(NULL, "\n", &sp);
  }
  free(tmp);

  // Binárne hľadanie najväčšej veľkosti fontu ktorá sa zmestí
  float lo = FONT_SIZE_MIN;
  float hi = FONT_SIZE_MAX;

  for (int iter = 0; iter < 20; iter++) {
    float mid = (lo + hi) * 0.5f;
    float spacing = mid * 0.05f;

    // Zisti šírku najširšieho riadku a celkovú výšku
    float max_w = 0.0f;
    for (int i = 0; i < c->pocet; i++) {
      Vector2 sz = MeasureTextEx(font, c->riadky[i], mid, spacing);
      if (sz.x > max_w)
        max_w = sz.x;
    }

    float line_h = mid * 1.15f;
    float total_h = c->pocet * line_h;

    // Zmestí sa do 98% šírky aj 97% výšky?
    if (max_w <= screenW * 0.98f && total_h <= screenH * 0.97f) {
      lo = mid; // zmestí sa → skús väčší
    } else {
      hi = mid; // nezmestí sa → skús menší
    }
  }

  // lo je teraz najväčšia veľkosť ktorá sa zmestí
  c->font_size = lo;
  c->line_h = lo * 1.15f;
  c->start_y = (screenH - c->pocet * c->line_h) * 0.5f;
  c->platna = true;
}

// ─────────────────────────────────────────────────────────────
// Hlavná funkcia – Raylib loop
// ─────────────────────────────────────────────────────────────

void premietac_run_raylib(const char *background_path) {

  Uart_chladnicka_t *uart_chl = calloc(1, sizeof(Uart_chladnicka_t));

  if (uart_chl == NULL) {
    perror("Chyba alokovania miesta pre uart chladnicku");
    return;
  }

  uart_chl->aktual_znak = 0;
  uart_chl->max_znakov = 50;
  uart_chl->fd = uart_init();
  uart_chl->nacitane = calloc(uart_chl->max_znakov, sizeof(char));
  if (uart_chl->nacitane == NULL) {
    perror("Chyba alokacie miesta pre chaldnicku nacitane");
    uart_close(uart_chl->fd);
    free(uart_chl);
    return;
  }
  pthread_cond_init(&uart_chl->kontroluj, NULL);
  pthread_cond_init(&uart_chl->zapisuj, NULL);
  pthread_mutex_init(&uart_chl->mutex, NULL);
  atomic_store(&uart_chl->pracuj, true);

  StavPremietania *stav_prem = calloc(1, sizeof(StavPremietania));
  if (stav_prem == NULL) {
    perror("Chyba alokacie pamate pre stav premietania");
    pthread_cond_destroy(&uart_chl->kontroluj);
    pthread_cond_destroy(&uart_chl->zapisuj);
    pthread_mutex_destroy(&uart_chl->mutex);
    free(uart_chl->nacitane);
    free(uart_chl);
    return;
  }
  atomic_store(&stav_prem->bezi, false);
  atomic_store(&stav_prem->blackscreen, false);
  stav_prem->akt_cislo_piesne = 0;
  stav_prem->akt_cislo_slohy = 1;
  stav_prem->db = db_init();
  pthread_mutex_init(&stav_prem->mutex, NULL);

  worker_protokol_t *protokol = calloc(1, sizeof(worker_protokol_t));
  if (protokol == NULL) {
    perror("Chyba alokovania miesta pre worker protokol");
    pthread_cond_destroy(&uart_chl->kontroluj);
    pthread_cond_destroy(&uart_chl->zapisuj);
    pthread_mutex_destroy(&uart_chl->mutex);
    free(uart_chl->nacitane);
    free(uart_chl);
    db_destroy(stav_prem->db);
    pthread_mutex_destroy(&stav_prem->mutex);
    free(stav_prem);
    return;
  }

  protokol->chladnicka = uart_chl;
  protokol->stav = stav_prem;
  printf("Inicializoval som potrebne struktury\n");
  printf("Inicializujem vlakna\n");
  pthread_t vlakno_uart;
  pthread_t vlakno_parser;
  if (pthread_create(&vlakno_uart, NULL, uart_worker, uart_chl) != 0) {
    perror("Chyba vytvorenia vlakna uart");
    pthread_cond_destroy(&uart_chl->kontroluj);
    pthread_cond_destroy(&uart_chl->zapisuj);
    pthread_mutex_destroy(&uart_chl->mutex);
    free(uart_chl->nacitane);
    free(uart_chl);
    db_destroy(stav_prem->db);
    pthread_mutex_destroy(&stav_prem->mutex);
    free(stav_prem);
    free(protokol);
    return;
  }
  if (pthread_create(&vlakno_parser, NULL, parser_worker, protokol) != 0) {
    perror("Chyba vytvorenia vlakna parser");
    pthread_cond_destroy(&uart_chl->kontroluj);
    pthread_cond_destroy(&uart_chl->zapisuj);
    pthread_mutex_destroy(&uart_chl->mutex);
    free(uart_chl->nacitane);
    free(uart_chl);
    db_destroy(stav_prem->db);
    pthread_mutex_destroy(&stav_prem->mutex);
    free(stav_prem);
    free(protokol);
    return;
  }
  printf("Vlakna bezia\n");
  RenderCache cache = {NULL, 0, FONT_SIZE_MAX, 0, 0, false};
  SetConfigFlags(FLAG_WINDOW_UNDECORATED);
  InitWindow(800, 600, "Premietac");
  HideCursor();

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
  HideCursor();
  char font_path[4096];
  build_path(font_path, sizeof(font_path), "NotoSans-Bold.ttf");
  Font uiFont = LoadSlovakFont(font_path, FONT_SIZE_MAX);

  char bg_path[4096];
  // Ak background_path je absolútna cesta, nechaj ju tak
  // Ak je relatívna (nezačína /), zostav ju od exe
  if (background_path[0] == '/') {
    strncpy(bg_path, background_path, sizeof(bg_path) - 1);
    bg_path[sizeof(bg_path) - 1] = '\0'; // ← pridaj
  } else {
    build_path(bg_path, sizeof(bg_path), background_path);
  }
  Texture2D background = LoadTexture(bg_path);
  if (background.id == 0)
    printf("[Premietac] CHYBA pozadie: '%s'\n", background_path);
  else
    printf("[Premietac] OK pozadie: %s (%dx%d)\n", background_path,
           background.width, background.height);

  // ── Render buffer ─────────────────────────────────────────────────
  char *render_text = NULL;      // malloc-ovaný text aktuálnej slohy
  int32_t render_piesen_id = -1; // id piesne v buffri
  int32_t render_sloha_idx = -1; // index slohy v buffri
  bool bezi_prev = false;

  while (!WindowShouldClose()) {
    HideCursor();
    pthread_mutex_lock(&stav_prem->mutex);
    bool bezi_now = atomic_load(&stav_prem->bezi);
    bool black_now = atomic_load(&stav_prem->blackscreen);
    int32_t cur_piesen = stav_prem->akt_cislo_piesne;
    int32_t cur_sloha = stav_prem->akt_cislo_slohy;
    pthread_mutex_unlock(&stav_prem->mutex);

    if (bezi_prev && !bezi_now) {
      free(render_text);
      render_text = NULL;
      render_piesen_id = -1;
      render_sloha_idx = -1;
      cache_update(&cache, NULL, uiFont, screenWidth, screenHeight);
      pthread_mutex_lock(&stav_prem->mutex);
      db_destroy(stav_prem->db);
      stav_prem->db = db_init();
      pthread_mutex_unlock(&stav_prem->mutex);
      atomic_store(&stav_prem->blackscreen, false);
    }

    bezi_prev = bezi_now;

    if (bezi_now &&
        (cur_piesen != render_piesen_id || cur_sloha != render_sloha_idx)) {

      pthread_mutex_lock(&stav_prem->mutex);
      const char *found = NULL;
      for (int i = 0; i < stav_prem->db->akt_pocet && !found; i++) {
        Piesen *p = &stav_prem->db->piesne[i];
        if (p->id == cur_piesen) {
          // hľadaj podľa cislo slohy (1-based)
          for (int j = 0; j < p->akt_pocet_sloh; j++) {
            if (p->slohy[j].cislo == cur_sloha) {
              found = p->slohy[j].text;
              break;
            }
          }
        }
      }
      if (found) {
        free(render_text);
        render_text = malloc(strlen(found) + 1);
        if (render_text)
          strcpy(render_text, found);
        render_piesen_id = cur_piesen;
        render_sloha_idx = cur_sloha;
        cache_update(&cache, render_text, uiFont, screenWidth, screenHeight);
      }
      pthread_mutex_unlock(&stav_prem->mutex);
    }
    // sem pokracuje while
    BeginDrawing();
    ClearBackground(BLACK);

    if (!bezi_now) {
      // Pozadie
      if (background.id != 0) {
        Rectangle src, dest;
        compute_fullscreen_dest(background.width, background.height,
                                screenWidth, screenHeight, &src, &dest);
        DrawTexturePro(background, src, dest, (Vector2){0, 0}, 0.0f, WHITE);
      }

    } else if (!black_now && cache.platna) {
      float spacing = cache.font_size * 0.05f;
      for (int i = 0; i < cache.pocet; i++) {
        Vector2 sz =
            MeasureTextEx(uiFont, cache.riadky[i], cache.font_size, spacing);
        float x = (screenWidth - sz.x) * 0.5f;
        float y = cache.start_y + i * cache.line_h;
        DrawTextEx(uiFont, cache.riadky[i], (Vector2){x, y}, cache.font_size,
                   spacing, WHITE);
      }
    }
    // black_now == true → len čierna (ClearBackground(BLACK) stačí)

    EndDrawing();
  }

  for (int i = 0; i < cache.pocet; i++) {

    free(cache.riadky[i]);
  }
  free(cache.riadky);
  free(render_text);
  CloseWindow();
  uart_close(uart_chl->fd);
  atomic_store(&uart_chl->pracuj, false);
  pthread_cond_broadcast(&uart_chl->kontroluj);

  pthread_join(vlakno_uart, NULL);
  pthread_join(vlakno_parser, NULL);

  pthread_cond_destroy(&uart_chl->kontroluj);
  pthread_cond_destroy(&uart_chl->zapisuj);
  pthread_mutex_destroy(&uart_chl->mutex);
  free(uart_chl->nacitane);
  free(uart_chl);
  db_destroy(stav_prem->db);
  pthread_mutex_destroy(&stav_prem->mutex);
  free(stav_prem);
  free(protokol);
}
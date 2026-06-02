#ifndef PROTOKOL_H
#define PROTOKOL_H

#include <stdint.h>

/* ── Maximá ──────────────────────────────────────────────────────────── */
#define MAX_PIESNI 100
#define MAX_SLOH 100
#define MAX_TEXT_LEN 3000
#define REASM_BUF_SIZE 8192 /* buffer na skladanie chunkov */

/* ── Typy príkazov ───────────────────────────────────────────────────── */
typedef enum {
  PRIKAZ_NONE = 0,
  PRIKAZ_SPUSTI,       /* %$1$0$% */
  PRIKAZ_VYPNI,        /* %$2$0$% */
  PRIKAZ_POSLI_PIESEN, /* %$3$X$% */
  PRIKAZ_ZATMAV,       /* %$4$0$% */
  PRIKAZ_ODTMAV,       /* %$5$0$% */
  PRIKAZ_PREPNI_NA,    /* %|X|Y|% */
  PRIKAZ_DATA_STROFY,  /* $$cislo$$text%%% */
} TypPrikazu;

/* ── Parsovaný príkaz ────────────────────────────────────────────────── */
typedef struct {
  TypPrikazu typ;
  int32_t param1; /* piesen / cislo_strofy / cislo_piesne */
  int32_t param2; /* sloha (pre PrepniNa) */
  char text[MAX_TEXT_LEN];
} ParseovanyPrikaz;

/* ── Sloha piesne ────────────────────────────────────────────────────── */
typedef struct {
  int32_t cislo;
  char text[MAX_TEXT_LEN];
} Sloha;

/* ── Pieseň ──────────────────────────────────────────────────────────── */
typedef struct {
  int32_t id;
  int pocet_sloh;
  Sloha slohy[MAX_SLOH];
} Piesen;

/* ── Databáza piesní ─────────────────────────────────────────────────── */
typedef struct {
  Piesen piesne[MAX_PIESNI];
  int pocet;
} DatabazaPiesni;

/* ── Stav premietania ────────────────────────────────────────────────── */
typedef struct {
  int bezi;
  int blackscreen;
  int32_t cislo_piesne;       /* index do poľa piesní */
  int32_t cislo_slohy;        /* 1-based */
  int32_t cakajuca_piesen_id; /* -1 = žiadna */
  DatabazaPiesni db;
} StavPremietania;

/* ── Funkcie parsovania ───────────────────────────────────────────────── */

/**
 * Pokúsi sa parsovať úplnú správu z reťazca.
 * Vráti 1 ak sa podarilo, 0 ak správa nie je ešte kompletná / neznáma.
 */
int protokol_parsuj(const char *sprava, ParseovanyPrikaz *out);

/**
 * Pridá chunk (riadok) do reassembly buffra.
 * Ak buffer obsahuje kompletnú správu, vráti ju cez `out` a vymaže buffer.
 * Vráti 1 ak je správa kompletná, 0 ak čaká na ďalšie chunky.
 */
int reasm_pridaj_chunk(char *buf, int *buf_len, const char *chunk,
                       ParseovanyPrikaz *out);

/* ── Stavové funkcie ─────────────────────────────────────────────────── */
void stav_init(StavPremietania *s);
void stav_aplikuj(StavPremietania *s, const ParseovanyPrikaz *p);

/* ── Pomocné ─────────────────────────────────────────────────────────── */
const char *typ_prikazu_str(TypPrikazu t);
Sloha *stav_get_sloha(StavPremietania *s, int32_t piesen_idx,
                      int32_t sloha_cislo);
Piesen *stav_get_piesen(StavPremietania *s, int32_t piesen_idx);

#endif /* PROTOKOL_H */
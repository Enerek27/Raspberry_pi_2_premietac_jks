#ifndef PROTOKOL_H
#define PROTOKOL_H

#include "uart.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Maximá ──────────────────────────────────────────────────────────── */
#define MAX_PIESNI 100
#define MAX_SLOH 100
#define MAX_TEXT_LEN 10000
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
  int max_pocet;
  int akt_lenght;
  char *text;
} Sloha;

Sloha *sloha_init();
void sloha_destroy(Sloha *s);
bool sloha_zvac(Sloha *s);

/* ── Pieseň ──────────────────────────────────────────────────────────── */
typedef struct {
  int32_t id;
  int akt_pocet_sloh;
  int max_pocet;
  Sloha *slohy;
} Piesen;

Piesen *piesen_init();
void piesen_destroy(Piesen *p);
bool piesen_zvac(Piesen *p);

/* ── Databáza piesní ─────────────────────────────────────────────────── */
typedef struct {
  Piesen *piesne;
  int akt_pocet;
  int max_pocet;
} DatabazaPiesni;

DatabazaPiesni *db_init();
void db_destroy(DatabazaPiesni *db);
bool db_zvac(DatabazaPiesni *db);

typedef struct {
  char *pole;
  int aktual_pocet;
  int max_pocet;
} pole_t;

pole_t *dyn_pole_init();
void dyn_pole_destroy(pole_t *p);
bool dyn_pole_zvac(pole_t *p);
void dyn_pole_posun_dopredu(pole_t *p, size_t okolko);

/* ── Stav premietania ────────────────────────────────────────────────── */
typedef struct {
  atomic_bool bezi;
  atomic_bool blackscreen;
  int32_t akt_cislo_piesne; /* index do poľa piesní */
  int32_t akt_cislo_slohy;  /* 1-based */
  DatabazaPiesni *db;
  pthread_mutex_t mutex;
} StavPremietania;

typedef struct {
  StavPremietania *stav;
  Uart_chladnicka_t *chladnicka;
} worker_protokol_t;

void *parser_worker(void *arg);
#endif /* PROTOKOL_H */
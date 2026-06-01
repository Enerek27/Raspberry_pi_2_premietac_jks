#include "protokol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  PARSOVANIE
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * Formáty:
 *   %$1$0$%          → SPUSTI
 *   %$2$0$%          → VYPNI
 *   %$3$<id>$%       → POSLI_PIESEN(id)
 *   %$4$0$%          → ZATMAV
 *   %$5$0$%          → ODTMAV
 *   %|<p>|<s>|%      → PREPNI_NA(p, s)
 *   $$<n>$$<text>%%% → DATA_STROFY(n, text)
 */
int protokol_parsuj(const char *sprava, ParseovanyPrikaz *out) {
  if (!sprava || !out)
    return 0;
  memset(out, 0, sizeof(*out));

  /* ── %$X$Y$% príkazy ── */
  if (sprava[0] == '%' && sprava[1] == '$') {
    int typ_id, param;
    /* %$X$Y$%  – X je typ, Y je param (zvyčajne 0) */
    if (sscanf(sprava, "%%$%d$%d$%%", &typ_id, &param) == 2) {
      out->param1 = param;
      switch (typ_id) {
      case 1:
        out->typ = PRIKAZ_SPUSTI;
        return 1;
      case 2:
        out->typ = PRIKAZ_VYPNI;
        return 1;
      case 3:
        out->typ = PRIKAZ_POSLI_PIESEN;
        out->param1 = param;
        return 1;
      case 4:
        out->typ = PRIKAZ_ZATMAV;
        return 1;
      case 5:
        out->typ = PRIKAZ_ODTMAV;
        return 1;
      default:
        return 0;
      }
    }
  }

  /* ── %|piesen|sloha|% ── */
  if (sprava[0] == '%' && sprava[1] == '|') {
    int piesen, sloha;
    if (sscanf(sprava, "%%|%d|%d|%%", &piesen, &sloha) == 2) {
      out->typ = PRIKAZ_PREPNI_NA;
      out->param1 = piesen;
      out->param2 = sloha;
      return 1;
    }
  }

  /* ── $$cislo$$text%%% ── */
  if (strncmp(sprava, "$$", 2) == 0) {
    /* Nájdi druhé $$ */
    const char *p = sprava + 2;
    char *end_num;
    long cislo = strtol(p, &end_num, 10);
    if (end_num != p && strncmp(end_num, "$$", 2) == 0) {
      const char *text_start = end_num + 2;
      /* Odsekni koncové %%% */
      size_t tlen = strlen(text_start);
      if (tlen >= 3 && strcmp(text_start + tlen - 3, "%%%") == 0) {
        tlen -= 3;
      }
      out->typ = PRIKAZ_DATA_STROFY;
      out->param1 = (int32_t)cislo;
      snprintf(out->text, MAX_TEXT_LEN, "%.*s", (int)tlen, text_start);
      return 1;
    }
  }

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  REASSEMBLY (skladanie chunkov)
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Rust posiela dlhé správy rozdelené na kusy ≤31 znakov, každý zakončený \n.
 * Skladáme ich do `buf` a po každom chunku overujeme či je správa kompletná.
 *
 * Správa je kompletná ak:
 *  - %$X$Y$%   → obsahuje oba '%' ohraničovatele
 *  - %|X|Y|%   → obsahuje oba '%' ohraničovatele
 *  - $$N$$...%%% → obsahuje záver '%%%'
 */

/* Vráti 1 ak reťazec vyzerá ako kompletná správa */
static int je_kompletna(const char *buf) {
  if (buf[0] == '%' && buf[1] == '$') {
    /* Hľadáme záverečné $% */
    const char *p = buf + 2;
    while (*p) {
      if (p[0] == '$' && p[1] == '%')
        return 1;
      p++;
    }
    return 0;
  }
  if (buf[0] == '%' && buf[1] == '|') {
    const char *p = buf + 2;
    while (*p) {
      if (p[0] == '|' && p[1] == '%')
        return 1;
      p++;
    }
    return 0;
  }
  if (strncmp(buf, "$$", 2) == 0) {
    return strstr(buf, "%%%") != NULL;
  }
  /* Iný formát – považuj za kompletný */
  return 1;
}

int reasm_pridaj_chunk(char *buf, int *buf_len, const char *chunk,
                       ParseovanyPrikaz *out) {
  int chunk_len = (int)strlen(chunk);

  /* Ak buffer prázdny a chunk začína novú správu */
  if (*buf_len == 0 && chunk_len > 0) {
    /* Detekuj začiatok správy */
    if (chunk[0] != '%' && strncmp(chunk, "$$", 2) != 0) {
      /* Neznámy začiatok – zahoď */
      return 0;
    }
  }

  /* Pridaj chunk do buffra */
  if (*buf_len + chunk_len >= REASM_BUF_SIZE - 1) {
    /* Pretečenie – resetuj */
    fprintf(stderr, "[reasm] Buffer pretiekol, resetujem\n");
    *buf_len = 0;
    buf[0] = '\0';
    return 0;
  }

  memcpy(buf + *buf_len, chunk, chunk_len);
  *buf_len += chunk_len;
  buf[*buf_len] = '\0';

  if (!je_kompletna(buf)) {
    return 0; /* Čakáme na ďalšie chunky */
  }

  /* Máme kompletnú správu – parsuj */
  int ok = protokol_parsuj(buf, out);

  /* Resetuj buffer */
  *buf_len = 0;
  buf[0] = '\0';

  return ok;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  STAV
 * ═══════════════════════════════════════════════════════════════════════ */

void stav_init(StavPremietania *s) {
  memset(s, 0, sizeof(*s));
  s->cakajuca_piesen_id = -1;
}

Piesen *stav_get_piesen(StavPremietania *s, int32_t piesen_idx) {
  if (piesen_idx < 0 || piesen_idx >= s->db.pocet)
    return NULL;
  return &s->db.piesne[piesen_idx];
}

Sloha *stav_get_sloha(StavPremietania *s, int32_t piesen_idx,
                      int32_t sloha_cislo) {
  Piesen *p = stav_get_piesen(s, piesen_idx);
  if (!p)
    return NULL;
  for (int i = 0; i < p->pocet_sloh; i++) {
    if (p->slohy[i].cislo == sloha_cislo)
      return &p->slohy[i];
  }
  return NULL;
}

/* Nájde alebo vytvorí pieseň podľa id */
static Piesen *db_najdi_alebo_vytvor(DatabazaPiesni *db, int32_t id) {
  for (int i = 0; i < db->pocet; i++) {
    if (db->piesne[i].id == id)
      return &db->piesne[i];
  }
  if (db->pocet >= MAX_PIESNI) {
    fprintf(stderr, "[DB] Plná databáza piesní!\n");
    return NULL;
  }
  Piesen *p = &db->piesne[db->pocet++];
  memset(p, 0, sizeof(*p));
  p->id = id;
  return p;
}

void stav_aplikuj(StavPremietania *s, const ParseovanyPrikaz *p) {
  switch (p->typ) {

  case PRIKAZ_SPUSTI:
    s->bezi = 1;
    s->blackscreen = 0;
    printf("[Stav] Premietanie SPUSTENÉ\n");
    break;

  case PRIKAZ_VYPNI:
    // Reset celého stavu – tým „uvolníš“ všetky piesne
    printf("[Stav] Premietanie ZASTAVENÉ, reset databázy\n");
    stav_init(s);
    break;

  case PRIKAZ_POSLI_PIESEN:
    /* Začiatok príjmu novej piesne – zapamätaj id a priprav slot */
    s->cakajuca_piesen_id = p->param1;
    db_najdi_alebo_vytvor(&s->db, p->param1);
    printf("[Stav] Príjem piesne id=%d\n", p->param1);
    break;

  case PRIKAZ_DATA_STROFY: {
    if (s->cakajuca_piesen_id < 0) {
      fprintf(stderr, "[Stav] Strofa bez hlavičky piesne, ignorujem\n");
      break;
    }
    Piesen *piesen = db_najdi_alebo_vytvor(&s->db, s->cakajuca_piesen_id);
    if (!piesen)
      break;
    if (piesen->pocet_sloh >= MAX_SLOH) {
      fprintf(stderr, "[Stav] Pieseň %d má príliš veľa slôh\n", piesen->id);
      break;
    }
    Sloha *sloha = &piesen->slohy[piesen->pocet_sloh++];
    sloha->cislo = p->param1;
    snprintf(sloha->text, MAX_TEXT_LEN, "%s", p->text);
    printf("[Stav] Uložená strofa %d piesne %d: %.40s%s\n", p->param1,
           piesen->id, p->text, strlen(p->text) > 40 ? "..." : "");
    break;
  }

  case PRIKAZ_ZATMAV:
    s->blackscreen = 1;
    printf("[Stav] ZATMAVENÉ\n");
    break;

  case PRIKAZ_ODTMAV:
    s->blackscreen = 0;
    printf("[Stav] ODTMAVENÉ\n");
    break;

  case PRIKAZ_PREPNI_NA: {
    int32_t piesen_idx = p->param1;
    int32_t sloha_cislo = p->param2;
    s->cislo_piesne = piesen_idx;
    s->cislo_slohy = sloha_cislo;

    Sloha *sloha = stav_get_sloha(s, piesen_idx, sloha_cislo);
    printf("[Stav] Prepnuté na pieseň[%d] sloha %d\n", piesen_idx, sloha_cislo);
    if (sloha) {
      printf("[Zobraz] %s\n", sloha->text);
    } else {
      printf("[Zobraz] (sloha nenájdená)\n");
    }
    break;
  }

  default:
    break;
  }
}

/* ── Pomocná funkcia pre výpis ── */
const char *typ_prikazu_str(TypPrikazu t) {
  switch (t) {
  case PRIKAZ_SPUSTI:
    return "SPUSTI";
  case PRIKAZ_VYPNI:
    return "VYPNI";
  case PRIKAZ_POSLI_PIESEN:
    return "POSLI_PIESEN";
  case PRIKAZ_ZATMAV:
    return "ZATMAV";
  case PRIKAZ_ODTMAV:
    return "ODTMAV";
  case PRIKAZ_PREPNI_NA:
    return "PREPNI_NA";
  case PRIKAZ_DATA_STROFY:
    return "DATA_STROFY";
  default:
    return "NEZNÁMY";
  }
}
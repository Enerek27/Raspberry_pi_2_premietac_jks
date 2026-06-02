// protokol.c
#include "protokol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  PARSOVANIE
 * ═══════════════════════════════════════════════════════════════════════ */

int protokol_parsuj(const char *sprava, ParseovanyPrikaz *out) {
  if (!sprava || !out)
    return 0;
  memset(out, 0, sizeof(*out));

  /* ── %$X$Y$% príkazy ── */
  if (sprava[0] == '%' && sprava[1] == '$') {
    int typ_id, param;
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
    const char *p = sprava + 2;
    char *end_num;
    long cislo = strtol(p, &end_num, 10);
    if (end_num != p && strncmp(end_num, "$$", 2) == 0) {
      const char *text_start = end_num + 2;
      size_t tlen = strlen(text_start);
      if (tlen >= 3 && strcmp(text_start + tlen - 3, "%%%") == 0)
        tlen -= 3;
      out->typ = PRIKAZ_DATA_STROFY;
      out->param1 = (int32_t)cislo;
      snprintf(out->text, MAX_TEXT_LEN, "%.*s", (int)tlen, text_start);
      return 1;
    }
  }

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  REASSEMBLY – stream parser
 *  - chunk = surové bajty zo seriálu (môže byť prázdny)
 *  - buf/reasm_buf = vnútorný buffer s doteraz prijatými dátami
 *  - vráti 1 ak sa podarilo poskladať a spracovať 1 správu, inak 0
 * ═══════════════════════════════════════════════════════════════════════ */

int reasm_pridaj_chunk(char *buf, int *buf_len, const char *chunk,
                       ParseovanyPrikaz *out) {
  int chunk_len = (int)strlen(chunk);

  /* 1) Ak máme nový chunk, prilepíme ho na koniec buffra */
  if (chunk_len > 0) {
    if (*buf_len + chunk_len >= REASM_BUF_SIZE - 1) {
      fprintf(stderr, "[reasm] Buffer pretiekol (%d + %d), resetujem\n",
              *buf_len, chunk_len);
      *buf_len = 0;
      buf[0] = '\0';
    }

    memcpy(buf + *buf_len, chunk, chunk_len);
    *buf_len += chunk_len;
    buf[*buf_len] = '\0';
  }

  /* 2) Teraz sa pokúsime z buffra vytiahnuť jednu kompletnú správu */
  for (;;) {
    if (*buf_len < 4)
      return 0; // príliš málo dát na akúkoľvek správu

    /* 2.1) Nájdeme najbližší možný začiatok správy */
    int start = -1;
    for (int i = 0; i < *buf_len - 1; i++) {
      if (buf[i] == '%' && (buf[i + 1] == '$' || buf[i + 1] == '|')) {
        start = i;
        break;
      }
      if (buf[i] == '$' && buf[i + 1] == '$') {
        start = i;
        break;
      }
    }

    if (start < 0) {
      // V buffri nie je ani začiatok, necháme si posledný bajt pre prípad
      char last = buf[*buf_len - 1];
      buf[0] = last;
      *buf_len = 1;
      buf[1] = '\0';
      return 0;
    }

    if (start > 0) {
      // Odstráň bordel pred začiatkom správy
      memmove(buf, buf + start, *buf_len - start);
      *buf_len -= start;
      buf[*buf_len] = '\0';
    }

    if (*buf_len < 4)
      return 0;

    /* 2.2) Podľa prefixu hľadáme terminátor */
    char *end = NULL;
    int msg_len = 0;

    if (buf[0] == '%' && buf[1] == '$') {
      // %$X$Y$% → koniec "$%"
      end = strstr(buf + 2, "$%");
      if (!end)
        return 0; // čakáme na ďalšie dáta
      msg_len = (int)(end - buf) + 2;

    } else if (buf[0] == '%' && buf[1] == '|') {
      // %|piesen|sloha|%
      end = strstr(buf + 2, "|%");
      if (!end)
        return 0;
      msg_len = (int)(end - buf) + 2;

    } else if (buf[0] == '$' && buf[1] == '$') {
      // $$cislo$$text%%%
      end = strstr(buf + 2, "%%%");
      if (!end)
        return 0;
      msg_len = (int)(end - buf) + 3;

    } else {
      // Nečakaný prefix – posuň sa o 1 znak ďalej a skús znova
      memmove(buf, buf + 1, *buf_len - 1);
      (*buf_len)--;
      buf[*buf_len] = '\0';
      continue;
    }

    if (msg_len >= REASM_BUF_SIZE) {
      fprintf(stderr, "[reasm] Správa priveľká (%d), zahadzujem\n", msg_len);
      int remaining = *buf_len - msg_len;
      memmove(buf, buf + msg_len, remaining);
      *buf_len = remaining;
      buf[*buf_len] = '\0';
      // skúsiť ďalšiu správu z toho, čo ostalo
      continue;
    }

    /* 2.3) Skopíruj kompletnú správu do lokálneho buffra */
    char msg[REASM_BUF_SIZE];
    memcpy(msg, buf, msg_len);
    msg[msg_len] = '\0';

    /* 2.4) Odstráň túto správu z reasm buffra */
    int remaining = *buf_len - msg_len;
    memmove(buf, buf + msg_len, remaining);
    *buf_len = remaining;
    buf[*buf_len] = '\0';

    /* 2.5) Skús správu parsovať */
    if (protokol_parsuj(msg, out)) {
      // Úspech – volajúci spracuje out a ak chce ďalšiu správu,
      // zavolá reasm_pridaj_chunk znova s chunk="".
      return 1;
    } else {
      fprintf(stderr, "[reasm] Nepodarilo sa parsovať: \"%s\"\n", msg);
      // Skúsime nájsť ďalšiu správu v tom, čo ostalo
      continue;
    }
  }

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  POMOCNÉ FUNKCIE
 * ═══════════════════════════════════════════════════════════════════════ */

/* Odstráni úvodné čísla z textu slohy */
static const char *strip_leading_numbers(const char *s) {
  while (*s >= '0' && *s <= '9')
    s++;
  while (*s == ' ' || *s == '\t' || *s == '.' || *s == '-' || *s == ':')
    s++;
  return s;
}

/* Nahradí všetky výskyty znaku from za to v jednom reťazci */
static void replace_char_inplace(char *s, char from, char to) {
  for (; *s; s++) {
    if (*s == from)
      *s = to;
  }
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
    printf("[Stav] Premietanie ZASTAVENÉ, reset databázy\n");
    stav_init(s);
    break;

  case PRIKAZ_POSLI_PIESEN:
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

    /* 1) odstráň úvodné čísla zo správy */
    const char *clean = strip_leading_numbers(p->text);

    /* 2) skopíruj do dočasného buffra, aby sme mohli meniť text */
    char tmp[MAX_TEXT_LEN];
    snprintf(tmp, sizeof(tmp), "%s", clean);

    /* 3) nahradiť ^ späť za newline */
    replace_char_inplace(tmp, '^', '\n');

    /* 4) uložiť finálny text do slohy */
    snprintf(sloha->text, MAX_TEXT_LEN, "%s", tmp);

    printf("[Stav] Uložená strofa %d piesne %d: %.40s%s\n", p->param1,
           piesen->id, sloha->text, strlen(sloha->text) > 40 ? "..." : "");
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
    if (sloha)
      printf("[Zobraz] %s\n", sloha->text);
    else
      printf("[Zobraz] (sloha nenájdená)\n");
    break;
  }

  default:
    break;
  }
}

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
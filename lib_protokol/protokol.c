
#include "protokol.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dyn_pole_posun_dopredu(pole_t *p, size_t okolko) {
  if (!p || okolko == 0) {
    return;
  }

  if (okolko >= (size_t)p->aktual_pocet) {
    p->aktual_pocet = 0;
    if (p->max_pocet > 0)
      p->pole[0] = '\0';
    return;
  }

  size_t zostatok = (size_t)p->aktual_pocet - okolko;

  memmove(p->pole, p->pole + okolko, zostatok);

  p->aktual_pocet = zostatok;
}

pole_t *dyn_pole_init() {
  pole_t *p = calloc(1, sizeof(pole_t));

  if (p == NULL) {
    perror("Chyba alokovanie pre pole");
    return NULL;
  }

  p->aktual_pocet = 0;
  p->max_pocet = 50;
  p->pole = calloc(p->max_pocet, sizeof(char));

  if (p->pole == NULL) {
    free(p);
    perror("Chyba alokacie pole pola");
    return NULL;
  }

  return p;
}
void dyn_pole_destroy(pole_t *p) {
  free(p->pole);
  free(p);
}
bool dyn_pole_zvac(pole_t *p) {
  int nova = p->max_pocet + 50;
  char *temp = realloc(p->pole, nova * sizeof(char));

  if (temp == NULL) {
    perror("Chyba zvacsenia dyn pola");
    return false;
  }
  p->max_pocet = nova;
  p->pole = temp;
  return true;
}
bool sloha_zvac(Sloha *s) {
  int nova = s->max_pocet + 50;
  char *temp = realloc(s->text, nova * sizeof(char));

  if (temp == NULL) {
    perror("Chyba zvacsenia sloha text");
    return false;
  }
  s->max_pocet = nova;
  s->text = temp;
  return true;
}

Sloha *sloha_init() {
  Sloha *s = calloc(1, sizeof(Sloha));
  if (s == NULL) {
    perror("Nedostatok pamate chyba sloha");
    return NULL;
  }

  s->akt_lenght = 0;
  s->cislo = 0;
  s->max_pocet = 100;
  s->text = calloc(s->max_pocet, sizeof(char));

  if (s->text == NULL) {
    perror("Chyba alokovania sloha text");
    return NULL;
  }

  return s;
}
static void sloha_clear(Sloha *s) {
  if (!s)
    return;
  free(s->text);
  s->text = NULL;
}

void sloha_destroy(Sloha *s) {
  sloha_clear(s);
  free(s);
}

Piesen *piesen_init() {
  Piesen *p = calloc(1, sizeof(Piesen));
  if (p == NULL) {
    perror("Chyba alokovania pre piesen");
    return NULL;
  }
  p->akt_pocet_sloh = 0;
  p->max_pocet = 5;
  p->id = 0;
  p->slohy = calloc(p->max_pocet, sizeof(Sloha));

  if (p->slohy == NULL) {
    perror("Chyba alokovanie pre piesen slohy");
    free(p);
    return NULL;
  }

  return p;
}

bool piesen_zvac(Piesen *p) {
  int nova = p->max_pocet + 5;
  Sloha *temp = realloc(p->slohy, nova * sizeof(Sloha));

  if (temp == NULL) {
    perror("Chyba zvacsenia piesne slohy");
    return false;
  }
  memset(temp + p->max_pocet, 0, 5 * sizeof(Sloha));
  p->max_pocet = nova;
  p->slohy = temp;
  return true;
}

static void piesen_clear(Piesen *p) {
  if (!p)
    return;
  for (int i = 0; i < p->akt_pocet_sloh; i++)
    sloha_clear(&p->slohy[i]);
  free(p->slohy);
  p->slohy = NULL;
}
void piesen_destroy(Piesen *p) {
  piesen_clear(p);
  free(p);
}

DatabazaPiesni *db_init() {
  DatabazaPiesni *db = calloc(1, sizeof(DatabazaPiesni));
  if (db == NULL) {
    perror("Chyba alokacie pre db");
    return NULL;
  }
  db->akt_pocet = 0;
  db->max_pocet = 5;
  db->piesne = calloc(db->max_pocet, sizeof(Piesen));
  if (db->piesne == NULL) {
    perror("Chyba alokovania pre db piesne");
    free(db);
    return NULL;
  }
  return db;
}

bool db_zvac(DatabazaPiesni *db) {
  int nova = db->max_pocet + 5;
  Piesen *temp = realloc(db->piesne, nova * sizeof(Piesen));

  if (temp == NULL) {
    perror("Chyba zvacsenia db piesne");
    return false;
  }
  memset(temp + db->max_pocet, 0, 5 * sizeof(Piesen));
  db->max_pocet = nova;
  db->piesne = temp;
  return true;
}

void db_destroy(DatabazaPiesni *db) {
  if (!db) {
    return;
  }
  for (int i = 0; i < db->akt_pocet; i++) {
    piesen_clear(&db->piesne[i]);
  }
  free(db->piesne);
  free(db);
}

void nahrad_vsetky_znaky(char *s, char hladaj, char nahrad) {
  while ((s = strchr(s, hladaj)) != NULL) {
    *s = nahrad;
    s++;
  }
}

void *parser_worker(void *arg) {
  worker_protokol_t *wp = arg;
  Uart_chladnicka_t *uart_chld = wp->chladnicka;
  StavPremietania *premietac_chld = wp->stav;
  pole_t *dyn_pole = dyn_pole_init();
  bool prijimam_piesen = false;
  int cislo_piesne = 0;

  while (atomic_load(&uart_chld->pracuj)) {
    pthread_mutex_lock(&uart_chld->mutex);

    while (uart_chld->aktual_znak == 0) {
      pthread_cond_wait(&uart_chld->kontroluj, &uart_chld->mutex);
    }

    if (!atomic_load(&uart_chld->pracuj)) {
      pthread_mutex_unlock(&uart_chld->mutex);
      break;
    }

    int len = uart_chld->aktual_znak;
    while (dyn_pole->aktual_pocet + len >= dyn_pole->max_pocet) {
      if (!dyn_pole_zvac(dyn_pole)) {
        atomic_store(&uart_chld->pracuj, 0);
        pthread_mutex_unlock(&uart_chld->mutex);
        dyn_pole_destroy(dyn_pole);
        pthread_exit(NULL);
      }
    }

    memcpy(dyn_pole->pole + dyn_pole->aktual_pocet, uart_chld->nacitane, len);
    uart_chld->aktual_znak = 0;
    dyn_pole->aktual_pocet += len;

    pthread_mutex_unlock(&uart_chld->mutex);
    if (dyn_pole->aktual_pocet >= dyn_pole->max_pocet) {
      if (!dyn_pole_zvac(dyn_pole)) {
        dyn_pole_destroy(dyn_pole);
        atomic_store(&uart_chld->pracuj, 0);
        pthread_exit(NULL);
      }
    }
    dyn_pole->pole[dyn_pole->aktual_pocet] = '\0';
    printf("COPY: %s\n", dyn_pole->pole);
    char *skuska = strstr(dyn_pole->pole, "%$");
    if (skuska == NULL) {
      if (prijimam_piesen) {
        // Prijimanie a zaznamenavanie slohy piesne
        char *zaciatok = strstr(dyn_pole->pole, "$$");
        if (zaciatok == NULL) {
          continue;
        }
        char *koniec_cisla = strstr(zaciatok + 1, "$$");
        if (koniec_cisla == NULL) {
          continue;
        }
        char *koniec_textu = strstr(koniec_cisla + 1, "%%%");

        if (koniec_textu == NULL) {
          continue;
        }

        int rozdiel_pri_cisle = koniec_cisla + 2 - zaciatok;
        char temp[30] = {0};
        memcpy(temp, zaciatok, rozdiel_pri_cisle);
        temp[rozdiel_pri_cisle] = '\0';
        int cislo_slohy = 0;
        if (sscanf(temp, "$$%d$$", &cislo_slohy) != 1) {
          perror("Zly format chyba chyba pri slohe");
          dyn_pole_destroy(dyn_pole);
          atomic_store(&uart_chld->pracuj, 0);
          pthread_exit(NULL);
        }
        int dlzka_textu = koniec_textu - (koniec_cisla + 2);
        Sloha *s = sloha_init();

        while (s->max_pocet <= dlzka_textu + 1) {
          sloha_zvac(s);
        }
        memcpy(s->text, koniec_cisla + 2, dlzka_textu);
        s->akt_lenght = dlzka_textu;
        s->text[s->akt_lenght] = '\0';
        s->cislo = cislo_slohy;
        nahrad_vsetky_znaky(s->text, '^', '\n');
        printf("[parser] sloha %d piesen %d:%s\n", cislo_slohy, cislo_piesne,
               s->text);
        pthread_mutex_lock(&premietac_chld->mutex);

        bool nasiel = false;

        for (int i = 0; i < premietac_chld->db->akt_pocet; i++) {
          if (premietac_chld->db->piesne[i].id == cislo_piesne) {
            Piesen *najdena = &premietac_chld->db->piesne[i];
            while (najdena->max_pocet - 1 <= najdena->akt_pocet_sloh) {
              piesen_zvac(najdena);
            }
            najdena->slohy[najdena->akt_pocet_sloh] = *s;
            najdena->akt_pocet_sloh++;

            nasiel = true;
            pthread_mutex_unlock(&premietac_chld->mutex);
            break;
          }
        }
        if (!nasiel) {
          pthread_mutex_unlock(&premietac_chld->mutex);
          Piesen *p = piesen_init();
          p->id = cislo_piesne;
          p->slohy[p->akt_pocet_sloh] = *s;
          p->akt_pocet_sloh++;
          pthread_mutex_lock(&premietac_chld->mutex);
          while (premietac_chld->db->max_pocet - 1 <=
                 premietac_chld->db->akt_pocet) {
            db_zvac(premietac_chld->db);
          }
          premietac_chld->db->piesne[premietac_chld->db->akt_pocet] = *p;
          premietac_chld->db->akt_pocet++;
          pthread_mutex_unlock(&premietac_chld->mutex);
          free(p);
        }
        free(s);
        int offset = (koniec_textu + 3) - dyn_pole->pole;
        dyn_pole_posun_dopredu(dyn_pole, offset);
        continue;

        // ajajajjajaja
      }
      char *prepnutie_test = strstr(dyn_pole->pole, "%|");
      if (prepnutie_test == NULL) {
        continue;
      }

      char *prepnutie_koniec = strstr(prepnutie_test + 1, "|%");
      if (prepnutie_koniec == NULL) {
        continue;
      }
      int dlzka_copy = (prepnutie_koniec + 2) - prepnutie_test;
      char tempor[30] = {0};
      memcpy(tempor, prepnutie_test, dlzka_copy);
      tempor[dlzka_copy] = '\0';
      int32_t prep_songa = 0;
      int32_t prep_strofa = 1;
      if (sscanf(tempor, "%%|%d|%d|%%", &prep_songa, &prep_strofa) != 2) {
        perror("Chyba parsovania zly format pri prepinani");
        dyn_pole_destroy(dyn_pole);
        atomic_store(&uart_chld->pracuj, 0);
        pthread_exit(NULL);
      }

      pthread_mutex_lock(&premietac_chld->mutex);
      premietac_chld->akt_cislo_piesne = prep_songa;
      premietac_chld->akt_cislo_slohy = prep_strofa;
      pthread_mutex_unlock(&premietac_chld->mutex);
      int offset = (prepnutie_test - dyn_pole->pole) + dlzka_copy;
      dyn_pole_posun_dopredu(dyn_pole, offset);

      continue;
    }
    prijimam_piesen = false;
    // sem pokracuju vsetky ostatne prikazy
    char *koniec_skuska = strstr(skuska + 1, "$%");
    if (koniec_skuska == NULL) {
      continue;
    }
    koniec_skuska += 2;
    int dlzka_odpoctu = koniec_skuska - skuska;
    char prikaz[30] = {0};
    memcpy(prikaz, skuska, dlzka_odpoctu);
    prikaz[dlzka_odpoctu] = '\0';
    int a = 0;

    if (sscanf(prikaz, "%%$%d$%d$%%", &a, &cislo_piesne) != 2) {
      perror("Chyba priradenia prikazov nic sa nenaslo zly format");
      dyn_pole_destroy(dyn_pole);
      atomic_store(&uart_chld->pracuj, 0);
      pthread_exit(NULL);
    }
    switch (a) {
    case 1:
      // spusti
      atomic_store(&premietac_chld->bezi, true);
      break;
    case 2:
      // vypni
      atomic_store(&premietac_chld->bezi, false);
      break;
    case 3:
      // prijmi piesen
      prijimam_piesen = true;
      break;
    case 4:
      // zatmav obrazovku
      atomic_store(&premietac_chld->blackscreen, true);
      break;
    case 5:
      // odtmav obrazovku
      atomic_store(&premietac_chld->blackscreen, false);
      break;
    }
    dyn_pole_posun_dopredu(dyn_pole, dlzka_odpoctu);
  }

  pthread_exit(NULL);
}

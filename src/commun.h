#ifndef DOA5TOOLS_COMMUN_H
#define DOA5TOOLS_COMMUN_H
#include <windows.h>

void Journal(const char *module, const char *libelle, long a, long b, long c);

int Reglage(const char *section, const char *cle, int defaut);

const char *CheminIni(void);

typedef void (*Tic_t)(void);
void AjouterTic(Tic_t f);

void Lobby_Demarrer(void);
void JoinFix_Demarrer(void);
void Rematch_Demarrer(void);
void WiFiWired_Demarrer(void);
void Borderless_Demarrer(void);
void Fps60_Demarrer(void);
void Skip_Demarrer(void);
void Ultrawide_Demarrer(void);
void ReplayMenu_Demarrer(void);

#endif

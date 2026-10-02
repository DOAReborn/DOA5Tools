/*  DOA5Tools - definitions communes a tous les modules (voir doa5tools.c). */
#ifndef DOA5TOOLS_COMMUN_H
#define DOA5TOOLS_COMMUN_H
#include <windows.h>

/* Journal unique DOA5Tools.log (si [DOA5Tools] Log=1) : SEULE fonction d'ecriture du projet.
 * Libelle fixe + nombres, aucun identifiant ni texte venu du jeu. 64 Ko au plus. */
void Journal(const char *module, const char *libelle, long a, long b, long c);

/* Reglage entier lu dans DOA5Tools.ini (section, cle, valeur par defaut). */
int Reglage(const char *section, const char *cle, int defaut);
/* Chemin complet de DOA5Tools.ini (pour les modules qui y ecrivent, ex. Borderless). */
const char *CheminIni(void);

/* Tic sur le thread du jeu (SteamAPI_RunCallbacks, table d'imports) : un seul crochet, partage. */
typedef void (*Tic_t)(void);
void AjouterTic(Tic_t f);

/* Points d'entree des modules (appeles depuis DllMain si leur interrupteur vaut 1). */
void Lobby_Demarrer(void);
void JoinFix_Demarrer(void);
void Rematch_Demarrer(void);
void WiFiWired_Demarrer(void);
void Borderless_Demarrer(void);
void Fps60_Demarrer(void);
void Skip_Demarrer(void);
void Ultrawide_Demarrer(void);   /* ultrawide.c, compile avec -DDOA5TOOLS */

#endif

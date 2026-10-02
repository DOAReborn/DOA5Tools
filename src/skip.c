/*  DOA5LR-Skip 1.0 (02/10/2026) - au lancement du jeu, passe les logos et cinematiques d'intro.
 *
 *  Projet DOA5Tools. Cible : game.exe 1.10C (RVA).
 *
 *  Sequence de demarrage du jeu (automate, table d'etats D01048) : etat 0/1 : si l'octet D01039 vaut 1, menu
 *  MenuBoot (id 0x0F : logos et intros) puis attente de sa fin ; etat 2 : ecran titre "Press Start" (id 0x11).
 *  Le jeu met lui-meme D01039 a 0 en entrant dans l'ecran titre (les logos ne repassent pas en y revenant).
 *  Le module met D01039 a 0 des le lancement : on arrive directement sur l'ecran titre, un appui sur Start suffit.
 *
 *  Ecran titre : non saute. Un Start simule (0.1/0.2-test, evenement 2 de MenuTitle) bloquait le jeu sur un
 *  chargement sans fin ; un vrai appui fonctionne. L'appui sur Start est donc garde.
 *
 *  Aucun reseau, aucun fichier hors .ini et journal. Journal (Log=1, 0 par defaut) : DOA5LR-Skip.log.
 *  Compilation (LLVM-MinGW, 32 bits) :
 *    i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -o DOA5LR-Skip.asi skip.c
 */
/*  Version integree a DOA5Tools : [DOA5Tools] Skip=0 pour couper, journal commun. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_MODULE "1.0"

#define RVA_DRAPEAU_LOGOS    0xD01039   /* octet : 1 = jouer MenuBoot (logos) au prochain passage */
#define RVA_BOOT_ENTREE      0x2ECFE0   /* entree de l'etat 0 (lit D01039) : sert a reconnaitre la version */
static const uint8_t DEBUT_BOOT[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};

static void Note(const char *libelle, long a) { Journal("Skip", libelle, a, 0, 0); }   /* journal commun */

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    int ok = 0;
    for (int t = 0; t < 3000 && !ok; t++) {                 /* code dechiffre par SteamStub (10 ms par essai) */
        ok = !memcmp(base + RVA_BOOT_ENTREE, DEBUT_BOOT, sizeof DEBUT_BOOT);
        if (!ok) Sleep(10);
    }
    if (!ok) { Note("refus_version_game_exe", 0); return 0; }
    volatile uint8_t *d = base + RVA_DRAPEAU_LOGOS;
    const int avant = *d;
    if (avant == 1) *d = 0;
    Note("logos_drapeau_avant", avant);
    return 0;
}

void Skip_Demarrer(void)
{
    Note("debut " VERSION_MODULE, 0);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

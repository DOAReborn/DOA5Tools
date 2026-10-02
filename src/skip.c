#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_MODULE "1.0"

#define RVA_DRAPEAU_LOGOS    0xD01039
#define RVA_BOOT_ENTREE      0x2ECFE0
static const uint8_t DEBUT_BOOT[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};

static void Note(const char *libelle, long a) { Journal("Skip", libelle, a, 0, 0); }

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    int ok = 0;
    for (int t = 0; t < 3000 && !ok; t++) {
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

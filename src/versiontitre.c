#include <windows.h>
#include <string.h>
#include "commun.h"

#define RVA_CHARGE_TEXTE 0x241092
#define RVA_TEXTE_VER    0x99CAEC
#define RVA_APPEL_DESSIN 0x2410E5
#define RVA_DESSIN       0x7C7E30
#define RVA_MESURE       0x7C8810
#define TAILLE_OBJET     0x84
#define OFS_TEXTE        0x40
#define OFS_X            0x54
#define OFS_Y            0x58
#define BORD_DROIT       1100
#define INTERLIGNE       17.0f

static void Note(const char *libelle, long a) { Journal("VersionTitre", libelle, a, 0, 0); }

static BYTE *base;
typedef void (__cdecl *Dessin_t)(void *objet, int a);
typedef float (__fastcall *Mesure_t)(void *objet);
static Dessin_t o_dessin;
static Mesure_t mesure;
static void __cdecl Dessin(void *objet, int a)
{
    o_dessin(objet, a);
    DWORD copie[TAILLE_OBJET / 4];
    memcpy(copie, objet, TAILLE_OBJET);
    BYTE *c = (BYTE *)copie;
    *(const wchar_t **)(c + OFS_TEXTE) = TEXTE_VERSION_DOA5TOOLS;
    const float largeur = mesure(c);
    *(float *)(c + OFS_X) = (float)(BORD_DROIT - (int)(largeur * 0.5f));
    *(float *)(c + OFS_Y) += INTERLIGNE;
    o_dessin(c, a);
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    BYTE *charge = base + RVA_CHARGE_TEXTE, *appel = base + RVA_APPEL_DESSIN;
    int ok = 0;
    for (int t = 0; t < 600 && !ok; t++) {
        DWORD texte; LONG rel;
        memcpy(&texte, charge + 3, 4); memcpy(&rel, appel + 1, 4);
        ok = charge[0] == 0xC7 && charge[1] == 0x45 && charge[2] == 0xAC && texte - (DWORD)(ULONG_PTR)base == RVA_TEXTE_VER &&
             appel[0] == 0xE8 && appel + 5 + rel == base + RVA_DESSIN;
        if (!ok) Sleep(100);
    }
    if (!ok) { Note("refus_version_game_exe", 0); return 0; }
    o_dessin = (Dessin_t)(base + RVA_DESSIN);
    mesure = (Mesure_t)(base + RVA_MESURE);
    const LONG rel = (LONG)((BYTE *)Dessin - (appel + 5));
    DWORD ancien;
    if (!VirtualProtect(appel + 1, 4, PAGE_EXECUTE_READWRITE, &ancien)) { Note("protection_refusee", 0); return 0; }
    memcpy(appel + 1, &rel, 4);
    VirtualProtect(appel + 1, 4, ancien, &ancien);
    FlushInstructionCache(GetCurrentProcess(), appel, 5);
    Note("pose", 1);
    return 0;
}

void VersionTitre_Demarrer(void)
{
    base = (BYTE *)GetModuleHandleW(NULL);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/*  DOA5Tools 1.0 (02/10/2026) - un seul .asi pour les modules DOA5LR (game.exe 1.10C) du projet DOA5Tools.
 *
 *  Modules (sources a part, meme comportement que leurs versions separees validees en jeu) :
 *   Lobby 1.0.3, JoinFix 1.0, Rematch 1.0, WiFi-Wired 1.0, Borderless 2.0, 60fps 1.0 (AutoLink requis), Skip 1.0,
 *   Ultrawide 1.0 (reprise de DOA5LRFix de Lyall, MIT ; inactif sur un ecran 16:9).
 *  DOA5Tools.ini, section [DOA5Tools] : un interrupteur par module (1 = actif, 0 = aucun crochet pose) et Log.
 *  Ce fichier : point d'entree, reglages, journal unique, tic Steam partage (JoinFix + Lobby).
 *  Aucun reseau autre que les appels Steam du jeu ; fichiers : DOA5Tools.ini (lecture ; Borderless y enregistre son
 *  mode), DOA5LR.ini du jeu (Borderless, cle SCREEN_TYPE seulement), DOA5Tools.log si Log=1.
 *
 *  Compilation (LLVM-MinGW, 32 bits), dans ce dossier :
 *    i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -DDOA5TOOLS -o DOA5Tools.asi doa5tools.c lobby.c joinfix.c
 *        rematch.c wifiwired.c borderless.c fps60.c skip.c ultrawide.c -liphlpapi -lshell32
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_DOA5TOOLS "1.0"

static char g_ini[MAX_PATH];
const char *CheminIni(void) { return g_ini; }
int Reglage(const char *section, const char *cle, int defaut) { return GetPrivateProfileIntA(section, cle, defaut, g_ini); }

/* ---- journal ---------------------------------------------------------------------------------- */
static FILE *g_log;
static long g_octets;
static DWORD g_t0;
static CRITICAL_SECTION g_cs;
void Journal(const char *module, const char *libelle, long a, long b, long c)
{
    if (!g_log || g_octets > 64 * 1024) return;
    EnterCriticalSection(&g_cs);
    const int n = fprintf(g_log, "t=%lu %s %s %ld %ld %ld\n", (unsigned long)(GetTickCount() - g_t0), module, libelle, a, b, c);
    if (n > 0) g_octets += n;
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

/* ---- tic partage : SteamAPI_RunCallbacks (table d'imports du jeu), thread du jeu ------------------ */
#define RVA_IAT_RUNCALLBACKS 0x9663DC
#define RVA_KTOL_PRINTF      0x846010   /* reconnaissance de la version / dechiffrement SteamStub */
static const uint8_t KTOL_PRINTF_BYTES[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
#define NB_TICS 4
static Tic_t g_tics[NB_TICS];
static volatile LONG g_nbTics;
void AjouterTic(Tic_t f)
{
    const LONG i = InterlockedIncrement(&g_nbTics) - 1;
    if (i < NB_TICS) g_tics[i] = f;
}
typedef void (__cdecl *RunCallbacks_t)(void);
static RunCallbacks_t o_runCallbacks;
static void __cdecl hk_runCallbacks(void)
{
    o_runCallbacks();
    const LONG n = g_nbTics < NB_TICS ? g_nbTics : NB_TICS;
    for (LONG i = 0; i < n; i++) if (g_tics[i]) g_tics[i]();
}
static DWORD WINAPI PoserTic(LPVOID p)
{
    (void)p;
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    int ok = 0;
    for (int t = 0; t < 600 && !ok; t++) {
        ok = !memcmp(base + RVA_KTOL_PRINTF, KTOL_PRINTF_BYTES, sizeof KTOL_PRINTF_BYTES);
        if (!ok) Sleep(100);
    }
    if (!ok) { Journal("DOA5Tools", "refus_version_game_exe", 0, 0, 0); return 0; }
    void **iat = (void **)(base + RVA_IAT_RUNCALLBACKS);
    DWORD ancien;
    int pose = 0;
    if (VirtualProtect(iat, sizeof(void *), PAGE_READWRITE, &ancien)) {
        o_runCallbacks = (RunCallbacks_t)*iat;
        InterlockedExchange((volatile LONG *)iat, (LONG)(uintptr_t)hk_runCallbacks);
        VirtualProtect(iat, sizeof(void *), ancien, &ancien);
        pose = 1;
    }
    Journal("DOA5Tools", "tic", pose, g_nbTics, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD raison, LPVOID r)
{
    (void)r;
    if (raison != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    InitializeCriticalSection(&g_cs);
    g_t0 = GetTickCount();
    char log[MAX_PATH];
    GetModuleFileNameA(h, g_ini, sizeof g_ini);
    char *sep = strrchr(g_ini, '\\');
    if (sep) sep[1] = 0;
    snprintf(log, sizeof log, "%sDOA5Tools.log", g_ini);
    strncat(g_ini, "DOA5Tools.ini", sizeof g_ini - strlen(g_ini) - 1);
    if (Reglage("DOA5Tools", "Log", 0)) g_log = fopen(log, "w");
    Journal("DOA5Tools", "debut " VERSION_DOA5TOOLS, 0, 0, 0);
    /* Skip en premier : il doit agir avant la sequence de demarrage du jeu */
    if (Reglage("DOA5Tools", "Skip", 1)) Skip_Demarrer();
    if (Reglage("DOA5Tools", "Ultrawide", 1)) Ultrawide_Demarrer();   /* tot (resolution) ; ne fait rien en 16:9 */
    if (Reglage("DOA5Tools", "Borderless", 1)) Borderless_Demarrer();   /* avant que le jeu ne lise DOA5LR.ini */
    if (Reglage("DOA5Tools", "Lobby", 1)) Lobby_Demarrer();
    if (Reglage("DOA5Tools", "JoinFix", 1)) JoinFix_Demarrer();
    if (Reglage("DOA5Tools", "Rematch", 1)) Rematch_Demarrer();
    if (Reglage("DOA5Tools", "WiFiWired", 1)) WiFiWired_Demarrer();
    if (Reglage("DOA5Tools", "60fps", 1)) Fps60_Demarrer();
    if (g_nbTics) {                                         /* seulement si un module en a besoin */
        HANDLE t = CreateThread(NULL, 0, PoserTic, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

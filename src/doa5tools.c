#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_DOA5TOOLS "1.2"

static char g_ini[MAX_PATH];
const char *CheminIni(void) { return g_ini; }
int Reglage(const char *section, const char *cle, int defaut) { return GetPrivateProfileIntA(section, cle, defaut, g_ini); }

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

#define RVA_IAT_RUNCALLBACKS 0x9663DC
#define RVA_KTOL_PRINTF      0x846010
static const uint8_t KTOL_PRINTF_BYTES[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
#define NB_TICS 8
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

    if (Reglage("DOA5Tools", "Skip", 1)) Skip_Demarrer();
    if (Reglage("DOA5Tools", "Ultrawide", 1)) Ultrawide_Demarrer();
    if (Reglage("DOA5Tools", "Borderless", 1)) Borderless_Demarrer();
    if (Reglage("DOA5Tools", "Lobby", 1)) Lobby_Demarrer();
    if (Reglage("DOA5Tools", "JoinFix", 1)) JoinFix_Demarrer();
    if (Reglage("DOA5Tools", "Rematch", 1)) Rematch_Demarrer();
    if (Reglage("DOA5Tools", "WiFiWired", 1)) WiFiWired_Demarrer();
    if (Reglage("DOA5Tools", "60fps", 1)) Fps60_Demarrer();
    if (Reglage("DOA5Tools", "ReplayMenu", 1)) ReplayMenu_Demarrer();
    if (Reglage("DOA5Tools", "LobbyChat", 1)) LobbyChat_Demarrer();
    if (g_nbTics) {
        HANDLE t = CreateThread(NULL, 0, PoserTic, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

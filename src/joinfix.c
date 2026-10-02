#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_MODULE "1.0"
typedef uint64_t CSteamID;

#define RVA_KTOL_PRINTF      0x846010
static const uint8_t KTOL_PRINTF_BYTES[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
#define RVA_IAT_RUNCALLBACKS 0x9663DC
#define RVA_CRYPT_CTX        0x20924B8

#define MM_GET_NUM_MEMBERS   17
#define MM_SET_LOBBY_DATA    20
#define MM_REQUEST_DATA      28
#define MM_GET_OWNER         35

static uint8_t *g_base;
static int g_keyFix = 1, g_inviteRetry = 1, g_copyKey = 0x76;

static void Note(const char *libelle, long a, long b) { Journal("JoinFix", libelle, a, b, 0); }

static int RemplacerPointeur(void **slot, void *crochet, void **original)
{
    DWORD ancien;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &ancien)) return 0;
    *original = *slot;
    InterlockedExchange((volatile LONG *)slot, (LONG)(uintptr_t)crochet);
    VirtualProtect(slot, sizeof(void *), ancien, &ancien);
    return 1;
}

static void *g_mm;
static CSteamID g_lobby;
static CSteamID g_inviteLobby;
static DWORD g_inviteTick;
static int g_inviteTries;

typedef int (__thiscall *GetNumLobbyMembers_t)(void *self, CSteamID lobby);
typedef CSteamID *(__thiscall *GetLobbyOwner_t)(void *self, CSteamID *ret, CSteamID lobby);
typedef uint8_t (__thiscall *RequestLobbyData_t)(void *self, CSteamID lobby);
static void **VtMM(void) { return *(void ***)g_mm; }

typedef struct CB { void **vt; uint8_t flags; uint8_t pad[3]; int id; int size; } CB;
static void SurCallback(int id, const uint8_t *d)
{
    switch (id) {
    case 333:
        if (g_inviteRetry) { g_inviteLobby = *(const uint64_t *)d; g_inviteTick = GetTickCount(); g_inviteTries = 0; }
        break;
    case 505:
        if (g_inviteLobby && *(const uint64_t *)d == g_inviteLobby) {
            Note("invitation_donnees_recues relances", g_inviteTries, 0);
            g_inviteLobby = 0;
        }
        break;
    case 504: {
        const CSteamID lobby = *(const uint64_t *)d;
        if (*(const uint32_t *)(d + 16) == 1) g_lobby = lobby;
        if (g_inviteLobby == lobby) g_inviteLobby = 0;
        break; }
    case 513:
        if (*(const int *)d == 1) g_lobby = *(const uint64_t *)(d + 8);
        break;
    }
}
static void __thiscall cb_run_result(CB *self, void *pv, int io, uint64_t call) { (void)io; (void)call; SurCallback(self->id, pv); }
static void __thiscall cb_run(CB *self, void *pv) { SurCallback(self->id, pv); }
static int __thiscall cb_size(CB *self) { return self->size; }
static void *g_cbVt[3] = { (void *)cb_run_result, (void *)cb_run, (void *)cb_size };
static const int CB_IDS[][2] = { {333, 16}, {504, 24}, {505, 24}, {513, 16} };
#define NCB (sizeof CB_IDS / sizeof *CB_IDS)
static CB g_cbs[NCB];
typedef void (__cdecl *RegisterCallback_t)(CB *cb, int id);

typedef uint8_t (__thiscall *SetLobbyData_t)(void *self, CSteamID lobby, const char *key, const char *value);
static SetLobbyData_t o_setLobbyData;
static int RemplacerZeros(uint8_t *k, int n)
{
    int corriges = 0;
    for (int i = 0; i < n; i++) if (k[i] == 0) { k[i] = (uint8_t)(0x5B + 37 * i) | 1; corriges++; }
    return corriges;
}
static uint8_t __thiscall hk_setLobbyData(void *self, CSteamID lobby, const char *key, const char *value)
{
    if (g_keyFix && key && value) {
        const int seed = !strcmp(key, "cryptSeed"), sig = !strcmp(key, "sigKey");
        uint8_t *ctx = *(uint8_t **)(g_base + RVA_CRYPT_CTX);
        if ((seed || sig) && ctx) {
            uint8_t *k = ctx + (seed ? 0x1C : 0x2C);
            const int n = seed ? 16 : 8;
            const size_t lv = strlen(value);

            if (!memcmp(value, k, lv < (size_t)n ? lv : (size_t)n)) {
                const int f = RemplacerZeros(k, n);
                if (f) { memcpy((void *)value, k, n); Note("keyfix_corrige cle(1=seed,2=sig)/octets", seed ? 1 : 2, f); }
            } else Note("keyfix_valeur_differente cle(1=seed,2=sig)", seed ? 1 : 2, 0);
        }
    }
    return o_setLobbyData(self, lobby, key, value);
}

static int CopierTexte(const char *t)
{
    const size_t n = strlen(t) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n * sizeof(WCHAR));
    if (!h) return 0;
    WCHAR *w = (WCHAR *)GlobalLock(h);
    MultiByteToWideChar(CP_ACP, 0, t, -1, w, (int)n);
    GlobalUnlock(h);
    if (!OpenClipboard(NULL)) { GlobalFree(h); return 0; }
    EmptyClipboard();
    const int ok = SetClipboardData(CF_UNICODETEXT, h) != NULL;
    CloseClipboard();
    if (!ok) GlobalFree(h);
    return ok;
}
static int JeuAuPremierPlan(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}
static void CopierLien(void)
{
    const CSteamID lobby = g_lobby;

    const int n = (g_mm && lobby) ? ((GetNumLobbyMembers_t)VtMM()[MM_GET_NUM_MEMBERS])(g_mm, lobby) : 0;
    CSteamID hote = 0;
    if (n > 0) ((GetLobbyOwner_t)VtMM()[MM_GET_OWNER])(g_mm, &hote, lobby);
    if (!hote) { MessageBeep(MB_ICONHAND); Note("lien_pas_de_salon", 0, 0); return; }
    char lien[96];
    snprintf(lien, sizeof lien, "steam://joinlobby/311730/%llu/%llu", (unsigned long long)lobby, (unsigned long long)hote);
    const int ok = CopierTexte(lien);
    SecureZeroMemory(lien, sizeof lien);
    MessageBeep(ok ? MB_ICONASTERISK : MB_ICONHAND);
    Note("lien_copie ok", ok, 0);
}

static void Tic(void)
{
    if (g_copyKey) {
        static int avant;
        const int bas = (GetAsyncKeyState(g_copyKey) & 0x8000) != 0;
        if (bas && !avant && JeuAuPremierPlan()) CopierLien();
        avant = bas;
    }
    if (g_inviteRetry && g_inviteLobby && g_mm) {
        const DWORD now = GetTickCount();
        if (now - g_inviteTick < 3000) return;
        if (g_inviteTries >= 5) { Note("invitation_abandon relances", g_inviteTries, 0); g_inviteLobby = 0; return; }
        const uint8_t ok = ((RequestLobbyData_t)VtMM()[MM_REQUEST_DATA])(g_mm, g_inviteLobby);
        g_inviteTries++;
        g_inviteTick = now;
        Note("invitation_relance n/ok", g_inviteTries, ok);
    }
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    int ok = 0;
    for (int t = 0; t < 600 && !ok; t++) {
        ok = !memcmp(g_base + RVA_KTOL_PRINTF, KTOL_PRINTF_BYTES, sizeof KTOL_PRINTF_BYTES);
        if (!ok) Sleep(100);
    }
    if (!ok) { Note("refus_version_game_exe", 0, 0); return 0; }
    HMODULE sa = NULL;
    for (int t = 0; t < 1200 && !sa; t++) { sa = GetModuleHandleA("steam_api.dll"); if (!sa) Sleep(100); }
    if (!sa) { Note("refus_steam_api_absent", 0, 0); return 0; }
    RegisterCallback_t reg = (RegisterCallback_t)GetProcAddress(sa, "SteamAPI_RegisterCallback");
    if (reg)
        for (size_t i = 0; i < NCB; i++) {
            g_cbs[i].vt = g_cbVt; g_cbs[i].id = CB_IDS[i][0]; g_cbs[i].size = CB_IDS[i][1];
            reg(&g_cbs[i], CB_IDS[i][0]);
        }
    typedef void *(*Acces_t)(void);
    Acces_t accesMM = (Acces_t)GetProcAddress(sa, "SteamMatchmaking");
    for (int t = 0; t < 1200 && !g_mm; t++) { if (accesMM) g_mm = accesMM(); if (!g_mm) Sleep(250); }
    const int k = g_keyFix && g_mm && RemplacerPointeur(&VtMM()[MM_SET_LOBBY_DATA], (void *)hk_setLobbyData, (void **)&o_setLobbyData);
    Note("pret keyfix", k, 0);
    Note("reglages inviteretry/copykey", g_inviteRetry, g_copyKey);
    return 0;
}

void JoinFix_Demarrer(void)
{
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    g_keyFix = Reglage("JoinFix", "KeyFix", 1) != 0;
    g_inviteRetry = Reglage("JoinFix", "InviteRetry", 1) != 0;
    g_copyKey = Reglage("JoinFix", "CopyLinkKey", 0x76);
    if (g_copyKey < 0 || g_copyKey > 0xFE) g_copyKey = 0;
    Note("debut " VERSION_MODULE, 0, 0);
    AjouterTic(Tic);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

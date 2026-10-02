/*  DOA5LR-JoinFix 1.0 (01/10/2026) - correctifs de jonction des salons (DOA5LR PC, game.exe 1.10C).
 *
 *  Reprise minimale de JoinFix 0.3 (projet DOA5Tools) : seulement ce qui sert, journal minimal.
 *
 *  KeyFix=1      : l'hote publie ses cles de chiffrement P2P dans les donnees du salon Steam comme du TEXTE
 *                  ("cryptSeed" 16 octets, "sigKey" 8 octets, contexte global RVA 0x20924B8 -> +0x1C / +0x2C).
 *                  Un octet nul coupe la valeur : les joueurs derivent une mauvaise cle et ne peuvent pas entrer
 *                  (~1 salon sur 11). Juste avant la publication, les octets nuls sont remplaces dans la cle du
 *                  jeu ET dans la valeur publiee (le jeu relit sa cle a chaque paquet : tout reste coherent).
 *                  Correctif cote HOTE : tout createur de salon doit l'avoir.
 *  InviteRetry=1 : apres une invitation acceptee, le jeu attend les donnees du salon (LobbyDataUpdate) pour
 *                  lancer la jonction ; si elles se perdent, rien ne se passe. On les redemande toutes les 3 s,
 *                  5 fois au plus.
 *  CopyLinkKey=118 : touche (code virtuel, 118 = F7, 0 = aucune) qui copie dans le presse-papiers le lien
 *                  steam://joinlobby/311730/<salon>/<hote> du salon courant (les salons prives n'ont pas de
 *                  bouton "Rejoindre" dans Steam). Bip aigu = copie, bip grave = pas de salon.
 *
 *  Retire depuis 0.3 : AcceptMembers (lisait les ID des membres, jamais demontre utile), FastFail (lisait l'ID
 *  de l'hote et l'etat de sa liaison ; le jeu abandonne seul apres son delai), KeyTest (essai).
 *
 *  Donnees : l'ID du salon (courant / invite) et, pour F7 seulement, l'ID de l'hote sont lus en memoire.
 *  Ils ne sont jamais ecrits dans le journal. Aucun reseau autre que les appels Steam du jeu.
 *  Journal (Log=1, 0 par defaut) : DOA5LR-JoinFix.log a cote du module, libelles fixes et nombres, 16 Ko au plus.
 *
 *  Tous les appels Steam se font sur le thread du jeu (tic = SteamAPI_RunCallbacks, via la table d'imports).
 *  Compilation (LLVM-MinGW, 32 bits) :
 *    i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -o DOA5LR-JoinFix.asi joinfix.c
 */
/*  Version integree a DOA5Tools : reglages [JoinFix] de DOA5Tools.ini, journal et tic communs. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_MODULE "1.0"
typedef uint64_t CSteamID;

/* ---- adresses (game.exe 1.10C, RVA) ------------------------------------------------------- */
#define RVA_KTOL_PRINTF      0x846010   /* sert a reconnaitre la version (et le dechiffrement SteamStub) */
static const uint8_t KTOL_PRINTF_BYTES[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
#define RVA_IAT_RUNCALLBACKS 0x9663DC   /* import SteamAPI_RunCallbacks */
#define RVA_CRYPT_CTX        0x20924B8  /* pointeur : +0x1C cryptSeed[16], +0x2C sigKey[8] */

/* slots des interfaces Steam utilises (SDK de l'epoque du jeu) */
#define MM_GET_NUM_MEMBERS   17         /* ISteamMatchmaking::GetNumLobbyMembers */
#define MM_SET_LOBBY_DATA    20         /* ISteamMatchmaking::SetLobbyData */
#define MM_REQUEST_DATA      28         /* ISteamMatchmaking::RequestLobbyData */
#define MM_GET_OWNER         35         /* ISteamMatchmaking::GetLobbyOwner */

static uint8_t *g_base;
static int g_keyFix = 1, g_inviteRetry = 1, g_copyKey = 0x76;

/* journal commun (doa5tools.c) */
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

/* ---- Steam ---------------------------------------------------------------------------------- */
static void *g_mm;                  /* ISteamMatchmaking */
static CSteamID g_lobby;            /* salon courant (LobbyEnter / LobbyCreated reussis) */
static CSteamID g_inviteLobby;      /* invitation acceptee, en attente des donnees du salon */
static DWORD g_inviteTick;
static int g_inviteTries;

typedef int (__thiscall *GetNumLobbyMembers_t)(void *self, CSteamID lobby);
typedef CSteamID *(__thiscall *GetLobbyOwner_t)(void *self, CSteamID *ret, CSteamID lobby);
typedef uint8_t (__thiscall *RequestLobbyData_t)(void *self, CSteamID lobby);
static void **VtMM(void) { return *(void ***)g_mm; }

/* callbacks Steam (CCallbackBase MSVC : [0] Run(pv, bIO, hCall), [1] Run(pv), [2] taille) */
typedef struct CB { void **vt; uint8_t flags; uint8_t pad[3]; int id; int size; } CB;
static void SurCallback(int id, const uint8_t *d)
{
    switch (id) {
    case 333:   /* GameLobbyJoinRequested : salon, ami */
        if (g_inviteRetry) { g_inviteLobby = *(const uint64_t *)d; g_inviteTick = GetTickCount(); g_inviteTries = 0; }
        break;
    case 505:   /* LobbyDataUpdate : donnees du salon recues */
        if (g_inviteLobby && *(const uint64_t *)d == g_inviteLobby) {
            Note("invitation_donnees_recues relances", g_inviteTries, 0);
            g_inviteLobby = 0;
        }
        break;
    case 504: { /* LobbyEnter : salon, ..., reponse (+16, 1 = ok) */
        const CSteamID lobby = *(const uint64_t *)d;
        if (*(const uint32_t *)(d + 16) == 1) g_lobby = lobby;
        if (g_inviteLobby == lobby) g_inviteLobby = 0;
        break; }
    case 513:   /* LobbyCreated : resultat (1 = ok), salon (+8) */
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

/* ---- KeyFix : ISteamMatchmaking::SetLobbyData ------------------------------------------------ */
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
            /* la valeur publiee doit etre la copie (eventuellement coupee) de la cle du jeu : sinon on n'y touche pas */
            if (!memcmp(value, k, lv < (size_t)n ? lv : (size_t)n)) {
                const int f = RemplacerZeros(k, n);
                if (f) { memcpy((void *)value, k, n); Note("keyfix_corrige cle(1=seed,2=sig)/octets", seed ? 1 : 2, f); }
            } else Note("keyfix_valeur_differente cle(1=seed,2=sig)", seed ? 1 : 2, 0);
        }
    }
    return o_setLobbyData(self, lobby, key, value);
}

/* ---- F7 : lien steam://joinlobby du salon courant -> presse-papiers -------------------------- */
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
    /* GetNumLobbyMembers vaut 0 si l'on n'est plus dans ce salon */
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

/* ---- tic sur le thread du jeu : SteamAPI_RunCallbacks ----------------------------------------- */
static void Tic(void)                       /* appele par le tic commun (doa5tools.c) */
{
    if (g_copyKey) {                         /* front montant de la touche, fenetre du jeu au premier plan */
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

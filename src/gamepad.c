#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include "commun.h"
#include "accroche.h"

#define RVA_KTOL_PRINTF     0x846010
#define RVA_APPEL_CTOR      0x8393A8
#define RVA_CTOR            0x838CF0
#define RVA_GENERIQUE       0x838BD0
#define RVA_SONY            0x838AB0
#define RVA_XBOX            0x8387F0
#define RVA_POSE_GENERIQUE  0x838D9F
#define RVA_LISTE           0x208C3FC
#define RVA_APPEL_REMISE    0x83958B
#define RVA_APPEL_TOUCHE    0x8A30F3
#define RVA_TOUCHE          0x8A26A0
#define RVA_ETAT_COMBAT     0x1087A4C
#define OBJ_DEVICE         0x00
#define OBJ_PRODUIT         0x14
#define OBJ_REMISE          0x34
#define OBJ_SUIVANT         0x24C
#define CTX_COPIE           0x24
#define ETAT_BOUTONS        0x30
#define ETAT_L2             0x36
#define ETAT_R2             0x37
#define ID_STEAM            0x11FF28DEu

#define JOURNAL(libelle,a,b,c) Journal("Gamepad",libelle,(long)(a),(long)(b),(long)(c))

static uint8_t *g_base;
static void *volatile g_suite;
static void *volatile g_suiteTouche;

#define NB_LISTE 16
static DWORD g_xbox[NB_LISTE], g_sony[NB_LISTE];
static int g_nbXbox, g_nbSony;
static int g_sonyAuto = 1, g_gachettes = 1, g_avisSteam = 1, g_seuilGachette = 60;

static DWORD LireId(const char *p, char **fin)
{
    char *f;
    const unsigned long vid = strtoul(p, &f, 16);
    if (f == p || *f != ':' || vid > 0xFFFF) return 0;
    p = f + 1;
    const unsigned long pid = strtoul(p, &f, 16);
    if (f == p || pid > 0xFFFF) return 0;
    if (fin) *fin = f;
    return (DWORD)(pid << 16 | vid);
}

static int LireListe(const char *cle, DWORD *t)
{
    char s[512];
    int n = 0;
    GetPrivateProfileStringA("Gamepad", cle, "", s, sizeof s, CheminIni());
    for (char *p = s; *p && n < NB_LISTE;) {
        while (*p == ' ' || *p == ',' || *p == ';' || *p == '\t') p++;
        if (!*p) break;
        char *fin;
        const DWORD id = LireId(p, &fin);
        if (!id) break;
        t[n++] = id;
        p = fin;
    }
    return n;
}
static int DansListe(const DWORD *t, int n, DWORD id)
{
    for (int i = 0; i < n; i++) if (t[i] == id) return 1;
    return 0;
}

static const char *const NOMS_DS4[14] = {"Square", "Cross", "Circle", "Triangle", "L1", "R1", "L2", "R2",
                                         "Share", "Options", "L3", "R3", "PS", "Touchpad"};
static const struct { const char *nom; int ds4; } AUTRES_NOMS[] = {
    {"X", 0}, {"A", 1}, {"B", 2}, {"Y", 3}, {"LB", 4}, {"RB", 5}, {"LT", 6}, {"RT", 7}, {"ZL", 6}, {"ZR", 7},
    {"Back", 8}, {"View", 8}, {"Create", 8}, {"Select", 8}, {"Minus", 8}, {"Start", 9}, {"Menu", 9}, {"Plus", 9},
    {"LS", 10}, {"RS", 11}, {"Guide", 12}, {"Home", 12}, {"Capture", 13}, {"Touch", 13}};

#define NB_PERSO 8
typedef struct { DWORD id; int8_t vers[32]; } Disposition;
static Disposition g_perso[NB_PERSO];
static int g_nbPerso;

static int NomBouton(const char *s, size_t n)
{
    if (n == 1 && s[0] == '-') return -1;
    for (int i = 0; i < 14; i++) if (strlen(NOMS_DS4[i]) == n && !_strnicmp(s, NOMS_DS4[i], n)) return i;
    for (size_t i = 0; i < sizeof AUTRES_NOMS / sizeof AUTRES_NOMS[0]; i++)
        if (strlen(AUTRES_NOMS[i].nom) == n && !_strnicmp(s, AUTRES_NOMS[i].nom, n)) return AUTRES_NOMS[i].ds4;
    return -2;
}

static int LireDisposition(const char *s, Disposition *d)
{
    memset(d->vers, -1, sizeof d->vers);
    int i = 0;
    for (const char *p = s; *p && i < 32;) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        const char *debut = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        const int b = NomBouton(debut, (size_t)(p - debut));
        if (b == -2) return 0;
        d->vers[i++] = (int8_t)b;
    }
    return i > 0;
}

static void LireDispositions(void)
{
    static char sec[4096];
    const DWORD n = GetPrivateProfileSectionA("GamepadLayouts", sec, sizeof sec, CheminIni());
    for (const char *l = sec; n && *l && g_nbPerso < NB_PERSO; l += strlen(l) + 1) {
        while (*l == ' ' || *l == '\t') l++;
        if (*l == ';' || *l == '#') continue;
        char *fin;
        const DWORD id = LireId(l, &fin);
        const char *egal = strchr(l, '=');
        if (!id || !egal) continue;
        Disposition d;
        if (!LireDisposition(egal + 1, &d)) { JOURNAL("disposition libre refusee (nom de bouton inconnu), rang", g_nbPerso + 1, 0, 0); continue; }
        d.id = id;
        g_perso[g_nbPerso++] = d;
    }
}
static const Disposition *TrouverPerso(DWORD id)
{
    for (int i = 0; i < g_nbPerso; i++) if (g_perso[i].id == id) return &g_perso[i];
    return NULL;
}

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPad;
typedef struct { DWORD dwPacketNumber; XPad Gamepad; } XEtat;
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XEtat *);
static XInputGetState_t g_xget;
static volatile LONG g_xpret;
static int g_xindex = -1;
static DWORD g_xrecherche;

static int XInputPret(void)
{
    if (g_xpret) return g_xpret > 0;
    HMODULE m = LoadLibraryA("xinput1_4.dll");
    if (!m) m = LoadLibraryA("xinput9_1_0.dll");
    g_xget = m ? (XInputGetState_t)(void *)GetProcAddress(m, "XInputGetState") : NULL;
    InterlockedExchange(&g_xpret, g_xget ? 1 : -1);
    JOURNAL("XInput pour les gachettes (1 pret, -1 absent)", g_xpret, 0, 0);
    return g_xget != NULL;
}

static void ChercherXInput(void)
{
    const DWORD t = GetTickCount();
    if (g_xrecherche && t - g_xrecherche < 2000) return;
    g_xrecherche = t ? t : 1;
    int trouve = -1, n = 0;
    XEtat e;
    for (DWORD i = 0; i < 4; i++) if (g_xget(i, &e) == ERROR_SUCCESS) { trouve = (int)i; n++; }
    g_xindex = n == 1 ? trouve : -1;
}

typedef void (__cdecl *Remise_t)(uint8_t *etat, uint8_t *ctx);

static void __cdecl RemiseXbox(uint8_t *etat, uint8_t *ctx)
{
    ((Remise_t)(g_base + RVA_XBOX))(etat, ctx);
    if (g_xpret <= 0) return;
    if (g_xindex < 0) { ChercherXInput(); if (g_xindex < 0) return; }
    XEtat e;
    if (g_xget((DWORD)g_xindex, &e) != ERROR_SUCCESS) { g_xindex = -1; g_xrecherche = 0; return; }
    etat[ETAT_L2] = e.Gamepad.bLeftTrigger > g_seuilGachette;
    etat[ETAT_R2] = e.Gamepad.bRightTrigger > g_seuilGachette;
}

static int g_ctxValide;
static void __cdecl RemisePerso(uint8_t *etat, uint8_t *ctx)
{
    const Disposition *d = g_ctxValide ? TrouverPerso(*(DWORD *)(ctx - CTX_COPIE + OBJ_PRODUIT)) : NULL;
    if (d) {
        uint8_t b[32] = {0};
        for (int i = 0; i < 32; i++)
            if (d->vers[i] >= 0 && etat[ETAT_BOUTONS + i]) b[(int)d->vers[i]] |= etat[ETAT_BOUTONS + i];
        memcpy(etat + ETAT_BOUTONS, b, sizeof b);
    }
    ((Remise_t)(g_base + RVA_SONY))(etat, ctx);
}

static volatile LONG g_steamVu;

static DWORD WINAPI AvisSteam(LPVOID p)
{
    (void)p;
    Sleep(4000);
    const int r = MessageBoxA(NULL,
        "Steam Input is on for DEAD OR ALIVE 5 Last Round.\n\n"
        "Your controller works, but Steam turns it into a virtual Xbox controller and hides the real one from the game. "
        "For direct support of PlayStation and Xbox controllers and arcade sticks, turn Steam Input off for this game:\n\n"
        "Steam > Library > right-click DEAD OR ALIVE 5 Last Round > Properties > Controller > Disable Steam Input\n\n"
        "Then restart the game.\n\n"
        "Show this message again next time?",
        "DOA5LR - Controller", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
    if (r == IDNO) WritePrivateProfileStringA("Gamepad", "SteamInputNotice", "0", CheminIni());
    JOURNAL("avis Steam Input montre (6 oui, 7 non)", r, 0, 0);
    return 0;
}

static void SignalerSteam(void)
{
    if (InterlockedExchange(&g_steamVu, 1)) return;
    const int wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL;
    JOURNAL("Steam Input actif (manette virtuelle de Steam ouverte) ; avis", 1, g_avisSteam, 0);
    if (wine || !g_avisSteam) return;
    HANDLE t = CreateThread(NULL, 0, AvisSteam, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

typedef struct { DWORD dwSize, dwHeaderSize, dwObj, dwHow; GUID guidClass; WCHAR wszPath[MAX_PATH]; } ProprieteChemin;
typedef HRESULT (WINAPI *GetProperty_t)(void *self, const GUID *prop, void *entete);
#define DIPROP_GUIDANDPATH_ ((const GUID *)(uintptr_t)12)
typedef struct { DWORD dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs, dwFFSamplePeriod, dwFFMinTimeResolution,
                 dwFirmwareRevision, dwHardwareRevision, dwFFDriverVersion; } Capacites;
typedef HRESULT (WINAPI *GetCapabilities_t)(void *self, Capacites *c);

static int EstXInput(void *device)
{
    if (!device) return 0;
    ProprieteChemin p;
    memset(&p, 0, sizeof p);
    p.dwSize = sizeof p;
    p.dwHeaderSize = 16;
    const GetProperty_t get = (GetProperty_t)(*(void ***)device)[5];
    if (FAILED(get(device, DIPROP_GUIDANDPATH_, &p))) return 0;
    p.wszPath[MAX_PATH - 1] = 0;
    for (WCHAR *c = p.wszPath; c[0] && c[1] && c[2]; c++)
        if ((c[0] == L'I' || c[0] == L'i') && (c[1] == L'G' || c[1] == L'g') && c[2] == L'_') return 1;
    return 0;
}

static int FormeDS4(void *device)
{
    if (!device) return 0;
    Capacites c;
    memset(&c, 0, sizeof c);
    c.dwSize = sizeof c;
    const GetCapabilities_t get = (GetCapabilities_t)(*(void ***)device)[3];
    if (FAILED(get(device, &c))) return 0;
    return (c.dwButtons == 14 || c.dwButtons == 15) && c.dwPOVs == 1 && c.dwAxes == 6;
}

enum { RIEN, SONY, XBOX, PERSO };
static LONG g_compte[4];

static int Reclasser(uint8_t *obj, int reprise)
{
    void *volatile *remise = (void *volatile *)(obj + OBJ_REMISE);
    void *const cur = *remise;
    const DWORD id = *(DWORD *)(obj + OBJ_PRODUIT);
    if (id == ID_STEAM) SignalerSteam();
    int choix = RIEN;
    if (TrouverPerso(id) && g_ctxValide) choix = PERSO;
    else if (cur == g_base + RVA_GENERIQUE) {
        if (DansListe(g_xbox, g_nbXbox, id)) choix = XBOX;
        else if (DansListe(g_sony, g_nbSony, id)) choix = SONY;
        else if ((id & 0xFFFF) == 0x054C) choix = SONY;
        else if (EstXInput(*(void **)(obj + OBJ_DEVICE))) choix = XBOX;
        else if (g_sonyAuto && FormeDS4(*(void **)(obj + OBJ_DEVICE))) choix = SONY;
    } else if (cur == g_base + RVA_XBOX) choix = XBOX;
    void *cible = NULL;
    if (choix == SONY) cible = g_base + RVA_SONY;
    else if (choix == PERSO) cible = (void *)RemisePerso;
    else if (choix == XBOX) cible = (g_gachettes && XInputPret()) ? (void *)RemiseXbox : (void *)(g_base + RVA_XBOX);
    int fait = 0;
    if (cible && cible != cur)
        fait = InterlockedCompareExchangePointer((void **)remise, cible, cur) == cur;
    if (fait) InterlockedIncrement(&g_compte[choix]);
    JOURNAL(reprise ? "manette deja ouverte (0 inchangee, 1 Sony, 2 Xbox, 3 libre ; change)" : "manette ouverte (0 inchangee, 1 Sony, 2 Xbox, 3 libre ; change)",
            choix, fait, cible == (void *)RemiseXbox);
    return choix;
}

typedef void *(__attribute__((thiscall)) *Ctor_t)(void *, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                  DWORD, DWORD, DWORD, DWORD);
static void *__attribute__((thiscall)) hk_ctor(void *self, DWORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5,
                                               DWORD a6, DWORD a7, DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12)
{
    void *r = ((Ctor_t)g_suite)(self, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
    if (r) Reclasser((uint8_t *)r, 0);
    return r;
}

static const struct { const char *nom; uint8_t dik; } ACTIONS[15] = {
    {"Up", 0x11}, {"Down", 0x1F}, {"Left", 0x1E}, {"Right", 0x20}, {"Guard", 0x24}, {"Punch", 0x25}, {"Kick", 0x26},
    {"Throw", 0x32}, {"PK", 0x16}, {"HPK", 0x17}, {"HK", 0x18}, {"Taunt", 0x31}, {"Duel", 0x39}, {"Pause", 0x1C},
    {"CommandList", 0x19}};
static const struct { const char *nom; uint8_t dik; } TOUCHES[] = {
    {"Space", 0x39}, {"Enter", 0x1C}, {"Return", 0x1C}, {"Esc", 0x01}, {"Escape", 0x01}, {"Tab", 0x0F},
    {"Backspace", 0x0E}, {"LShift", 0x2A}, {"RShift", 0x36}, {"LCtrl", 0x1D}, {"RCtrl", 0x9D}, {"LAlt", 0x38},
    {"RAlt", 0xB8}, {"CapsLock", 0x3A}, {"Up", 0xC8}, {"Down", 0xD0}, {"Left", 0xCB}, {"Right", 0xCD},
    {"Home", 0xC7}, {"End", 0xCF}, {"PageUp", 0xC9}, {"PageDown", 0xD1}, {"Insert", 0xD2}, {"Delete", 0xD3},
    {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E}, {"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42},
    {"F9", 0x43}, {"F10", 0x44}, {"F11", 0x57}, {"F12", 0x58}, {"Num0", 0x52}, {"Num1", 0x4F}, {"Num2", 0x50},
    {"Num3", 0x51}, {"Num4", 0x4B}, {"Num5", 0x4C}, {"Num6", 0x4D}, {"Num7", 0x47}, {"Num8", 0x48}, {"Num9", 0x49},
    {"NumEnter", 0x9C}, {"NumPlus", 0x4E}, {"NumMinus", 0x4A}, {"NumStar", 0x37}, {"NumSlash", 0xB5},
    {"NumDot", 0x53}};

#define BLOQUEE 0x100

static uint16_t g_tables[2][256];
static uint16_t *volatile g_clavier;
static int g_cible[15];
static volatile LONG g_capture = -1;
static volatile LONG g_avaler = -1;
static HKL g_disposition;
static int g_clavierPose;

#define NB_MENU 2
static const struct { const char *nom; uint8_t vers; } MENU[NB_MENU] = {{"Confirm", 0x32}, {"Cancel", 0x26}};
static volatile LONG g_menu[NB_MENU];
static uint16_t g_envoye[256];
static volatile const DWORD *g_etatCombat;
static int g_traceMenu;

static int CodeTouche(const char *s, HKL disposition)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (!n) return 0;
    if (s[0] == '#') { const long v = strtol(s + 1, NULL, 0); return v > 0 && v < 256 ? (int)v : 0; }
    for (size_t i = 0; i < sizeof TOUCHES / sizeof TOUCHES[0]; i++)
        if (strlen(TOUCHES[i].nom) == n && !_strnicmp(s, TOUCHES[i].nom, n)) return TOUCHES[i].dik;
    if (n != 1) return 0;
    const SHORT vk = VkKeyScanExA(s[0], disposition);
    if (vk == -1) return 0;
    const UINT sc = MapVirtualKeyExA((UINT)(vk & 0xFF), 0  , disposition);
    return sc > 0 && sc < 0x80 ? (int)sc : 0;
}

static int ConstruireTable(const int *cible, uint16_t *table)
{
    for (int i = 0; i < 15; i++)
        for (int j = i + 1; j < 15; j++)
            if (cible[i] == cible[j]) return 0;
    for (int k = 0; k < 256; k++) table[k] = (uint16_t)k;
    for (int i = 0; i < 15; i++) {
        int utilisee = 0;
        for (int j = 0; j < 15; j++) if (cible[j] == ACTIONS[i].dik) utilisee = 1;
        if (!utilisee) table[ACTIONS[i].dik] = BLOQUEE;
    }
    for (int i = 0; i < 15; i++) table[cible[i]] = ACTIONS[i].dik;
    return 1;
}
static int NbDeplacees(const int *cible)
{
    int n = 0;
    for (int i = 0; i < 15; i++) n += cible[i] != ACTIONS[i].dik;
    return n;
}

static void Appliquer(void)
{
    uint16_t *const libre = g_clavier == g_tables[0] ? g_tables[1] : g_tables[0];
    if (!ConstruireTable(g_cible, libre)) return;
    InterlockedExchangePointer((void *volatile *)&g_clavier, NbDeplacees(g_cible) ? libre : NULL);
}

static int LireClavier(HKL disposition)
{
    int cible[15];
    for (int i = 0; i < 15; i++) cible[i] = g_cible[i] = ACTIONS[i].dik;
    char v[64];
    for (int i = 0; i < 15; i++) {
        GetPrivateProfileStringA("Keyboard", ACTIONS[i].nom, "", v, sizeof v, CheminIni());
        if (!v[0]) continue;
        const int c = CodeTouche(v, disposition);
        if (!c) { JOURNAL("clavier refuse : touche inconnue pour l'action", i + 1, 0, 0); return -1; }
        cible[i] = c;
    }

    static char sec[2048];
    if (GetPrivateProfileSectionA("Keyboard", sec, sizeof sec, CheminIni()))
        for (const char *l = sec; *l; l += strlen(l) + 1) {
            char *fin;
            const long a = strtol(l, &fin, 10);
            if (fin == l || *fin != '=') continue;
            const long b = strtol(fin + 1, NULL, 10);
            for (int i = 0; i < 15; i++)
                if (ACTIONS[i].dik == a && b > 0 && b < 256) cible[i] = (int)b;
        }
    uint16_t essai[256];
    if (!ConstruireTable(cible, essai)) { JOURNAL("clavier refuse : deux actions sur la meme touche", 0, 0, 0); return -1; }
    memcpy(g_cible, cible, sizeof g_cible);
    return NbDeplacees(cible);
}

static int LireMenu(HKL disposition)
{
    char v[64];
    int n = 0;
    for (int m = 0; m < NB_MENU; m++) {
        g_menu[m] = 0;
        GetPrivateProfileStringA("Keyboard", MENU[m].nom, "", v, sizeof v, CheminIni());
        if (!v[0]) continue;
        const int c = CodeTouche(v, disposition);
        if (!c) { JOURNAL("clavier : touche de menu inconnue, ignoree (1 Confirm, 2 Cancel)", m + 1, 0, 0); continue; }
        if (m && c == g_menu[0]) { JOURNAL("clavier : Cancel sur la touche de Confirm, ignoree", 0, 0, 0); continue; }
        g_menu[m] = c;
        n++;
    }
    return n;
}

static const char *const LIBELLES[15 + NB_MENU] = {"Up", "Down", "Left", "Right", "Guard (H)", "Punch (P)",
                                                   "Kick (K) / Cancel", "Throw / Confirm", "P+K", "H+P+K", "H+K", "Taunt",
                                                   "Duel / Shortcut", "Pause", "Command list", "Confirm (menus)",
                                                   "Cancel (menus)"};

int Clavier_NbActions(void) { return 15 + NB_MENU; }
const char *Clavier_Action(int i) { return i >= 0 && i < 15 + NB_MENU ? LIBELLES[i] : ""; }
int Clavier_Touche(int i) { return i >= 0 && i < 15 ? g_cible[i] : i >= 15 && i < 15 + NB_MENU ? (int)g_menu[i - 15] : 0; }
int Clavier_EnCapture(void) { return (int)g_capture; }

static char CaractereTouche(int dik)
{
    if (dik <= 0 || dik >= 0x80) return 0;
    const UINT vk = MapVirtualKeyExA((UINT)dik, 1  , g_disposition);
    if (!vk) return 0;
    const UINT c = MapVirtualKeyExA(vk, 2  , g_disposition) & 0x7FFF;
    return c > ' ' && c < 0x7F ? (char)(c >= 'a' && c <= 'z' ? c - 32 : c) : 0;
}

void Clavier_NomTouche(int dik, char *texte, int taille)
{
    if (!texte || taille < 2) return;
    for (size_t i = 0; i < sizeof TOUCHES / sizeof TOUCHES[0]; i++)
        if (TOUCHES[i].dik == dik) { snprintf(texte, (size_t)taille, "%s", TOUCHES[i].nom); return; }
    const char c = CaractereTouche(dik);
    if (c) snprintf(texte, (size_t)taille, "%c", c);
    else snprintf(texte, (size_t)taille, "Key %d", dik);
}

static void NomPourIni(int dik, char *v, size_t taille)
{
    for (size_t k = 0; k < sizeof TOUCHES / sizeof TOUCHES[0]; k++)
        if (TOUCHES[k].dik == dik) { snprintf(v, taille, "%s", TOUCHES[k].nom); return; }
    const char c = CaractereTouche(dik);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) snprintf(v, taille, "%c", c);
    else snprintf(v, taille, "#%d", dik);
}

static void Enregistrer(void)
{
    char v[32], cle[8];
    for (int i = 0; i < 15; i++) {
        v[0] = 0;
        if (g_cible[i] != ACTIONS[i].dik) NomPourIni(g_cible[i], v, sizeof v);
        WritePrivateProfileStringA("Keyboard", ACTIONS[i].nom, v, CheminIni());
        snprintf(cle, sizeof cle, "%d", ACTIONS[i].dik);
        WritePrivateProfileStringA("Keyboard", cle, NULL, CheminIni());
    }
    for (int m = 0; m < NB_MENU; m++) {
        v[0] = 0;
        if (g_menu[m]) NomPourIni((int)g_menu[m], v, sizeof v);
        WritePrivateProfileStringA("Keyboard", MENU[m].nom, v, CheminIni());
    }
}

int Clavier_Definir(int i, int dik)
{
    if (i >= 15 && i < 15 + NB_MENU) {
        if (!g_clavierPose || dik < 0 || dik >= 256) return 0;
        const int m = i - 15, autre = 1 - m;
        if (dik && g_menu[autre] == dik) InterlockedExchange(&g_menu[autre], g_menu[m]);
        InterlockedExchange(&g_menu[m], dik);
        Enregistrer();
        JOURNAL("clavier : touche de menu changee depuis le menu (1 Confirm, 2 Cancel ; touche)", m + 1, dik, 0);
        return 1;
    }
    if (!g_clavierPose || i < 0 || i >= 15 || dik <= 0 || dik >= 256) return 0;
    int cible[15];
    memcpy(cible, g_cible, sizeof cible);
    for (int j = 0; j < 15; j++) if (j != i && cible[j] == dik) cible[j] = cible[i];
    cible[i] = dik;
    uint16_t essai[256];
    if (!ConstruireTable(cible, essai)) return 0;
    memcpy(g_cible, cible, sizeof g_cible);
    Appliquer();
    Enregistrer();
    JOURNAL("clavier : touche changee depuis le menu (action, actions deplacees)", i + 1, NbDeplacees(g_cible), 0);
    return 1;
}

void Clavier_Capturer(int i) { InterlockedExchange(&g_capture, g_clavierPose && i >= 0 && i < 15 + NB_MENU ? i : -1); }

void Clavier_ParDefaut(void)
{
    for (int i = 0; i < 15; i++) g_cible[i] = ACTIONS[i].dik;
    for (int m = 0; m < NB_MENU; m++) InterlockedExchange(&g_menu[m], 0);
    Appliquer();
    Enregistrer();
    JOURNAL("clavier : touches du jeu remises depuis le menu", 0, 0, 0);
}

typedef void (__attribute__((thiscall)) *Touche_t)(void *self, void *evenement);
static void __attribute__((thiscall)) hk_touche(void *self, void *evenement)
{
    const DWORD ofs = evenement ? ((DWORD *)evenement)[0] : 0xFFFFFFFFu;
    if (ofs < 256) {
        const DWORD appui = ((DWORD *)evenement)[1] & 0x80;
        const LONG capture = g_capture;
        if (capture >= 0 && appui) {
            InterlockedExchange(&g_capture, -1);
            g_avaler = (LONG)ofs;
            if (ofs != 0x01) Clavier_Definir((int)capture, (int)ofs);
            return;
        }
        if (!appui && (LONG)ofs == g_avaler) { g_avaler = -1; return; }
        int m = -1;
        for (int k = 0; k < NB_MENU; k++) if (g_menu[k] && (DWORD)g_menu[k] == ofs) m = k;
        if (m >= 0 || g_envoye[ofs]) {
            uint16_t vers = 0;
            if (appui) {
                const DWORD etat = g_etatCombat ? *g_etatCombat : 4;
                if (m >= 0 && etat != 4) vers = MENU[m].vers;
                g_envoye[ofs] = vers;
                if (m >= 0 && g_traceMenu < 6) {
                    g_traceMenu++;
                    JOURNAL("clavier : touche de menu (1 Confirm, 2 Cancel ; etat du jeu ; 1 = validation / annulation)", m + 1, etat, vers != 0);
                }
            } else {
                vers = g_envoye[ofs];
                g_envoye[ofs] = 0;
            }
            if (vers) {
                DWORD copie[5];
                memcpy(copie, evenement, sizeof copie);
                copie[0] = vers;
                ((Touche_t)g_suiteTouche)(self, copie);
                return;
            }
        }
        const uint16_t *const table = g_clavier;
        if (table) {
            const uint16_t vers = table[ofs];
            if (vers == BLOQUEE) return;
            if (vers != ofs) {
                DWORD copie[5];
                memcpy(copie, evenement, sizeof copie);
                copie[0] = vers;
                ((Touche_t)g_suiteTouche)(self, copie);
                return;
            }
        }
    }
    ((Touche_t)g_suiteTouche)(self, evenement);
}

static const uint8_t KTOL_PRINTF[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
static const uint8_t PRO_CTOR[] = {0x55, 0x8B, 0xEC, 0x53, 0x8B, 0xD9, 0x56, 0x57};
static const uint8_t PRO_REMISE[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20};

static const uint8_t APPEL_REMISE[] = {0x8B, 0xF3, 0x8D, 0x7D, 0xC8, 0xF3, 0xA5, 0x8D, 0x4D, 0xEC, 0x51,
                                       0x8D, 0x95, 0xB8, 0xFE, 0xFF, 0xFF, 0x52, 0xFF, 0xD0};
static const uint8_t PRO_TOUCHE[] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x56, 0x8B, 0x70, 0x04, 0x8B, 0x00};

static int VersionAttendue(void)
{
    if (memcmp(g_base + RVA_CTOR, PRO_CTOR, sizeof PRO_CTOR)) return 0;
    if (memcmp(g_base + RVA_GENERIQUE, PRO_REMISE, sizeof PRO_REMISE)) return 0;
    if (memcmp(g_base + RVA_SONY, PRO_REMISE, sizeof PRO_REMISE)) return 0;
    if (memcmp(g_base + RVA_XBOX, PRO_REMISE, sizeof PRO_REMISE)) return 0;
    const uint8_t *pose = g_base + RVA_POSE_GENERIQUE;
    if (pose[0] != 0xC7 || pose[1] != 0x43 || pose[2] != 0x34) return 0;
    return *(uint8_t *const *)(pose + 3) == g_base + RVA_GENERIQUE;
}

static void InstallerClavier(void)
{
    g_disposition = GetKeyboardLayout(0);
    const int n = LireClavier(g_disposition);
    const int nm = LireMenu(g_disposition);
#ifdef DOA5TOOLS
    const int toujours = 1;
#else
    const int toujours = 0;
#endif
    if (n <= 0 && !nm && !toujours) { JOURNAL("clavier : touches du jeu (0 pas de profil, -1 profil refuse)", n, 0, 0); return; }
    if (memcmp(g_base + RVA_TOUCHE, PRO_TOUCHE, sizeof PRO_TOUCHE)) { JOURNAL("clavier : refus, version inattendue", 0, 0, 0); return; }
    g_etatCombat = (volatile const DWORD *)(g_base + RVA_ETAT_COMBAT);
    Appliquer();
    g_clavierPose = ac_call(g_base + RVA_APPEL_TOUCHE, g_base + RVA_TOUCHE, (void *)hk_touche, &g_suiteTouche) != NULL;
    if (!g_clavierPose) InterlockedExchangePointer((void *volatile *)&g_clavier, NULL);
    JOURNAL("clavier : accroche, actions deplacees, touches de menu", g_clavierPose, n, nm);
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    int t;
    for (t = 0; t < 600 && memcmp(g_base + RVA_KTOL_PRINTF, KTOL_PRINTF, sizeof KTOL_PRINTF); t++) Sleep(100);
    if (t >= 600 || !VersionAttendue()) { JOURNAL("refus : version de game.exe inattendue", 0, 0, 0); return 0; }
    g_nbXbox = LireListe("XboxLayout", g_xbox);
    g_nbSony = LireListe("SonyLayout", g_sony);
    g_sonyAuto = Reglage("Gamepad", "SonyLikeAuto", 1);
    g_gachettes = Reglage("Gamepad", "SplitTriggers", 1);
    g_avisSteam = Reglage("Gamepad", "SteamInputNotice", 1);
    g_seuilGachette = Reglage("Gamepad", "TriggerThreshold", 60);
    if (g_seuilGachette < 1 || g_seuilGachette > 250) g_seuilGachette = 60;
    g_ctxValide = !memcmp(g_base + RVA_APPEL_REMISE, APPEL_REMISE, sizeof APPEL_REMISE);
    LireDispositions();
    JOURNAL("reglages (dispositions libres, lecture reconnue, gachettes a part)", g_nbPerso, g_ctxValide, g_gachettes);
    InstallerClavier();
    const int pose = ac_call(g_base + RVA_APPEL_CTOR, g_base + RVA_CTOR, (void *)hk_ctor, &g_suite) != NULL;
    JOURNAL("accroche ouverture des manettes (listes Xbox, Sony)", pose, g_nbXbox, g_nbSony);
    if (!pose) return 0;
    int n = 0;
    for (uint8_t *o = *(uint8_t **)(g_base + RVA_LISTE); o && n < 16; o = *(uint8_t **)(o + OBJ_SUIVANT), n++)
        Reclasser(o, 1);
    JOURNAL("manettes deja ouvertes", n, 0, 0);
    return 0;
}

void Gamepad_Demarrer(void)
{
#ifdef DOA5TOOLS
    if (GetModuleHandleA("DOA5LR-Gamepad.asi")) { Journal("Gamepad", "ancien DOA5LR-Gamepad.asi charge : module coupe", 0, 0, 0); return; }
#endif
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

#ifdef GAMEPAD_TESTS

int Test_LireClavier(HKL d) { g_disposition = d; const int n = LireClavier(d); LireMenu(d); Appliquer(); return n; }
void Test_EtatCombat(volatile const DWORD *etat) { g_etatCombat = etat; }
const uint16_t *Test_TableClavier(void) { return g_clavier; }
int Test_CodeTouche(const char *s, HKL d) { return CodeTouche(s, d); }
int Test_LireDisposition(const char *s, int8_t *vers) { Disposition d; const int r = LireDisposition(s, &d); memcpy(vers, d.vers, 32); return r; }
void Test_Brancher(uint8_t *base, int ctxValide, void *suiteTouche) { g_base = base; g_ctxValide = ctxValide; g_suiteTouche = suiteTouche; g_clavierPose = 1; }
void Test_ToucheAccroche(void *self, void *ev) { hk_touche(self, ev); }
void Test_LireDispositions(void) { g_nbPerso = 0; LireDispositions(); }
void Test_RemisePerso(uint8_t *etat, uint8_t *ctx) { RemisePerso(etat, ctx); }
void Test_XInput(XInputGetState_t f) { g_xget = f; g_xpret = 1; g_xindex = -1; g_xrecherche = 0; }
void Test_RemiseXbox(uint8_t *etat, uint8_t *ctx) { RemiseXbox(etat, ctx); }
#endif

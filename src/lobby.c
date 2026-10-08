#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "menu_tables.h"
#include "commun.h"

#define VERSION_MODULE "1.0.3"
#define NB(t) (sizeof(t) / sizeof((t)[0]))
typedef uint64_t CSteamID;

static const uint8_t SIGNATURE[16] = {0x55, 0x8b, 0xec, 0x56, 0x57, 0x8b, 0x7d, 0x08, 0x8d, 0x47, 0xb2, 0x8b, 0xf1, 0x83, 0xf8, 0x1a};
#define RVA_SIGNATURE        0x708ED0
#define RVA_VT_ONLINE_MAIN   0xC0FA54
#define RVA_MENU_INIT        0x707A20
#define RVA_MENU_SELECT      0x708400
#define RVA_MENU_ENTER       0x708B70
#define RVA_VT_LOBBY_INVITE  0xC0F9E0
#define RVA_LOBBY_INVITE     0x702020
#define RVA_VT_INVITATION    0xC26558
#define RVA_INVITATION       0x840BF0
#define RVA_INVITATION_RESET 0x840B90
#define RVA_INVITATION_RESET2 0x504080
#define RVA_INVITATION_PTR   0xF83C50
#define RVA_INVITATION_OBJ   0xF83C08
#define RVA_RESEAU_PTR       0x20924BC
#define RVA_JONCTION         0x8C2B90
#define RVA_CREATION_PARAM   0x8C2980
#define RVA_CREATION_TYPE    0x8C7870
#define RVA_IAT_MATCHMAKING  0x9663F0
#define RVA_IAT_RUNCALLBACKS 0x9663DC

#define RVA_ECRAN            0x1087A4C
#define RVA_FLAG_A           0x206EB8D
#define RVA_FLAG_B           0xFCDB8F
#define RVA_FLAG_C           0x107ED55
#define RVA_FLAG_D           0x1087A08
static const uint8_t DEBUT_FONCTION_8[6]  = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};
static const uint8_t DEBUT_FONCTION_24[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24};
#define MM_REQUEST_DATA      28
#define MM_MEMBER_LIMIT      32
#define FRIENDS_INVITE_DIALOG 27

static uint8_t *g_base;

static void Note(const char *libelle, long a) { Journal("Lobby", libelle, a, 0, 0); }

static int Lire(const void *adresse, void *sortie, SIZE_T n)
{
    SIZE_T lu = 0;
    return ReadProcessMemory(GetCurrentProcess(), adresse, sortie, n, &lu) && lu == n;
}
static uintptr_t LirePointeur(uintptr_t adresse)
{
    uint32_t v = 0;
    if (!Lire((void *)adresse, &v, 4) || v - 0x10001u > 0x7FFDFFFEu) return 0;
    return v;
}
static int SalonValide(CSteamID id)
{
    const uint32_t lo = (uint32_t)id, hi = (uint32_t)(id >> 32);
    return lo && (hi >> 24) == 1 && ((hi >> 20) & 0xF) == 8 && (hi & 0x40000);
}
static int RemplacerPointeur(DWORD rva, void *attendu, void *crochet, void **original)
{
    void **slot = (void **)(g_base + rva);
    if (*slot != attendu) return 0;
    DWORD ancien;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &ancien)) return 0;
    *original = *slot;
    InterlockedExchange((volatile LONG *)slot, (LONG)(uintptr_t)crochet);
    VirtualProtect(slot, sizeof(void *), ancien, &ancien);
    return 1;
}
static void PoserRel32(uint8_t *ou, const void *cible) { *(int32_t *)ou = (int32_t)((const uint8_t *)cible - (ou + 4)); }

typedef void (__cdecl *Handler_t)(uint32_t *r);
static int Detourner(DWORD rva, const uint8_t debut[6], Handler_t handler)
{
    uint8_t *site = g_base + rva;
    if (memcmp(site, debut, 6)) return 0;
    uint8_t *s = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!s) return 0;
    uint8_t *p = s;
    *p++ = 0x60; *p++ = 0x9C;
    *p++ = 0x8D; *p++ = 0x44; *p++ = 0x24; *p++ = 0x04;
    *p++ = 0x50;
    *p++ = 0xE8; PoserRel32(p, (void *)handler); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;
    *p++ = 0x9D; *p++ = 0x61;
    memcpy(p, debut, 6); p += 6;
    *p++ = 0xE9; PoserRel32(p, site + 6); p += 4;
    DWORD ancien;
    if (!VirtualProtect(s, 64, PAGE_EXECUTE_READ, &ancien)) return 0;
    FlushInstructionCache(GetCurrentProcess(), s, 64);
    if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &ancien)) return 0;
    site[0] = 0xE9; PoserRel32(site + 1, s); site[5] = 0x90;
    VirtualProtect(site, 6, ancien, &ancien);
    FlushInstructionCache(GetCurrentProcess(), site, 6);
    return 1;
}

typedef void (__thiscall *MenuFn)(void *, void *);
static MenuFn g_initCopie;
static const DWORD LIBELLES[3] = {0xA30004, 0xA30001, 0xA30003};
static unsigned char g_ressource[0x200];
static void Poser32(unsigned char *p, DWORD v) { memcpy(p, &v, 4); }

static unsigned char *Copier(unsigned rva, unsigned taille, const Rel *rel, size_t n)
{
    unsigned char *p = (unsigned char *)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) return NULL;
    memcpy(p, g_base + rva, taille);
    for (size_t i = 0; i < n; ++i) {
        const uintptr_t dest = rel[i].target >= rva && rel[i].target < rva + taille
                             ? (uintptr_t)p + rel[i].target - rva : (uintptr_t)g_base + rel[i].target;
        Poser32(p + rel[i].offset, (DWORD)(dest - ((uintptr_t)p + rel[i].offset + 4)));
    }
    return p;
}
static int Finir(unsigned char *p)
{
    DWORD ancien;
    return VirtualProtect(p, 0x2000, PAGE_EXECUTE_READ, &ancien) && FlushInstructionCache(GetCurrentProcess(), p, 0x2000);
}
static void __fastcall hk_menuInit(void *objet, void *edx, void *arg)
{
    (void)edx;
    unsigned char *obj = (unsigned char *)objet;
    DWORD origine = 0;
    unsigned char octets[sizeof MenuResource];
    Lire(obj + 0x60, &origine, 4);
    if (origine && Lire((void *)(uintptr_t)origine, octets, sizeof octets) && !memcmp(octets, MenuResource, sizeof octets) &&
        (uintptr_t)arg >= origine && (uintptr_t)arg < origine + sizeof octets) {
        Poser32(obj + 0x60, (DWORD)(uintptr_t)g_ressource);
        g_initCopie(objet, g_ressource + ((uintptr_t)arg - origine));
        Poser32(obj + 0x60, origine);
    } else {
        Note("menu_ressource_inattendue -> menu d'origine", 0);
        ((MenuFn)(g_base + RVA_MENU_INIT))(objet, arg);
    }
}
static int InstallerMenu(void)
{
    unsigned char *init = Copier(RVA_MENU_INIT, 0x9DE, InitRel, NB(InitRel));
    unsigned char *select = Copier(RVA_MENU_SELECT, 0x1E4, SelectRel, NB(SelectRel));
    unsigned char *enter = Copier(RVA_MENU_ENTER, 0x35D, EnterRel, NB(EnterRel));
    if (!init || !select || !enter) return 0;

    const unsigned offsets[3] = {0x708205 - RVA_MENU_INIT, 0x70824A - RVA_MENU_INIT, 0x7082CA - RVA_MENU_INIT};
    const unsigned char regs[3] = {0x0C, 0x04, 0x14};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned char *p = init + offsets[i];
        p[0] = 0x8B; p[1] = regs[i]; p[2] = 0xB5; Poser32(p + 3, (DWORD)(uintptr_t)LIBELLES);
    }
    init[0x7083CB - RVA_MENU_INIT] = 3;
    select[0x7084AA - RVA_MENU_SELECT] = 3;
    enter[0x708C83 - RVA_MENU_ENTER] = 3;

    Poser32(select + 0x7084BF - RVA_MENU_SELECT, (DWORD)(uintptr_t)(select + 0x1DC));
    Poser32(select + 0x7084C6 - RVA_MENU_SELECT, (DWORD)(uintptr_t)(select + 0x1D0));
    for (unsigned o = 0x1D0; o < 0x1DC; o += 4) {
        DWORD cible;
        memcpy(&cible, select + o, 4);
        Poser32(select + o, (DWORD)((uintptr_t)select + cible - ((uintptr_t)g_base + RVA_MENU_SELECT)));
    }

    unsigned char *stub = select + 0x1000;
    memcpy(stub, select + 0xE1, 14);
    const unsigned char extra[7] = {0xC7, 0x45, 0xEC, 0x02, 0x00, 0x14, 0x00};
    memcpy(stub + 14, extra, 7);
    stub[21] = 0xE9; Poser32(stub + 22, (DWORD)((select + 0xEF) - (stub + 26)));
    memset(select + 0xE1, 0x90, 14);
    select[0xE1] = 0xE9; Poser32(select + 0xE2, (DWORD)(stub - (select + 0xE6)));

    memcpy(g_ressource, MenuResource, sizeof MenuResource);
    Poser32(g_ressource + 0x28, 3); Poser32(g_ressource + 0x2C, 0x160); Poser32(g_ressource + 0x30, 3);
    Poser32(g_ressource + 0x34, 0x1A8); Poser32(g_ressource + 0x98, 3);
    memcpy(g_ressource + 0x160, g_ressource + 0xA0, 48); memcpy(g_ressource + 0x190, g_ressource + 0xB8, 24);
    Poser32(g_ressource + 0x194, 0x121); Poser32(g_ressource + 0x19C, 2); Poser32(g_ressource + 0x1A0, 0x3A8);
    memcpy(g_ressource + 0x1A8, g_ressource + 0xE0, 32);
    Poser32(g_ressource + 0x1C8, 0x3F); Poser32(g_ressource + 0x1CC, 2); Poser32(g_ressource + 0x1D0, 0xFFFFFFFF);
    Poser32(g_ressource + 0x1D4, 0x64);
    if (!Finir(init) || !Finir(select) || !Finir(enter)) return 0;
    g_initCopie = (MenuFn)init;
    DWORD *vt = (DWORD *)(g_base + RVA_VT_ONLINE_MAIN), ancien;
    if (vt[1] != (DWORD)(uintptr_t)(g_base + RVA_MENU_INIT) || vt[8] != (DWORD)(uintptr_t)(g_base + RVA_MENU_SELECT) ||
        vt[9] != (DWORD)(uintptr_t)(g_base + RVA_MENU_ENTER)) return 0;
    if (!VirtualProtect(vt, 40, PAGE_READWRITE, &ancien)) return 0;
    InterlockedExchange((LONG *)(vt + 8), (LONG)(uintptr_t)select);
    InterlockedExchange((LONG *)(vt + 9), (LONG)(uintptr_t)enter);
    InterlockedExchange((LONG *)(vt + 1), (LONG)(uintptr_t)hk_menuInit);
    VirtualProtect(vt, 40, ancien, &ancien);
    return 1;
}

static void *Matchmaking(void)
{
    typedef void *(*Acces_t)(void);
    const uintptr_t f = LirePointeur((uintptr_t)(g_base + RVA_IAT_MATCHMAKING));
    return f ? ((Acces_t)f)() : NULL;
}
static int CapaciteSalon(CSteamID salon)
{
    typedef int (__thiscall *Limite_t)(void *, CSteamID);
    void *mm = Matchmaking();
    return mm ? ((Limite_t)(*(void ***)mm)[MM_MEMBER_LIMIT])(mm, salon) : 0;
}
static int DemanderDonnees(CSteamID salon)
{
    typedef uint8_t (__thiscall *Demande_t)(void *, CSteamID);
    void *mm = Matchmaking();
    return mm ? ((Demande_t)(*(void ***)mm)[MM_REQUEST_DATA])(mm, salon) : 0;
}

#define CLIENT_GET_FRIENDS   8
static int OuvrirFenetreInvitation(CSteamID salon)
{
    HMODULE api = GetModuleHandleA("steam_api.dll");
    if (!api) return -1;
    if (!SalonValide(salon)) return -2;
    typedef void *(*Client_t)(void);
    typedef int (*Poignee_t)(void);
    Client_t client = (Client_t)GetProcAddress(api, "SteamClient");
    Poignee_t user = (Poignee_t)GetProcAddress(api, "SteamAPI_GetHSteamUser");
    Poignee_t pipe = (Poignee_t)GetProcAddress(api, "SteamAPI_GetHSteamPipe");
    if (!client || !user || !pipe) return -3;
    void *c = client();
    const int hu = user(), hp = pipe();
    if (!c || !hp) return -4;
    typedef void *(__thiscall *GetFriends_t)(void *, int, int, const char *);
    void *amis = ((GetFriends_t)(*(void ***)c)[CLIENT_GET_FRIENDS])(c, hu, hp, "SteamFriends014");
    if (!amis) return -5;
    void *fn = (*(void ***)amis)[FRIENDS_INVITE_DIALOG];
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(fn, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || !(mbi.Protect & 0xF0)) return -6;
    typedef void (__thiscall *Dialogue_t)(void *, CSteamID);
    ((Dialogue_t)fn)(amis, salon);
    return 1;
}

typedef int (__thiscall *Evenement_t)(void *, int, int, int);
static Evenement_t o_menuLobby;
static DWORD g_dernierEnvoi;
static int __fastcall hk_menuLobby(void *self, void *edx, int evt, int b, int c)
{
    (void)edx;

    int32_t etat = 0;
    Lire((uint8_t *)self + 0x4844, &etat, 4);
    const int r = o_menuLobby(self, evt, b, c);
    if (evt == 0xAC) Note("bouton_inviter etat(-1 attendu)", etat);
    if (evt == 0xAC && etat == -1) {
        const DWORD now = GetTickCount();
        if (!g_dernierEnvoi || now - g_dernierEnvoi >= 1500) {
            const uintptr_t reseau = LirePointeur((uintptr_t)(g_base + RVA_RESEAU_PTR));
            CSteamID salon = 0;
            if (reseau && Lire((void *)(reseau + 0xB0), &salon, 8) && SalonValide(salon)) {
                g_dernierEnvoi = now;
                Note("invitation_fenetre_ouverte", OuvrirFenetreInvitation(salon));
            }
        }
    }
    return r;
}

static volatile LONG g_invEnCours;
static CSteamID g_invSalon;
static DWORD g_invDebut;
static CSteamID g_attenteSalon;
static DWORD g_attenteDebut;
static CSteamID g_dernierSalonVu;
static DWORD g_dernierVu;

static uintptr_t ObjetInvitation(void)
{
    const uintptr_t p = LirePointeur((uintptr_t)(g_base + RVA_INVITATION_PTR));
    return p ? p : (uintptr_t)(g_base + RVA_INVITATION_OBJ);
}

static void AccepterInvitation(CSteamID salon)
{
    const uintptr_t reseau = LirePointeur((uintptr_t)(g_base + RVA_RESEAU_PTR));
    if (!reseau) { Note("invitation_sans_objet_reseau", 0); return; }
    int cap = salon ? CapaciteSalon(salon) : 0;
    if (cap < 1 || cap > 64) cap = 8;
    *(volatile uint32_t *)(reseau + 0xA0) = (uint32_t)cap;
    *(volatile uint32_t *)(reseau + 0xA4) = 0;
    *(volatile CSteamID *)(reseau + 0xA8) = salon;
    const uintptr_t obj = ObjetInvitation();
    typedef int (__thiscall *Traiter_t)(void *, int, int);
    const uintptr_t fn = LirePointeur(LirePointeur(obj) + 0x4C);
    if (fn) ((Traiter_t)fn)((void *)obj, 0, 0);
    Note("invitation_acceptee capacite", cap);
}

typedef int (__thiscall *Invitation_t)(void *, int, int);
static Invitation_t o_invitation;
static int __fastcall hk_invitation(void *self, void *edx, int a, int b)
{
    (void)edx;
    uint8_t *obj = (uint8_t *)self;

    const uintptr_t reseau = LirePointeur((uintptr_t)(g_base + RVA_RESEAU_PTR));
    uint32_t cap = 0;
    CSteamID salon = 0;
    if (reseau) { Lire((void *)(reseau + 0xA0), &cap, 4); Lire((void *)(reseau + 0xA8), &salon, 8); }
    if (reseau && SalonValide(salon)) {
        const int limite = CapaciteSalon(salon);
        if (limite >= 1 && limite <= 64 && (uint32_t)limite != cap) *(volatile uint32_t *)(reseau + 0xA0) = (uint32_t)limite;
    }
    const int r = o_invitation(self, a, b);
    const uint8_t pret = obj[0x20];
    const DWORD now = GetTickCount();
    const int doublon = SalonValide(salon) && salon == g_dernierSalonVu && g_dernierVu && now - g_dernierVu < 10000;
    if (SalonValide(salon)) { g_dernierSalonVu = salon; g_dernierVu = now; }
    if (pret) obj[0x22] = 1;
    if (!doublon && SalonValide(salon)) {
        g_invSalon = salon;
        g_invDebut = now;
        InterlockedExchange(&g_invEnCours, 1);
        Note("invitation_recue capacite", (long)(reseau ? *(volatile uint32_t *)(reseau + 0xA0) : 0));
    }
    return r;
}

static void __cdecl SurJonction(uint32_t *r)
{
    const uintptr_t p = r[9];
    if (!g_invEnCours || p <= 0x10000) return;

    const uintptr_t fiche = LirePointeur(p);
    CSteamID avant = 0;
    if (!fiche || !Lire((void *)(fiche + 8), &avant, 8)) {
        InterlockedExchange(&g_invEnCours, 0);
        Note("jonction_fiche_illisible", 0);
        return;
    }
    if (avant != g_invSalon) *(volatile CSteamID *)(fiche + 8) = g_invSalon;
    InterlockedExchange(&g_invEnCours, 0);
    Note("jonction_vers_salon_invite deja_le_bon_salon", avant == g_invSalon);
}

static volatile LONG g_typeSalon = -1;
static void __cdecl SurParametresCreation(uint32_t *r)
{
    const uintptr_t p = r[9];
    if (p <= 0x10000) return;
    uint32_t places = 0, privees = 0;
    if (!Lire((void *)p, &places, 4) || !Lire((void *)(p + 0x38), &privees, 4)) return;
    const LONG type = privees == 0 ? 2 : (places && privees >= places) ? 0 : 1;
    InterlockedExchange(&g_typeSalon, type);
    Note("creation_salon type(0=prive,1=amis,2=public)", type);
}
static void __cdecl SurTypeCreation(uint32_t *r)
{
    const LONG type = g_typeSalon;
    if (type != -1) r[9] = (uint32_t)type;
}

typedef struct CB { void **vt; uint8_t flags; uint8_t pad[3]; int id; int size; } CB;
static void SurCallback(int id, const uint8_t *d)
{
    const CSteamID salon = *(const uint64_t *)d;
    if (id == 333) {
        if (!SalonValide(salon)) return;
        const int cap = CapaciteSalon(salon);
        if (cap >= 1 && cap <= 64) { AccepterInvitation(salon); return; }
        if (DemanderDonnees(salon)) { g_attenteSalon = salon; g_attenteDebut = GetTickCount(); return; }
        AccepterInvitation(salon);
    } else if (id == 505) {
        if (g_attenteSalon && salon == g_attenteSalon && *(const uint64_t *)(d + 8) == salon) {
            g_attenteSalon = 0; g_attenteDebut = 0;
            AccepterInvitation(salon);
        }
    }
}
static void __thiscall cb_run_result(CB *self, void *pv, int io, uint64_t call) { (void)io; (void)call; SurCallback(self->id, pv); }
static void __thiscall cb_run(CB *self, void *pv) { SurCallback(self->id, pv); }
static int __thiscall cb_size(CB *self) { return self->size; }
static void *g_cbVt[3] = { (void *)cb_run_result, (void *)cb_run, (void *)cb_size };
static CB g_cbs[2] = { { g_cbVt, 0, {0}, 333, 16 }, { g_cbVt, 0, {0}, 505, 24 } };
typedef void (__cdecl *RegisterCallback_t)(CB *cb, int id);

static void RemettreAZero(void)
{
    const uintptr_t obj = ObjetInvitation();
    typedef void (__thiscall *Reset_t)(void *);
    typedef void (__cdecl *Reset2_t)(int);
    ((Reset_t)(g_base + RVA_INVITATION_RESET))((void *)obj);
    ((Reset2_t)(g_base + RVA_INVITATION_RESET2))(0);
    InterlockedExchange(&g_invEnCours, 0);
    Note("f12_invitation_remise_a_zero", 0);
}
static int LireOctet(DWORD rva) { uint8_t v = 0; Lire(g_base + rva, &v, 1); return v; }

static void Tic(void)
{
    static int f12Avant;
    const int f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    if (f12 && !f12Avant) RemettreAZero();
    f12Avant = f12;
    const DWORD now = GetTickCount();
    if (g_attenteSalon && g_attenteDebut && now - g_attenteDebut > 3000) {
        const CSteamID salon = g_attenteSalon;
        g_attenteSalon = 0; g_attenteDebut = 0;
        Note("invitation_delai_capacite_par_defaut", 3);
        AccepterInvitation(salon);
    }
    if (g_invEnCours && g_invDebut && now - g_invDebut > 180000) {
        InterlockedExchange(&g_invEnCours, 0);
        Note("invitation_expiree_s", 180);
    }

    static uint32_t ecranAvant = 0xFFFFFFFF;
    static DWORD ecranDepuis, dernierDeverrouillage;
    uint32_t ecran = 0;
    Lire(g_base + RVA_ECRAN, &ecran, 4);
    if (ecran != ecranAvant) { ecranAvant = ecran; ecranDepuis = now; }
    const uintptr_t inv = LirePointeur((uintptr_t)(g_base + RVA_INVITATION_PTR));
    uint32_t etat = 0;
    const int prete = inv && Lire((void *)(inv + 0x20), &etat, 4) && (etat & 0xFF) == 1 && (etat & 0xFF0000) == 0x10000;
    if (prete && LireOctet(RVA_FLAG_A) && ecran != 2 && !LireOctet(RVA_FLAG_D) &&
        (LireOctet(RVA_FLAG_B) || !LireOctet(RVA_FLAG_C)) && now - ecranDepuis > 300) {
        g_base[RVA_FLAG_B] = 0;
        g_base[RVA_FLAG_C] = 1;
        if (now - dernierDeverrouillage > 1000) { dernierDeverrouillage = now; Note("chemin_natif_deverrouille", 1); }
    }
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    int ok = 0;
    for (int t = 0; t < 600 && !ok; t++) {
        ok = !memcmp(g_base + RVA_SIGNATURE, SIGNATURE, sizeof SIGNATURE);
        if (!ok) Sleep(100);
    }
    if (!ok) { Note("refus_version_game_exe", 0); return 0; }
    Note("menu_natif", InstallerMenu());
    Note("envoi_invitation", RemplacerPointeur(RVA_VT_LOBBY_INVITE, g_base + RVA_LOBBY_INVITE, (void *)hk_menuLobby, (void **)&o_menuLobby));
    Note("reception_invitation", RemplacerPointeur(RVA_VT_INVITATION, g_base + RVA_INVITATION, (void *)hk_invitation, (void **)&o_invitation));
    Note("jonction", Detourner(RVA_JONCTION, DEBUT_FONCTION_8, SurJonction));
    Note("salon_prive", Detourner(RVA_CREATION_PARAM, DEBUT_FONCTION_24, SurParametresCreation) &&
                        Detourner(RVA_CREATION_TYPE, DEBUT_FONCTION_8, SurTypeCreation));
    HMODULE sa = NULL;
    for (int t = 0; t < 1200 && !sa; t++) { sa = GetModuleHandleA("steam_api.dll"); if (!sa) Sleep(100); }
    RegisterCallback_t reg = sa ? (RegisterCallback_t)GetProcAddress(sa, "SteamAPI_RegisterCallback") : NULL;
    if (reg) { reg(&g_cbs[0], 333); reg(&g_cbs[1], 505); }
    Note("callbacks", reg != NULL);
    return 0;
}

void Lobby_Demarrer(void)
{
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    Note("debut " VERSION_MODULE, 0);
    AjouterTic(Tic);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

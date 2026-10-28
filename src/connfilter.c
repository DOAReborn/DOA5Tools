#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "commun.h"

typedef uint64_t SteamID;
static uint8_t *g_base;
static void Note(const char *libelle, long a, long b, long c) { Journal("ConnFilter", libelle, a, b, c); }
static int Lisible(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!p || (uintptr_t)p < 0x10000 || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const uint8_t *)p + n <= (const uint8_t *)m.BaseAddress + m.RegionSize;
}
static int Ecrire(void *a, const void *d, size_t n)
{
    DWORD o;
    if (!VirtualProtect(a, n, PAGE_EXECUTE_READWRITE, &o)) return 0;
    memcpy(a, d, n);
    VirtualProtect(a, n, o, &o);
    FlushInstructionCache(GetCurrentProcess(), a, n);
    return 1;
}
static int Pointeur(void **slot, void *hook, void **orig)
{
    DWORD o;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &o)) return 0;
    *orig = *slot;
    InterlockedExchange((volatile LONG *)slot, (LONG)(uintptr_t)hook);
    VirtualProtect(slot, sizeof(void *), o, &o);
    return 1;
}
static void PoserAppel(uint8_t *a, void *fn, int len)
{
    uint8_t c[8]; memset(c, 0x90, sizeof c);
    c[0] = 0xE8; *(int32_t *)(c + 1) = (int32_t)((uint8_t *)fn - (a + 5));
    Ecrire(a, c, len);
}
static int AppelVers(const uint8_t *c, uint32_t rva) { return c[0] == 0xE8 && c + 5 + *(const int32_t *)(c + 1) == g_base + rva; }

#define RVA_NET_PTR 0x20924BC
static void *g_mm, *g_net, *g_user;
typedef SteamID *(__thiscall *Proprio_t)(void *self, SteamID *ret, SteamID lobby);
typedef SteamID *(__thiscall *MonId_t)(void *self, SteamID *ret);
typedef uint8_t (__thiscall *DonneeSalon_t)(void *self, SteamID lobby, const char *cle, const char *valeur);
typedef const char *(__thiscall *LireDonnee_t)(void *self, SteamID lobby, const char *cle);
typedef uint8_t (__thiscall *EnvoiP2P_t)(void *self, SteamID dest, const void *d, uint32_t n, int type, int canal);
typedef uint8_t (__thiscall *LectureP2P_t)(void *self, void *dest, uint32_t n, uint32_t *taille, SteamID *exp, int canal);
typedef uint8_t (__thiscall *FermerP2P_t)(void *self, SteamID dest);
static SteamID SalonCourant(void)
{
    const uint8_t *net = Lisible(g_base + RVA_NET_PTR, 4) ? *(uint8_t **)(g_base + RVA_NET_PTR) : NULL;
    SteamID id = 0;
    if (net && Lisible(net + 0xB0, 8)) memcpy(&id, net + 0xB0, 8);
    return id;
}

static int SuisHote(SteamID salon)
{
    if (!g_mm || !g_user || !salon) return 0;
    SteamID proprio = 0, moi = 0;
    ((Proprio_t)(*(void ***)g_mm)[35])(g_mm, &proprio, salon);
    ((MonId_t)(*(void ***)g_user)[2])(g_user, &moi);
    return proprio && proprio == moi;
}
static void FermerP2P(SteamID id) { if (g_net && id) ((FermerP2P_t)(*(void ***)g_net)[4])(g_net, id); }

#define MAX_BLOQUES 16
static SteamID g_bloques[MAX_BLOQUES]; static volatile LONG g_nbBloques; static SteamID g_salonBloques;
static int Bloque(SteamID id)
{
    const LONG n = g_nbBloques;
    for (LONG i = 0; i < n && i < MAX_BLOQUES; i++) if (g_bloques[i] == id) return 1;
    return 0;
}
static void Bloquer(SteamID salon, SteamID id)
{
    if (g_salonBloques != salon) { g_nbBloques = 0; g_salonBloques = salon; }
    if (!id || Bloque(id) || g_nbBloques >= MAX_BLOQUES) return;
    g_bloques[g_nbBloques] = id; InterlockedIncrement(&g_nbBloques);
    Note("bloque nombre", g_nbBloques, 0, 0);
}
static void ViderBloques(void) { if (g_nbBloques) Note("blocage_vide", 0, 0, 0); g_nbBloques = 0; memset(g_bloques, 0, sizeof g_bloques); g_salonBloques = 0; }

static SteamID g_sursis; static DWORD g_sursisFin; static int g_envoiLibre;
static EnvoiP2P_t o_envoiP2P; static LectureP2P_t o_lectureP2P;
static uint8_t __fastcall EnvoiP2P(void *self, void *edx, SteamID dest, const void *d, uint32_t n, int type, int canal)
{
    (void)edx;
    if (g_nbBloques && Bloque(dest) && !g_envoiLibre && dest != g_sursis) return 1;
    return o_envoiP2P(self, dest, d, n, type, canal);
}
static uint8_t __fastcall LectureP2P(void *self, void *edx, void *dest, uint32_t n, uint32_t *taille, SteamID *exp, int canal)
{
    (void)edx;
    SteamID de = 0;
    const uint8_t r = o_lectureP2P(self, dest, n, taille, &de, canal);
    if (r && g_nbBloques && Bloque(de) && de != g_sursis) { if (taille) *taille = 0; if (exp) *exp = 0; return 0; }
    if (exp) *exp = de;
    return r;
}

#define OPT_CONN      0x44
#define RVA_VT_SEARCH 0xC0FFBC
#define RVA_LABEL3    0x70F6A0
#define RVA_SHOWTAB   0x7104E5
#define RVA_SELTAB    0x70C1C9
#define RVA_LOADSKIP  0x70D0A8
#define RVA_AIDE1     0x70D6B2
#define RVA_RANGER    0x70D12D
#define RVA_TXT_JT1   0x70C624
#define RVA_TXT_44    0x70C257
#define RVA_TXT_CONN  0x6FD5AB
#define RVA_PAGE_FN   0x70C8B0
#define RVA_CHARGER   0x70C9D0
#define RVA_AJOUT     0x7D80A0
#define RVA_RETRAIT   0x7D8180
#define RVA_APPEL_CHARGER 0x711AFB
#define RVA_REGLAGES_CONN 0xF818E0
static const uint8_t AIDE_ORIG[] = {0xC7, 0x45, 0xC0, 0x6C, 0x00, 0x14, 0x00};
static const uint8_t RANGER_ORIG[] = {0x89, 0xBE, 0x0C, 0x31, 0x00, 0x00};
static const uint8_t AJOUT_ORIG[] = {0x55, 0x8B, 0xEC, 0x56, 0x57, 0x8B, 0xF9}, RETRAIT_ORIG[] = {0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x2C};
static const uint32_t APPELS_PAGE[] = {0x711AED, 0x711B10, 0x714581};
typedef struct { uint32_t rva, de, vers; int taille; } Imm;
static const Imm MENU[] = {{RVA_LABEL3, 0xAB0008, 0xAB0016, 4}, {RVA_SHOWTAB, 5, 1, 1}, {RVA_SELTAB, 4, 1, 1}, {RVA_LOADSKIP, 0, 1, 1}};
typedef void (__thiscall *Ajout_t)(void *m, int opt, int val, int a, int b);
typedef void (__thiscall *Retrait_t)(void *m, int opt, int val);
int cf_pageCreer;
void *cf_txtRegion, *cf_txtConn, *cf_pageFn, *cf_chargerFn;
static volatile uint8_t *g_menuCreer;
static int g_choix;
static int g_minBarres;
static int Barres(int choix) { return choix == 1 ? 5 : choix == 2 ? 4 : choix == 3 ? 3 : 0; }
__asm__(".text\n_cf_aide:\n"
        "  pushfl\n"
        "  movl $0x14006C, -0x40(%ebp)\n"
        "  cmpl $0, _cf_pageCreer\n"
        "  je 1f\n"
        "  movl $0x1401DF, -0x40(%ebp)\n"
        "1:\n"
        "  popfl\n"
        "  ret\n");
__asm__(".text\n_cf_txt44:\n"
        "  cmpl $0, _cf_pageCreer\n"
        "  jne 1f\n"
        "  jmp *_cf_txtRegion\n"
        "1:\n"
        "  jmp *_cf_txtConn\n");
void cf_aide(void); void cf_txt44(void);
static uint8_t *Choix(uint8_t *m, int opt, int val, int *cur)
{
    *cur = -1;
    uint8_t **items = *(uint8_t ***)(m + 0x2C); const int n = *(int *)(m + 0x24);
    if (n <= 0 || n > 64 || !Lisible(items, n * 4)) return NULL;
    for (int i = 0; i < n; i++) {
        uint8_t *it = items[i];
        if (!Lisible(it, 0x48) || *(int *)it != opt) continue;
        uint8_t **ents = *(uint8_t ***)(it + 0x34);
        if (Lisible(ents, 4) && Lisible(ents[0], 0x10)) { uint8_t *v = *(uint8_t **)(ents[0] + 0xC); if (Lisible(v, 8)) *cur = *(int *)(v + 4); }
        const int nc = *(int *)(it + 0x3C); uint8_t **cs = *(uint8_t ***)(it + 0x44);
        if (nc <= 0 || nc > 64 || !Lisible(cs, nc * 4)) return NULL;
        for (int j = 0; j < nc; j++) if (Lisible(cs[j], 8) && *(int *)(cs[j] + 4) == val) return cs[j];
        return NULL;
    }
    return NULL;
}
__attribute__((used)) void __cdecl cf_apresPage(uint8_t *m, int rechercher)
{
    if (!m || *(uint32_t *)m != (uint32_t)(uintptr_t)(g_base + RVA_VT_SEARCH)) return;
    g_menuCreer = m;
    int cur; const int creer = !rechercher && *(int *)(m + 0x2CC) == 3;
    cf_pageCreer = creer;
    if (creer && !Choix(m, OPT_CONN, 3, &cur)) ((Ajout_t)(g_base + RVA_AJOUT))(m, OPT_CONN, 3, -1, 0x4D);
}
__attribute__((used)) void __cdecl cf_apresCharger(uint8_t *m)
{
    int cur;
    if (cf_pageCreer || !m || !Choix(m, OPT_CONN, 3, &cur) || cur == 3) return;
    ((Retrait_t)(g_base + RVA_RETRAIT))(m, OPT_CONN, 3);
}
__asm__(".text\n_cf_page:\n"
        "  push %ecx\n"
        "  pushl 8(%esp)\n"
        "  call *_cf_pageFn\n"
        "  pop %ecx\n"
        "  pushl 4(%esp)\n"
        "  push %ecx\n"
        "  call _cf_apresPage\n"
        "  add $8, %esp\n"
        "  ret $4\n");
__asm__(".text\n_cf_charger:\n"
        "  push %ecx\n"
        "  call *_cf_chargerFn\n"
        "  pop %ecx\n"
        "  push %ecx\n"
        "  call _cf_apresCharger\n"
        "  add $4, %esp\n"
        "  ret\n");
void cf_page(void); void cf_charger(void);
static int ValeurConn(uint8_t *m) { int cur; Choix(m, OPT_CONN, -999, &cur); return cur >= 0 && cur <= 3 ? cur : -1; }
__attribute__((used)) void __cdecl cf_ranger(uint8_t *m)
{
    int c = 0;
    if (cf_pageCreer && *(int *)(m + 0x2CC) == 3) { const int v = ValeurConn(m); if (v >= 0) c = v; }
    if (c != g_choix) Note("validation connexion", c, 0, 0);
    g_choix = c;
    *(int *)(m + 0x310C) = c;
}
__asm__(".text\n_cf_rangerThunk:\n"
        "  pushal\n"
        "  push %esi\n"
        "  call _cf_ranger\n"
        "  add $4, %esp\n"
        "  popal\n"
        "  ret\n");
void cf_rangerThunk(void);
static int PoserMenu(void)
{
    for (size_t i = 0; i < sizeof MENU / sizeof *MENU; i++) {
        const uint8_t *a = g_base + MENU[i].rva; const uint32_t v = MENU[i].taille == 1 ? *a : *(const uint32_t *)a;
        if (v != MENU[i].de && v != MENU[i].vers) return 0;
    }
    uint8_t *aide = g_base + RVA_AIDE1, *ranger = g_base + RVA_RANGER;
    uint32_t *jt = (uint32_t *)(g_base + RVA_TXT_JT1);
    if (memcmp(aide, AIDE_ORIG, sizeof AIDE_ORIG) || memcmp(ranger, RANGER_ORIG, sizeof RANGER_ORIG)
        || *jt != (uint32_t)(uintptr_t)(g_base + RVA_TXT_44) || !AppelVers(g_base + RVA_APPEL_CHARGER, RVA_CHARGER)
        || memcmp(g_base + RVA_AJOUT, AJOUT_ORIG, sizeof AJOUT_ORIG) || memcmp(g_base + RVA_RETRAIT, RETRAIT_ORIG, sizeof RETRAIT_ORIG)) return 0;
    for (int i = 0; i < 3; i++) if (!AppelVers(g_base + APPELS_PAGE[i], RVA_PAGE_FN)) return 0;
    for (size_t i = 0; i < sizeof MENU / sizeof *MENU; i++) {
        uint8_t *a = g_base + MENU[i].rva;
        if (MENU[i].taille == 1) { const uint8_t v = (uint8_t)MENU[i].vers; Ecrire(a, &v, 1); }
        else Ecrire(a, &MENU[i].vers, 4);
    }
    PoserAppel(aide, (void *)cf_aide, sizeof AIDE_ORIG);
    PoserAppel(ranger, (void *)cf_rangerThunk, sizeof RANGER_ORIG);
    cf_txtRegion = g_base + RVA_TXT_44; cf_txtConn = g_base + RVA_TXT_CONN;
    const uint32_t tx = (uint32_t)(uintptr_t)cf_txt44; Ecrire(jt, &tx, 4);
    cf_pageFn = g_base + RVA_PAGE_FN; cf_chargerFn = g_base + RVA_CHARGER;
    for (int i = 0; i < 3; i++) PoserAppel(g_base + APPELS_PAGE[i], (void *)cf_page, 5);
    PoserAppel(g_base + RVA_APPEL_CHARGER, (void *)cf_charger, 5);
    return 1;
}

static void Panneau(SteamID salon)
{
    if (!salon || !g_mm) return;
    int barres = -1;
    if (SuisHote(salon)) barres = g_minBarres;
    else { const char *v = ((LireDonnee_t)(*(void ***)g_mm)[19])(g_mm, salon, "dlr_min_bars"); if (v && *v) barres = atoi(v); }
    if (barres < 0) return;
    *(int *)(g_base + RVA_REGLAGES_CONN) = barres >= 5 ? 1 : barres == 4 ? 2 : barres == 3 ? 3 : 0;
}

#define RVA_LIRE_SALLE 0x8BF150
#define RVA_CB_SALON   0xC39BE0
#define RVA_RETRAIT_MEMBRE 0x8C66B0
#define RVA_ENVOI_SALLE    0x8C5A20
static const uint8_t RETRAIT_MEMBRE_ORIG[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x53, 0x56, 0x57, 0x8B, 0x3D};
static const uint8_t ENVOI_SALLE_ORIG[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xA1};
typedef void (__thiscall *RetraitMembre_t)(void *self, SteamID id);
typedef void (__thiscall *EnvoiSalle_t)(void *self, SteamID dest, const void *d, uint32_t n, uint32_t type);
static uint8_t *Salle(void)
{
    const uint8_t *f = g_base + RVA_LIRE_SALLE;
    if (f[0] != 0xA1 || f[5] != 0xC3) return NULL;
    uint8_t **pp = *(uint8_t ***)(f + 1);
    uint8_t *ctx = Lisible(pp, 4) ? *pp : NULL;
    return ctx && Lisible(ctx + 8, 4) ? *(uint8_t **)(ctx + 8) : NULL;
}
static uint8_t *ChercherObjetSalon(SteamID salon)
{
    const uint32_t vt = (uint32_t)(uintptr_t)(g_base + RVA_CB_SALON);
    MEMORY_BASIC_INFORMATION mi;
    for (uint8_t *a = (uint8_t *)0x10000; a < (uint8_t *)0x7FFE0000 && VirtualQuery(a, &mi, sizeof mi); a = (uint8_t *)mi.BaseAddress + mi.RegionSize) {
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || mi.Protect != PAGE_READWRITE) continue;
        uint32_t *p = (uint32_t *)mi.BaseAddress, *e = (uint32_t *)((uint8_t *)mi.BaseAddress + mi.RegionSize) - 4;
        for (; p < e; p++) {
            if (*p != vt || p[2] != 506) continue;
            uint8_t *obj = (uint8_t *)p - 0x1C;
            if ((uint8_t *)(uintptr_t)p[3] == obj && obj >= (uint8_t *)mi.BaseAddress && *(SteamID *)obj == salon) return obj;
        }
    }
    return NULL;
}
static uint8_t *g_objSalon; static SteamID g_objSalonId;
static uint8_t *ObjetSalon(SteamID salon)
{
    if (g_objSalon && g_objSalonId == salon && Lisible(g_objSalon, 0x20) && *(SteamID *)g_objSalon == salon
        && *(uint32_t *)(g_objSalon + 0x1C) == (uint32_t)(uintptr_t)(g_base + RVA_CB_SALON)) return g_objSalon;
    g_objSalon = ChercherObjetSalon(salon); g_objSalonId = salon;
    return g_objSalon;
}
static int EnvoiSalle(SteamID salon, SteamID dest, const void *d, uint32_t n, uint32_t type)
{
    if (memcmp(g_base + RVA_ENVOI_SALLE, ENVOI_SALLE_ORIG, sizeof ENVOI_SALLE_ORIG)) return 0;
    uint8_t *obj = ObjetSalon(salon);
    if (!obj) return 0;
    g_envoiLibre = 1;
    ((EnvoiSalle_t)(g_base + RVA_ENVOI_SALLE))(obj, dest, d, n, type);
    g_envoiLibre = 0;
    return 1;
}
static void RetirerMembre(SteamID salon, SteamID qui)
{
    static const uint8_t zero[4];
    if (memcmp(g_base + RVA_RETRAIT_MEMBRE, RETRAIT_MEMBRE_ORIG, sizeof RETRAIT_MEMBRE_ORIG) || !EnvoiSalle(salon, qui, zero, sizeof zero, 3)) {
        Note("retrait impossible", 0, 0, 0); return;
    }
    ((RetraitMembre_t)(g_base + RVA_RETRAIT_MEMBRE))(ObjetSalon(salon), qui);
}
static int Barres_DuPing(int ms)
{
    if (ms <= 0) return -1;
    const int delai = (int)(((int64_t)ms * 2997 + 100000 + 99999) / 100000);
    return delai <= 3 ? 5 : delai == 4 ? 4 : delai == 5 ? 3 : delai == 6 ? 2 : 1;
}
static int EtatPlace(SteamID id)
{
    uint8_t *salle = Salle();
    if (!salle || !id || !Lisible(salle + 0x58, 0x14)) return -1;
    const int n = *(int *)(salle + 0x58); uint8_t *tab = *(uint8_t **)(salle + 0x68);
    if (!tab || n <= 0 || n > 64 || !Lisible(tab, n * 0x20)) return -1;
    for (int i = 0; i < n; i++) if (*(SteamID *)(tab + i * 0x20 + 8) == id) return *(int *)(tab + i * 0x20) >= 0 ? 1 : 0;
    return -1;
}

#define RVA_APPEL_RECEPTION 0x8C3DA1
#define RVA_RECEPTION       0x8C0870
typedef uint8_t (__cdecl *Reception_t)(void *ctx, uint8_t *buf, int *taille, int *etat, int *type, uint64_t *exp);
static Reception_t o_reception;
#define TAILLE_MSG 0x5DC
#define MAX_PORTE 8
typedef struct {
    SteamID id; DWORD t0, dernierEnvoi; int numero, envois, recus, ping[8], refuser;
    int garde, gType, gEtat, gTaille; uint8_t gBuf[TAILLE_MSG];
} Porte;
static Porte g_porte[MAX_PORTE];
static struct { int pret, type, etat, taille; SteamID de; uint8_t buf[TAILLE_MSG]; } g_rendre[MAX_PORTE];
static int g_numero;
static Porte *PorteDe(SteamID id) { for (int i = 0; i < MAX_PORTE; i++) if (g_porte[i].id == id) return &g_porte[i]; return NULL; }
static uint8_t __cdecl Reception(void *ctx, uint8_t *buf, int *taille, int *etat, int *type, uint64_t *exp)
{
    if (buf && taille && etat && type && exp)
        for (int i = 0; i < MAX_PORTE; i++) if (g_rendre[i].pret && *taille >= g_rendre[i].taille) {
            memcpy(buf, g_rendre[i].buf, g_rendre[i].taille); *taille = g_rendre[i].taille; *etat = g_rendre[i].etat;
            *type = g_rendre[i].type; *exp = g_rendre[i].de; g_rendre[i].pret = 0; g_rendre[i].de = 0;
            return 1;
        }
    const uint8_t r = o_reception(ctx, buf, taille, etat, type, exp);
    if (!r || !type || !exp || !taille || !etat) return r;
    Porte *g = PorteDe(*exp);
    if (g && *type == 5 && *taille >= 4) {
        const int ms = (int)(timeGetTime() - *(uint32_t *)buf);
        if (ms > 0 && ms < 10000 && g->recus < 8) g->ping[g->recus++] = ms;
        return r;
    }
    const int t = *type;
    if (t == 1 || t == 3 || t == 4 || t == 5) return r;
    const SteamID salon = SalonCourant();
    const int bloque = g_nbBloques && Bloque(*exp);
    if ((!g_minBarres && !bloque) || !SuisHote(salon) || EtatPlace(*exp) != 0) return r;
    if (!g) {
        for (int i = 0; i < MAX_PORTE && !g; i++) if (!g_porte[i].id) g = &g_porte[i];
        if (!g) return r;
        memset(g, 0, sizeof *g);
        g->id = *exp; g->t0 = GetTickCount(); g->numero = ++g_numero; g->refuser = bloque;
        Note("porte joueur/bloque", g->numero, bloque, 0);
    }
    if (!g->garde && *taille <= TAILLE_MSG) {
        memcpy(g->gBuf, buf, *taille); g->gTaille = *taille; g->gEtat = *etat; g->gType = t; g->garde = 1;
    }
    return 0;
}
static void Decider(Porte *g, SteamID salon, int accepter)
{
    if (accepter) {
        if (g->garde)
            for (int i = 0; i < MAX_PORTE; i++) if (!g_rendre[i].pret) {
                memcpy(g_rendre[i].buf, g->gBuf, g->gTaille); g_rendre[i].taille = g->gTaille; g_rendre[i].etat = g->gEtat;
                g_rendre[i].type = g->gType; g_rendre[i].de = g->id; g_rendre[i].pret = 1;
                break;
            }
        Note("porte accepte joueur", g->numero, 0, 0);
    } else {
        static const uint8_t zero[4];
        EnvoiSalle(salon, g->id, zero, sizeof zero, 3);
        Bloquer(salon, g->id);
        g_sursis = g->id; g_sursisFin = GetTickCount() + 2000;
        Note("porte refuse joueur", g->numero, 0, 0);
    }
    memset(g, 0, sizeof *g);
}
static void Porte_Tic(SteamID salon)
{
    const DWORD maintenant = GetTickCount();
    for (int k = 0; k < MAX_PORTE; k++) {
        Porte *g = &g_porte[k];
        if (!g->id) continue;
        if (!SuisHote(salon) || EtatPlace(g->id) < 0) { Note("porte parti joueur", g->numero, 0, 0); memset(g, 0, sizeof *g); continue; }
        if (g->refuser) { Decider(g, salon, 0); continue; }
        if (g->recus >= 4 || (g->recus >= 2 && maintenant - g->t0 >= 3000) || maintenant - g->t0 >= 5000) {
            int meilleur = 0;
            for (int i = 0; i < g->recus; i++) if (!meilleur || g->ping[i] < meilleur) meilleur = g->ping[i];
            Decider(g, salon, !meilleur || Barres_DuPing(meilleur) >= g_minBarres);
            continue;
        }
        if (maintenant - g->dernierEnvoi < 250) continue;
        const uint32_t t = timeGetTime();
        if (EnvoiSalle(salon, g->id, &t, sizeof t, 4)) { g->dernierEnvoi = maintenant; g->envois++; }
    }
}

#define RVA_PLATEFORME  0x20924B0
#define RVA_TABLE_PLAT  0x0F10D30
#define RVA_BOUCHON     0x08C2050
#define RVA_RETOUR_EJ   0x08BFC15
typedef int (__cdecl *Ejecter_t)(void *salle, int id);
static Ejecter_t o_ejecter;
static volatile uint8_t *g_sessionEj; static int g_imagesEj;
static SteamID g_aRetirer; static int g_attenteRetrait;
static SteamID IdDuMembre(uint8_t *salle, int id)
{
    uint8_t *ctx = salle + 0x58;
    if (!Lisible(ctx, 0x14)) return 0;
    const int cap = *(int *)ctx; uint8_t *tab = *(uint8_t **)(ctx + 0x10);
    if (!tab || cap <= 0 || cap > 64 || !Lisible(tab, cap * 0x20)) return 0;
    for (int i = 0; i < cap; i++) if (*(int *)(tab + i * 0x20) == id) return *(SteamID *)(tab + i * 0x20 + 8);
    return 0;
}
__attribute__((used)) int __cdecl cf_ejecter(void *salle, int id, uint32_t retour, uint8_t *ebpAppelant)
{
    if (retour != (uint32_t)(uintptr_t)(g_base + RVA_RETOUR_EJ) || !salle) return o_ejecter(salle, id);
    const SteamID salon = SalonCourant(), qui = IdDuMembre((uint8_t *)salle, id);
    if (!qui || !SuisHote(salon)) { Note("chasser refuse membre", id, 0, 0); return o_ejecter(salle, id); }
    Note("chasser membre", id, 0, 0);
    Bloquer(salon, qui);
    g_sursis = qui; g_sursisFin = GetTickCount() + 2000;
    g_sessionEj = *(uint8_t **)(ebpAppelant + 8) - 0x38; g_imagesEj = 0;
    g_aRetirer = qui; g_attenteRetrait = 2;
    return 0;
}
__asm__(".text\n_cf_ejecterThunk:\n"
        "  push %ebp\n"
        "  push 4(%esp)\n"
        "  push 16(%esp)\n"
        "  push 16(%esp)\n"
        "  call _cf_ejecter\n"
        "  add $16, %esp\n"
        "  ret\n");
void cf_ejecterThunk(void);
static void Chasser_Tic(SteamID salon)
{
    static int pose;
    if (!pose) {
        uint8_t *plat = Lisible(g_base + RVA_PLATEFORME, 4) ? *(uint8_t **)(g_base + RVA_PLATEFORME) : NULL;
        if (plat && Lisible(plat + 4, 4)) {
            void **tab = *(void ***)(plat + 4);
            pose = 1;
            if ((uint8_t *)tab == g_base + RVA_TABLE_PLAT && tab[0x78 / 4] == (void *)(g_base + RVA_BOUCHON)
                && Pointeur(&tab[0x78 / 4], (void *)cf_ejecterThunk, (void **)&o_ejecter)) Note("chasser pose", 1, 0, 0);
            else Note("chasser pose", 0, 0, 0);
        }
    }
    volatile uint8_t *s = g_sessionEj;
    if (s) {
        if ((*(volatile uint32_t *)(s + 8) & 0x20) && s[0xC]) { s[0xC] = 0; g_sessionEj = NULL; }
        else if (++g_imagesEj > 120) g_sessionEj = NULL;
    }
    if (g_aRetirer && g_attenteRetrait-- <= 0) {
        const SteamID qui = g_aRetirer; g_aRetirer = 0;
        if (SuisHote(salon)) RetirerMembre(salon, qui);
    }
}

typedef struct CB { void **vt; uint8_t flags; uint8_t pad[3]; int id; int taille; } CB;
static void SurRappel(int id, void *pv)
{
    const uint8_t *d = (const uint8_t *)pv;
    if (id == 513 && *(const int *)d == 1) {
        SteamID salon; memcpy(&salon, d + 8, 8);
        g_minBarres = Barres(g_choix);
        if (g_mm) { char v[4]; v[0] = (char)('0' + g_minBarres); v[1] = 0; ((DonneeSalon_t)(*(void ***)g_mm)[20])(g_mm, salon, "dlr_min_bars", v); }
        Note("salon_cree barres", g_minBarres, 0, 0);
    } else if (id == 1202) {
        SteamID qui; memcpy(&qui, d, 8);
        if (g_nbBloques && Bloque(qui) && qui != g_sursis) FermerP2P(qui);
    }
}
static void __fastcall RappelRes(CB *self, void *edx, void *pv, int io, uint64_t appel) { (void)edx; (void)io; (void)appel; SurRappel(self->id, pv); }
static void __fastcall Rappel(CB *self, void *edx, void *pv) { (void)edx; SurRappel(self->id, pv); }
static int __fastcall TailleRappel(CB *self, void *edx) { (void)edx; return self->taille; }
static void *g_vtRappel[3] = {(void *)RappelRes, (void *)Rappel, (void *)TailleRappel};
static CB g_rappels[2];
typedef void (__cdecl *Inscrire_t)(CB *cb, int id);

static volatile LONG g_pret;
static void ConnFilter_Tic(void)
{
    if (!g_pret) return;
    const SteamID salon = SalonCourant();
    if (g_salonBloques != salon) ViderBloques();
    if (!salon) { memset(g_porte, 0, sizeof g_porte); return; }
    Chasser_Tic(salon);
    Porte_Tic(salon);
    if (g_sursis && (LONG)(GetTickCount() - g_sursisFin) >= 0) { FermerP2P(g_sursis); g_sursis = 0; }
    static DWORD dernier; const DWORD t = GetTickCount();
    if (t - dernier >= 500) { dernier = t; Panneau(salon); }
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    static const uint8_t KTOL[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
    int t;
    for (t = 0; t < 600 && memcmp(g_base + 0x846010, KTOL, sizeof KTOL); t++) Sleep(100);
    if (t == 600) { Note("refus_version_game_exe", 0, 0, 0); return 0; }
    if (GetModuleHandleA("DOA5LR-ConnFilter.asi")) { Note("module_separe_present inactif", 0, 0, 0); return 0; }
    const int menu = PoserMenu();
    HMODULE sa = NULL;
    for (t = 0; t < 1200 && !(sa = GetModuleHandleA("steam_api.dll")); t++) Sleep(100);
    if (!sa) { Note("steam_api_absent", menu, 0, 0); return 0; }
    typedef void *(*Acces_t)(void);
    Acces_t aMM = (Acces_t)GetProcAddress(sa, "SteamMatchmaking"), aNet = (Acces_t)GetProcAddress(sa, "SteamNetworking"),
            aU = (Acces_t)GetProcAddress(sa, "SteamUser");
    for (t = 0; t < 1200 && !(g_mm && g_net && g_user); t++) {
        if (aMM && !g_mm) g_mm = aMM();
        if (aNet && !g_net) g_net = aNet();
        if (aU && !g_user) g_user = aU();
        if (!(g_mm && g_net && g_user)) Sleep(250);
    }
    if (!g_mm || !g_net || !g_user) { Note("interfaces_steam_absentes", menu, 0, 0); return 0; }
    Inscrire_t inscrire = (Inscrire_t)GetProcAddress(sa, "SteamAPI_RegisterCallback");
    static const int IDS[2][2] = {{513, 16}, {1202, 8}};
    if (inscrire) for (int i = 0; i < 2; i++) { g_rappels[i].vt = g_vtRappel; g_rappels[i].id = IDS[i][0]; g_rappels[i].taille = IDS[i][1]; inscrire(&g_rappels[i], IDS[i][0]); }
    void **nvt = *(void ***)g_net;
    const int p2p = Pointeur(&nvt[0], (void *)EnvoiP2P, (void **)&o_envoiP2P) && Pointeur(&nvt[2], (void *)LectureP2P, (void **)&o_lectureP2P);
    uint8_t *c = g_base + RVA_APPEL_RECEPTION; int porte = 0;
    if (AppelVers(c, RVA_RECEPTION)) {
        o_reception = (Reception_t)(g_base + RVA_RECEPTION);
        const int32_t rel = (int32_t)((uint8_t *)Reception - (c + 5));
        porte = Ecrire(c + 1, &rel, 4);
    }
    InterlockedExchange(&g_pret, 1);
    Note("pose menu/p2p/porte", menu, p2p, porte);
    return 0;
}

void ConnFilter_Demarrer(void)
{
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    AjouterTic(ConnFilter_Tic);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

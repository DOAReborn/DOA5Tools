#define _CRT_RAND_S
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "commun.h"
#include "accroche.h"

#define RVA_APPEL_POOL     0x400E76
#define RVA_POOL           0x428EC0
#define RVA_APPEL_DISPO    0x400F3D
#define RVA_DISPO          0x41AC60
#define RVA_APPEL_SURVIVAL 0x40BA0E
#define RVA_RESOUDRE       0x401A50
#define RVA_LIAISON        0x42B020
#define RVA_RESSOURCES     0xDDACF0
#define RVA_POOLS          0xDD9B88
#define RVA_LIAISONS       0xD05268
#define RVA_MODE           0xF8A930
#define RVA_PARCOURS       0xF8A8B8
#define RVA_FICHE_SURVIVAL 0xCD251C

typedef struct { uint32_t id; uint32_t *debut, *fin, *cap; uint32_t extra; } Pool;
typedef struct { Pool pool; uint32_t cles[133]; } Copie;
static Copie g_copies[512];
static unsigned g_nbCopies;
static SRWLOCK g_verrou = SRWLOCK_INIT;
static uint8_t *g_base;
static int g_arcade = 1, g_timeAttack = 1, g_survival = 1;
static _Thread_local unsigned g_autoriser;
static const uint32_t CLES_CONSOLE[5] = {20, 25, 30, 45, 46};
static void Note(const char *libelle, long a, long b, long c) { Journal("ArcadeConsole", libelle, a, b, c); }

static int Lire(const void *p, void *dst, size_t n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    if ((const uint8_t *)p + n > (const uint8_t *)m.BaseAddress + m.RegionSize) return 0;
    memcpy(dst, p, n);
    return 1;
}
static int Solo(unsigned id) { return (g_arcade && id >= 1 && id <= 8) || (g_timeAttack && id >= 17 && id <= 24); }

static unsigned Cles(const Pool *p, unsigned masque, uint32_t *cles, unsigned *natifs)
{
    *natifs = 0;
    const uintptr_t d = (uintptr_t)p->debut, f = (uintptr_t)p->fin;
    if (f < d || f > (uintptr_t)p->cap || (d & 3) || ((f - d) & 3) || f - d > 128 * 4) return 0;
    const unsigned total = (unsigned)((f - d) / 4);
    unsigned n = 0;
    if (total && !Lire(p->debut, cles, total * 4)) return 0;
    while (n < total && cles[n]) n++;
    *natifs = n;
    for (unsigned i = 0; i < 5; i++) if (masque & (1u << i)) {
        int deja = 0;
        for (unsigned j = 0; j < n; j++) deja |= cles[j] == CLES_CONSOLE[i];
        if (!deja) cles[n++] = CLES_CONSOLE[i];
    }
    return n;
}
static Pool *Etendre(Pool *source, unsigned id, unsigned masque)
{
    Pool p; uint32_t cles[133] = {0};
    if (!source || !Solo(id) || !masque || !Lire(source, &p, sizeof p) || p.id != id) return source;
    unsigned natifs;
    const unsigned n = Cles(&p, masque, cles, &natifs);
    if (n == natifs) return source;
    Pool *r = source;
    AcquireSRWLockExclusive(&g_verrou);
    for (unsigned i = 0; i < g_nbCopies; i++) {
        Copie *c = &g_copies[i];
        if (c->pool.id == id && c->pool.extra == p.extra && (unsigned)(c->pool.fin - c->pool.debut) == n && !memcmp(c->cles, cles, n * 4)) { r = &c->pool; break; }
    }
    if (r == source && g_nbCopies < 512) {
        Copie *c = &g_copies[g_nbCopies++];
        memcpy(c->cles, cles, n * 4);
        c->pool.id = p.id; c->pool.debut = c->cles; c->pool.fin = c->pool.cap = c->cles + n; c->pool.extra = p.extra;
        r = &c->pool;
    }
    ReleaseSRWLockExclusive(&g_verrou);
    return r;
}

static int RessourcePrete(unsigned id, unsigned type)
{
    uint32_t ligne[4]; char lien[8];
    return Lire(g_base + RVA_RESSOURCES + id * 16, ligne, sizeof ligne) && ligne[1] == type && ligne[3] == 0xFFFFFFFF
        && Lire((void *)(uintptr_t)ligne[2], lien, sizeof lien) && !memcmp(lien, "NO_LINK", 8);
}
typedef int (__cdecl *HotePret_t)(int hote);
typedef int (__cdecl *EtatExtra_t)(void);
static unsigned Installes(void)
{
    unsigned m = 0;
    HMODULE maps = GetModuleHandleA("DOA5LR-Maps.asi");
    HotePret_t pret = maps ? (HotePret_t)GetProcAddress(maps, "ExtraStagesHostReady") : NULL;
    EtatExtra_t etat = maps ? (EtatExtra_t)GetProcAddress(maps, "ExtraStagesState") : NULL;
    if (pret) { if (pret(18) == 1) m |= 1; if (pret(23) == 1 && pret(32) == 1) m |= 2; if (pret(28) == 1) m |= 4; }
    if (etat && etat() == 1) {
        if (RessourcePrete(22738, 43)) m |= 8;
        if (RessourcePrete(22710, 27) && RessourcePrete(22724, 27)) m |= 16;
    }
    return m;
}
static int ForcerDispo(unsigned map, unsigned masque) { return (map == 43 && (masque & 8)) || (map == 44 && (masque & 16)); }

typedef Pool *(__thiscall *PoolFn_t)(void *mgr, unsigned id);
static void *volatile g_poolSuite;
static volatile LONG g_notes;
static Pool *__fastcall Parcours(void *mgr, void *edx, unsigned id)
{
    (void)edx;
    g_autoriser = 0;
    Pool *p = ((PoolFn_t)g_poolSuite)(mgr, id);
    if (!Solo(id)) return p;
    const unsigned masque = Installes();
    Pool *r = Etendre(p, id, masque);
    if (r != p) g_autoriser = masque & 24;
    if (InterlockedIncrement(&g_notes) <= 100) Note("parcours id/masque/etendu", (long)id, (long)masque, r != p);
    return r;
}
typedef uint8_t (__thiscall *Dispo_t)(void *mgr, unsigned map);
static void *volatile g_dispoSuite;
static uint8_t __fastcall Disponible(void *mgr, void *edx, unsigned map)
{
    (void)edx;
    const uint8_t natif = ((Dispo_t)g_dispoSuite)(mgr, map);
    return natif || ForcerDispo(map, g_autoriser);
}

typedef unsigned (__thiscall *Resoudre_t)(void *mgr, unsigned cle);
typedef void *(__thiscall *Liaison_t)(void *liaisons, unsigned cle);
static unsigned TirageSurvival(unsigned masque, unsigned origine)
{
    void *pools = NULL, *liaisons = NULL;
    if (!Lire(g_base + RVA_POOLS, &pools, 4) || !pools || !Lire(g_base + RVA_LIAISONS, &liaisons, 4) || !liaisons) return origine;
    Pool p; uint32_t cles[133];
    Pool *src = ((PoolFn_t)(g_base + RVA_POOL))(pools, 1);
    if (!src || !Lire(src, &p, sizeof p)) return origine;
    unsigned natifs;
    const unsigned n = Cles(&p, masque, cles, &natifs);
    unsigned gardes = 0;
    for (unsigned i = 0; i < n; i++) {
        uint8_t *e = ((Liaison_t)(g_base + RVA_LIAISON))(liaisons, cles[i]); uint8_t map = 0;
        if (!e || !Lire(e + 4, &map, 1) || map >= 74) continue;
        if (((Dispo_t)(g_base + RVA_DISPO))(NULL, map) || ForcerDispo(map, masque & 24)) cles[gardes++] = cles[i];
    }
    unsigned alea = 0;
    if (!gardes || rand_s(&alea)) return origine;
    return cles[alea % gardes];
}
static void *volatile g_survivalSuite;
static unsigned __fastcall Survival(void *mgr, void *edx, unsigned cle)
{
    (void)edx;
    const unsigned origine = ((Resoudre_t)g_survivalSuite)(mgr, cle);
    unsigned mode = 0, parcours = ~0u;
    if (cle != 32 || origine != 32 || !Lire(g_base + RVA_MODE, &mode, 4) || mode != 4
        || !Lire(g_base + RVA_PARCOURS, &parcours, 4) || parcours >= 8) return origine;

    uint32_t ptr = 0, debut = 0, fin = 0, fiche[3];
    if (!Lire(g_base + RVA_FICHE_SURVIVAL, &ptr, 4) || !ptr || !Lire((uint8_t *)(uintptr_t)ptr + 0x4C, &debut, 4)
        || !Lire((uint8_t *)(uintptr_t)ptr + 0x50, &fin, 4) || fin < debut || fin - debut != 16 * 12
        || !Lire((void *)(uintptr_t)(debut + parcours * 12), fiche, sizeof fiche) || fiche[0] != parcours + 1 || fiche[1] != 150 + parcours)
        return origine;
    const unsigned masque = Installes(), choix = TirageSurvival(masque, origine);
    Note("survival parcours/masque/cle", (long)parcours, (long)masque, (long)choix);
    return choix;
}

static int Signature(const uint8_t *p, const uint8_t *attendu, size_t n) { return !memcmp(p, attendu, n); }
static DWORD WINAPI Installer(LPVOID arg)
{
    (void)arg;
    static const uint8_t SUITE_POOL[] = {0x8B, 0xD8, 0x8B, 0x4B, 0x04, 0x8B, 0x43, 0x08};
    static const uint8_t TETE_POOL[] = {0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x6C, 0x8B, 0x49, 0x70};
    static const uint8_t TETE_DISPO[] = {0x55, 0x8B, 0xEC, 0x68, 0x80, 0, 0, 0};
    static const uint8_t TETE_RESOUDRE[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
    static const uint8_t TETE_LIAISON[] = {0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x4C, 0x8B, 0x49, 0x50};
    static const uint8_t SUITE_SURVIVAL[] = {0x50, 0xB9};
    int t;
    for (t = 0; t < 600 && !Signature(g_base + RVA_POOL, TETE_POOL, sizeof TETE_POOL); t++) Sleep(100);
    if (GetModuleHandleA("DOA5LR-ArcadeConsole.asi")) { Note("module_separe_present inactif", 0, 0, 0); return 0; }
    if (t == 600 || !Signature(g_base + RVA_APPEL_POOL + 5, SUITE_POOL, sizeof SUITE_POOL)
        || !Signature(g_base + RVA_DISPO, TETE_DISPO, sizeof TETE_DISPO)) { Note("refus_version_game_exe", 0, 0, 0); return 0; }
    const int parcours = ac_call(g_base + RVA_APPEL_POOL, g_base + RVA_POOL, (void *)Parcours, &g_poolSuite) != NULL;
    const int dispo = parcours && ac_call(g_base + RVA_APPEL_DISPO, g_base + RVA_DISPO, (void *)Disponible, &g_dispoSuite) != NULL;
    int survival = 0;
    if (g_survival && Signature(g_base + RVA_APPEL_SURVIVAL + 5, SUITE_SURVIVAL, 2)
        && Signature(g_base + RVA_RESOUDRE, TETE_RESOUDRE, sizeof TETE_RESOUDRE) && Signature(g_base + RVA_LIAISON, TETE_LIAISON, sizeof TETE_LIAISON))
        survival = ac_call(g_base + RVA_APPEL_SURVIVAL, g_base + RVA_RESOUDRE, (void *)Survival, &g_survivalSuite) != NULL;
    Note("pose parcours/dispo/survival", parcours, dispo, survival);
    return 0;
}

void ArcadeConsole_Demarrer(void)
{
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    g_arcade = Reglage("ArcadeConsole", "Arcade", 1);
    g_timeAttack = Reglage("ArcadeConsole", "TimeAttack", 1);
    g_survival = Reglage("ArcadeConsole", "Survival", 1);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

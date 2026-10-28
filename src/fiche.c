#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define RVA_NCP_VTABLE      0xBE22E4
#define RVA_NCP_SERIALIZE   0x515540
#define NCP_MARQUE          0x116
#define NCP_TYPE_CONNEXION  0x117
#define NCP_MARQUE2         0xEF
#define RVA_FICHES          0xF82950
#define RVA_FICHES_PRET     0xF83BF4
#define FICHES_TABLE        0xD8
#define FICHE_TAILLE        0x10C
#define FICHE_ID            4
#define FICHE_MARQUE        0x10A
#define FICHE_MARQUE2       0xE3
#define RVA_SESSION_OBTENIR 0x0C8B90
#define SESSION_MEMBRES     0xD4
#define MEMBRE_TAILLE       0x1C
#define RVA_ID_LOCAL        0x5048C0
#define NB_PLACES           16

#define MARQUE_BASE         0xA0
#define MARQUE_MASQUE       0xF0
#define MARQUE_PROTOCOLE2   0x08
#define MARQUE_NIVEAUX      0x07
#define MARQUE2_BASE        0xC0
#define MARQUE2_MASQUE      0xE0
#define MARQUE2_NIVEAUX     0x03
#define MARQUE2_CORRECTIFS  0x04

static int g_correctifs;
void Fiche_Correctifs(int actifs) { g_correctifs = actifs != 0; }

static uint8_t *base;
static void Note(const char *libelle, long a, long b, long c) { Journal("Fiche", libelle, a, b, c); }

static int Lisible(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const uint8_t *)p + n <= (const uint8_t *)m.BaseAddress + m.RegionSize;
}

static uint8_t Marque_Octet(int packs)
{
    const uint8_t proto2 = (packs >> 3) ? 0 : MARQUE_PROTOCOLE2;
    return (uint8_t)(MARQUE_BASE | proto2 | (packs & MARQUE_NIVEAUX));
}
typedef uint8_t (__thiscall *Serialiser_t)(void *paquet, void *flux);
static Serialiser_t o_serialiser;
static volatile LONG g_envois;
static uint8_t __fastcall hk_serialiser(void *paquet, void *edx, void *flux)
{
    (void)edx;
    uint8_t *p = (uint8_t *)paquet;
    const uint8_t origine = p[NCP_MARQUE], origine2 = p[NCP_MARQUE2];
    const int packs = StageSlots_Packs();
    p[NCP_MARQUE] = Marque_Octet(packs);
    p[NCP_MARQUE2] = (uint8_t)(MARQUE2_BASE | ((packs >> 3) & MARQUE2_NIVEAUX) | (g_correctifs ? MARQUE2_CORRECTIFS : 0));
    const uint8_t type = WiFiWired_Type();
    if (type) p[NCP_TYPE_CONNEXION] = type;
    if (InterlockedIncrement(&g_envois) <= 3) {
        Note("envoi origine/marque/type", origine, p[NCP_MARQUE], type);
        Note("envoi origine2_non_nul/marque2", origine2 != 0, p[NCP_MARQUE2], 0);
    }
    return o_serialiser(paquet, flux);
}

static const uint8_t *Fiche(int id)
{
    if (!base || !(base[RVA_FICHES_PRET] & 1) || id < 0 || id >= NB_PLACES) return NULL;
    const uint8_t *t = base + RVA_FICHES + FICHES_TABLE;
    if (!Lisible(t, NB_PLACES * FICHE_TAILLE)) return NULL;
    for (int i = 0; i < NB_PLACES; i++) {
        const uint8_t *f = t + i * FICHE_TAILLE;
        int32_t fid; memcpy(&fid, f + FICHE_ID, 4);
        if (fid == id) return f;
    }
    return NULL;
}

int Marque_DuMembre(int id)
{
    const uint8_t *f = Fiche(id);
    if (!f) return -1;
    const uint8_t m = f[FICHE_MARQUE];
    return (m & MARQUE_MASQUE) == MARQUE_BASE ? m : 0;
}

static int Niveaux_DuMembre(int id)
{
    const int m = Marque_DuMembre(id);
    if (m <= 0) return -1;
    const uint8_t m2 = Fiche(id)[FICHE_MARQUE2];
    if ((m2 & MARQUE2_MASQUE) == MARQUE2_BASE) return (m & MARQUE_NIVEAUX) | ((m2 & MARQUE2_NIVEAUX) << 3);
    return (m & MARQUE_PROTOCOLE2) ? (m & MARQUE_NIVEAUX) : -1;
}

static int Correctifs_DuMembre(int id)
{
    if (Marque_DuMembre(id) <= 0) return 0;
    const uint8_t m2 = Fiche(id)[FICHE_MARQUE2];
    return (m2 & MARQUE2_MASQUE) == MARQUE2_BASE && (m2 & MARQUE2_CORRECTIFS);
}

typedef void *(__cdecl *SessionObtenir_t)(void);
typedef int (__cdecl *IdLocal_t)(void);

static int CompterMembres(int niveaux, int *marques, int correctifs)
{
    *marques = 0;
    const uint8_t *s = (const uint8_t *)((SessionObtenir_t)(base + RVA_SESSION_OBTENIR))();
    if (!s || !Lisible(s + SESSION_MEMBRES, NB_PLACES * MEMBRE_TAILLE)) return -1;
    const int moi = ((IdLocal_t)(base + RVA_ID_LOCAL))();
    int n = 0;
    for (int i = 0; i < NB_PLACES; i++) {
        int32_t id; memcpy(&id, s + SESSION_MEMBRES + i * MEMBRE_TAILLE, 4);
        if (id < 0 || id >= NB_PLACES || id == moi) continue;
        n++;
        if (correctifs ? Correctifs_DuMembre(id) : niveaux ? Niveaux_DuMembre(id) == niveaux : Marque_DuMembre(id) > 0) (*marques)++;
    }
    return n;
}

int Marque_TousNiveaux(void)
{
    const int niveaux = base ? StageSlots_Packs() : 0;
    if (!niveaux) return 0;
    int marques; const int n = CompterMembres(niveaux, &marques, 0);
    static int dernier = -2, dernierN = -2, dernierM = -2;
    const int ok = n > 0 && marques == n;
    if (ok != dernier || n != dernierN || marques != dernierM) {
        Note("niveaux tous/membres/marques", ok, n, marques);
        dernier = ok; dernierN = n; dernierM = marques;
    }
    return ok;
}

int Marque_TousDOA5Tools(void)
{
    if (!base) return 0;
    int marques; const int n = CompterMembres(0, &marques, 0);
    return n > 0 && marques == n;
}

int Marque_TousCorrectifs(void)
{
    if (!base || !g_correctifs) return 0;
    int marques; const int n = CompterMembres(0, &marques, 1);
    Note("correctifs tous/membres/marques", n > 0 && marques == n, n, marques);
    return n > 0 && marques == n;
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    void **slot = (void **)(base + RVA_NCP_VTABLE) + 2;
    for (int t = 0; t < 600 && *slot != (void *)(base + RVA_NCP_SERIALIZE); t++) Sleep(100);
    if (*slot != (void *)(base + RVA_NCP_SERIALIZE)) { Note("refus_version_game_exe", 0, 0, 0); return 0; }
    DWORD ancien;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &ancien)) return 0;
    o_serialiser = (Serialiser_t)*slot;
    InterlockedExchange((volatile LONG *)slot, (LONG)(uintptr_t)hk_serialiser);
    VirtualProtect(slot, sizeof(void *), ancien, &ancien);
    Note("pose", Marque_Octet(StageSlots_Packs()), 0, 0);
    return 0;
}

void Fiche_Demarrer(void)
{
    base = (uint8_t *)GetModuleHandleA(NULL);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

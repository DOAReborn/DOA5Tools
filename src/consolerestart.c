#include <windows.h>
#include <xmmintrin.h>
#include <float.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"
#include "accroche.h"

#define VA_REPRISE       0x99E460
#define VA_STAGE         0x9964E0
#define VA_ALEA          0x7F72D0
#define VA_NOM_ALEA      0xFE7F90
#define VA_TABLE         0x12B7EA8
#define VA_STAGE_MGR     0x2472580
#define VA_INDEX_IFACE   0x138E638
#define VA_EN_LIGNE      0x1381820
#define VA_EN_LIGNE_PRET 0x1382884

typedef struct { DWORD x, z; int angle, zone; } Point;
static const Point FIREWORKS[] = {
    {0x40B66666, 0xC1300000, -9102, -1}, {0xC0D40000, 0xC12EE560, 24211, -1}, {0xC174CCCD, 0xC19D999A, 18204, -1}};
static const Point AZUCHI1[] = {
    {0x00000000, 0x00000000, 32768, -1}, {0xC0B00000, 0x40B00000, 24576, -1}, {0x40B00000, 0x40A00000, 40960, -1}};
static const Point AZUCHI2[] = {
    {0x00000000, 0x40000000, 32768, 1}, {0xC0D33333, 0x40533333, 24576, 1}, {0x40933333, 0x40533333, 24576, 1},
    {0x3E99999A, 0x41700000, 32768, 2}, {0x41280000, 0x41700000, 32768, 2}, {0xC1200000, 0x41700000, 32768, 2}};
static const Point TITAN[] = {
    {0x00000000, 0x00000000, 0, -1}, {0xC1180000, 0x3E99999A, -16384, -1}, {0x40B00000, 0x40200000, 0, -1},
    {0x3F000000, 0xC0200000, 32768, -1}, {0x4134CCCD, 0xC02CCCCD, -8192, -1}};
typedef struct { DWORD hote; const Point *points; unsigned nombre; int plusProche; } Stage;
static const Stage STAGES[] = {
    {18, FIREWORKS, 3, 0}, {23, AZUCHI1, 3, 0}, {32, AZUCHI2, 6, 0}, {28, TITAN, 5, 1}};

static uint8_t *g_base;
static uint8_t *At(DWORD va) { return g_base + va - 0x400000; }
static void Note(const char *libelle, long a, long b, long c) { Journal("ConsoleRestart", libelle, a, b, c); }
static float F(DWORD bits) { float f; memcpy(&f, &bits, 4); return f; }

__attribute__((target("sse"))) static float DistancePS4(float x, float z)
{
    __m128 zero = _mm_setzero_ps(), demi = _mm_set_ss(.5f), trois = _mm_set_ss(3.f);
    __m128 vx = _mm_set_ss(x), vz = _mm_set_ss(z);
    __m128 d = _mm_add_ss(_mm_mul_ss(vz, vz), _mm_add_ss(_mm_mul_ss(vx, vx), zero));
    __m128 masque = _mm_cmplt_ss(_mm_set_ss(FLT_MIN), d);
    __m128 b = _mm_or_ps(_mm_and_ps(d, masque), _mm_andnot_ps(masque, _mm_set_ss(1.f)));
    __m128 r = _mm_rsqrt_ss(b);
    __m128 r1 = _mm_mul_ss(_mm_mul_ss(r, demi), _mm_sub_ss(trois, _mm_mul_ss(r, _mm_mul_ss(r, b))));
    __m128 r2 = _mm_mul_ss(b, _mm_mul_ss(_mm_mul_ss(r1, demi), _mm_sub_ss(trois, _mm_mul_ss(r1, _mm_mul_ss(b, r1)))));
    __m128 special = _mm_or_ps(_mm_cmpeq_ss(b, zero), _mm_cmpeq_ss(r, zero));
    return _mm_cvtss_f32(_mm_and_ps(masque, _mm_or_ps(_mm_and_ps(special, b), _mm_andnot_ps(special, r2))));
}
static int DansZone(const Point *p, int zone) { return p->zone == zone || p->zone == -1; }

static unsigned PlusProche(const Stage *s, int zone, float x, float z)
{
    unsigned choix = 0; float meilleur = 9999.f;
    for (unsigned i = 0; i < s->nombre; i++) {
        if (!DansZone(&s->points[i], zone)) continue;
        const float d = DistancePS4(x - F(s->points[i].x), z - F(s->points[i].z));
        if (d < meilleur) { meilleur = d; choix = i; }
    }
    return choix;
}
static unsigned NombreDansZone(const Stage *s, int zone)
{
    unsigned n = 0;
    for (unsigned i = 0; i < s->nombre; i++) n += DansZone(&s->points[i], zone);
    return n;
}
static unsigned Nieme(const Stage *s, int zone, unsigned r)
{
    for (unsigned i = 0, k = 0; i < s->nombre; i++)
        if (DansZone(&s->points[i], zone)) { if (k == r) return i; k++; }
    return 0;
}

static int EnLigne(void)
{
    if (!(*(volatile uint8_t *)At(VA_EN_LIGNE_PRET) & 1)) return 0;
    const uint8_t *o = At(VA_EN_LIGNE);
    return (o[0xA64] & 8) && (*(const DWORD *)(o + 0xFA8) == 0 || o[0xF9D] == 0);
}
static int g_matchEnLigne, g_autoriseEnLigne;
static void Reprise_Tic(void) { if (!*(void *volatile *)At(VA_STAGE_MGR)) g_matchEnLigne = 0; }
static int Autorise(void)
{
    if (!EnLigne()) { g_matchEnLigne = 0; return 1; }
    if (!g_matchEnLigne) {
        g_matchEnLigne = 1;
        g_autoriseEnLigne = Marque_TousCorrectifs();
        Note("en_ligne tous_ont_le_correctif", g_autoriseEnLigne, 0, 0);
    }
    return g_autoriseEnLigne;
}

typedef int (__thiscall *Zone_t)(void *iface);
static int ZoneCourante(int *ok)
{
    *ok = 0;
    uint8_t *mgr = *(uint8_t *volatile *)At(VA_STAGE_MGR); if (!mgr) return 0;
    uint8_t *obj = *(uint8_t *volatile *)(mgr + 0xC); if (!obj) return 0;
    DWORD *premier = *(DWORD **)(obj + 0x10), *dernier = *(DWORD **)(obj + 0x14);
    const DWORD index = *(DWORD *)At(VA_INDEX_IFACE);
    if (!premier || dernier < premier || index >= (DWORD)(dernier - premier)) return 0;
    void *iface = (void *)(uintptr_t)premier[index]; if (!iface) return 0;
    *ok = 1;
    return ((Zone_t)(*(void ***)iface)[6])(iface);
}

typedef char (__cdecl *Reprise_t)(float *x, float *z, int *angle, float rayon);
typedef int (__cdecl *Alea_t)(int min, int max, const char *nom);
typedef DWORD (__cdecl *StageCourant_t)(void);
static void *volatile g_suite;
static volatile LONG g_appels;
static char __cdecl Reprise(float *x, float *z, int *angle, float rayon)
{
    const Reprise_t suite = (Reprise_t)g_suite;
    const DWORD stage = ((StageCourant_t)At(VA_STAGE))();
    const Stage *s = NULL;
    for (unsigned i = 0; i < sizeof STAGES / sizeof *STAGES; i++) if (STAGES[i].hote == stage) s = &STAGES[i];
    if (!s) return suite(x, z, angle, rayon);
    const DWORD *ligne = (const DWORD *)At(VA_TABLE) + stage * 2;
    if (ligne[0] && (int)ligne[1] > 0) return suite(x, z, angle, rayon);
    if (!Autorise()) return suite(x, z, angle, rayon);
    int ok; int zone = ZoneCourante(&ok);
    if (!ok) zone = (int)0x80000000;
    unsigned choix;
    if (s->plusProche) choix = PlusProche(s, zone, *x, *z);
    else {
        const unsigned n = NombreDansZone(s, zone);
        if (!n) { Note("aucun_point hote/zone", (long)stage, zone, 0); return 0; }
        const int r = ((Alea_t)At(VA_ALEA))(0, (int)n - 1, (const char *)At(VA_NOM_ALEA));
        choix = Nieme(s, zone, (unsigned)r);
    }
    const Point *p = &s->points[choix];
    *x = F(p->x); *z = F(p->z); *angle = p->angle;
    if (InterlockedIncrement(&g_appels) <= 100) Note("reprise hote/zone/point", (long)stage, zone, (long)choix);
    return 1;
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;

    uint8_t attendu[20] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56, 0xE8, 0, 0, 0, 0, 0x8B, 0x1C, 0xC5, 0, 0, 0, 0};
    uint8_t *f = At(VA_REPRISE);
    const DWORD rel = (DWORD)(uintptr_t)At(VA_STAGE) - (DWORD)(uintptr_t)(f + 13), table = (DWORD)(uintptr_t)At(VA_TABLE);
    memcpy(attendu + 9, &rel, 4); memcpy(attendu + 16, &table, 4);
    int t;
    for (t = 0; t < 600 && memcmp(f, attendu, sizeof attendu); t++) Sleep(100);
    static const uint8_t ALEA[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x56, 0x8B, 0x75, 0x0C, 0x57, 0x8B, 0x7D, 0x08};
    if (GetModuleHandleA("DOA5LR-ConsoleRestart.asi")) {
        Fiche_Correctifs(0); Note("module_separe_present inactif", 0, 0, 0); return 0;
    }
    if (t == 600 || memcmp(At(VA_ALEA), ALEA, sizeof ALEA) || strcmp((const char *)At(VA_NOM_ALEA), "Stage::get_player_restart_pos")) {
        Note("refus_version_game_exe", 0, 0, 0); return 0;
    }
    const int pose = ac_detour(f, attendu, 6, (void *)Reprise, &g_suite) != NULL;
    Note("pose", pose, 0, 0);
    return 0;
}

void ConsoleRestart_Demarrer(void)
{
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    AjouterTic(Reprise_Tic);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

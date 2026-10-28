#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"
#include "accroche.h"

#define VA_SAUT          0x7654A0
#define VA_APPEL_KO      0x80D235
#define VA_RETOUR_KO     0x80D23A
#define VA_ARRET         0x735870
#define VA_OPERANDE_B3   0x72AFF3
#define VA_TABLE_B3_PC   0xDB948C
#define VA_SCENE_ID      0x13CE5F8
#define VA_SCENE_ETAT    0x13CE5CC
#define VA_SCENE_MGR     0x13CE5E8
#define VA_VERROU        0x139CB54
#define VA_EN_LIGNE      0x1381820
#define VA_EN_LIGNE_PRET 0x1382884
#define VA_OPS_SCENE     0x110E048
#define VA_OP_DEGATS     0x72F050
#define VA_TABLE_DEGATS  0x110EAC0
#define VA_CHERCHER_DEG  0x534BC0
#define VA_ACTEURS       0x7244D0
#define VA_JOUEURS       0x13D0540
#define VA_IMAGE         0x7662E0
#define VA_STAGE         0x9964E0
#define VA_STAGE_MGR     0x2472580
#define VA_COLLISION     0xA3E280
#define VA_INDEX_COLL    0x138E63C
#define VA_REQUETE1      0x6B3730
#define VA_REQUETE2      0x6B3870
#define VA_REQUETE3      0x6B3700
#define VA_APPEL_APPLI   0x71D6B9
#define VA_APPLI         0x71B820
#define MAGIE_HOTE       0x54495441
#define HOTE_TITAN       28
#define SCENES_TITAN_1   0x4F3
#define SCENES_TITAN_N   0x516
#define BLOC_JOUEUR      0x6C8

static uint8_t *g_base;
static uint8_t *At(DWORD va) { return g_base + va - 0x400000; }
static void Note(const char *libelle, long a, long b, long c) { Journal("AoTFix", libelle, a, b, c); }

static volatile DWORD g_suite;
static int g_mur = 1, g_degats = 1;

static int Lisible(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const uint8_t *)p + n <= (const uint8_t *)m.BaseAddress + m.RegionSize;
}

static int EnLigne(void)
{
    if (!(*(volatile uint8_t *)At(VA_EN_LIGNE_PRET) & 1)) return 0;
    const uint8_t *o = At(VA_EN_LIGNE);
    return (o[0xA64] & 8) && (*(const DWORD *)(o + 0xFA8) == 0 || o[0xF9D] == 0);
}

static int g_matchEnLigne, g_autoriseEnLigne;
static int Autorise(void)
{
    if (!g_suite) return 0;
    if (!EnLigne()) { g_matchEnLigne = 0; return 1; }
    if (!g_matchEnLigne) {
        g_matchEnLigne = 1;
        g_autoriseEnLigne = Marque_TousCorrectifs();
        Note("en_ligne tous_ont_le_correctif", g_autoriseEnLigne, 0, 0);
    }
    return g_autoriseEnLigne;
}

static void AoT_Tic(void) { if (!*(void *volatile *)At(VA_STAGE_MGR)) g_matchEnLigne = 0; }
static int SuiteEnCours(void) { return *(volatile DWORD *)At(VA_SCENE_ID) == g_suite; }

static void *volatile g_sautSuite;
__attribute__((used)) int __cdecl AoT_RefuserSaut(DWORD retour)
{
    if (retour != (DWORD)(uintptr_t)At(VA_RETOUR_KO) || !g_suite || !SuiteEnCours() || *(volatile int *)At(VA_SCENE_ETAT) != 4) return 0;
    if (!Autorise()) { Note("ko_suite saut_natif_en_ligne", (long)g_suite, 0, 0); return 0; }
    if (g_degats && !EnLigne()) { Note("ko_suite saut_natif_apres_la_main", (long)g_suite, 0, 0); return 0; }
    Note("ko_suite saut_refuse", (long)g_suite, 0, 0);
    return 1;
}

__asm__(".text\n_AoT_SautThunk:\n"
        "  pushal\n"
        "  pushl 32(%esp)\n"
        "  call _AoT_RefuserSaut\n"
        "  addl $4, %esp\n"
        "  movl %eax, 28(%esp)\n"
        "  popal\n"
        "  testl %eax, %eax\n"
        "  jnz 1f\n"
        "  jmp *_g_sautSuite\n"
        "1:\n"
        "  xorl %eax, %eax\n"
        "  ret\n");
void AoT_SautThunk(void);

static void *volatile g_arretSuite;
__attribute__((used)) void __cdecl AoT_Arret(void)
{
    if (!Autorise() || !SuiteEnCours()) return;
    volatile DWORD *verrou = (volatile DWORD *)At(VA_VERROU);
    if (*verrou) { Note("suite_coupee verrou_rendu", (long)*verrou, 0, 0); *verrou = 0; }
}
__asm__(".text\n_AoT_ArretThunk:\n"
        "  pushal\n"
        "  call _AoT_Arret\n"
        "  popal\n"
        "  jmp *_g_arretSuite\n");
void AoT_ArretThunk(void);

static uint8_t *Noeud(uint8_t *obj, unsigned id)
{
    if (!Lisible(obj + 0x2C, 8)) return NULL;
    DWORD *f = *(DWORD **)(obj + 0x2C); DWORD n = *(DWORD *)(obj + 0x30);
    return f && id < n && n < 0x400 && Lisible(f + id, 4) ? (uint8_t *)(uintptr_t)f[id] : NULL;
}
static int EstHoteTitan(uint8_t *o) { return o && Lisible(o, 0x150) && *(DWORD *)(o + 0x14C) == MAGIE_HOTE; }
static uint8_t *g_hote;
static uint8_t *HoteTitan(void)
{
    if (EstHoteTitan(g_hote)) return g_hote;
    uint8_t *mgr = *(uint8_t *volatile *)At(VA_STAGE_MGR);
    if (!mgr || !Lisible(mgr + 0xC, 4)) return NULL;
    uint8_t *obj = *(uint8_t *volatile *)(mgr + 0xC);
    if (!obj || !Lisible(obj, 0x18)) return NULL;
    DWORD *premier = *(DWORD **)(obj + 0x10), *dernier = *(DWORD **)(obj + 0x14);
    const int liste = premier && dernier >= premier && dernier - premier <= 256 && Lisible(premier, (dernier - premier) * 4 + 4);

    const DWORD idx = *(volatile DWORD *)At(VA_INDEX_COLL);
    if (liste && idx < (DWORD)(dernier - premier) && premier[idx] && EstHoteTitan((uint8_t *)(uintptr_t)premier[idx] - 0x130))
        return g_hote = (uint8_t *)(uintptr_t)premier[idx] - 0x130;
    if (EstHoteTitan(obj)) return g_hote = obj;
    if (liste) for (DWORD *c = premier; c < dernier; c++) if (EstHoteTitan((uint8_t *)(uintptr_t)*c)) return g_hote = (uint8_t *)(uintptr_t)*c;
    return NULL;
}
static volatile LONG g_hoteAbsent, g_murRemis;
static int g_libres = 1;
typedef uint8_t (__cdecl *Requete_t)(DWORD joueur);
typedef DWORD (__cdecl *Stage_t)(void);
typedef void (__cdecl *Collision_t)(void *xmgr, void *noeud, int actif);
static void *volatile g_imageSuite;
__attribute__((used)) void __cdecl AoT_Image(float dt)
{
    if (!g_mur || dt == 0.f || !Autorise()) return;
    if (!*(void *volatile *)At(VA_STAGE_MGR)) return;
    if (((Stage_t)At(VA_STAGE))() != HOTE_TITAN) { g_libres = 1; return; }
    uint8_t *hote = HoteTitan();
    if (!hote) { if (InterlockedIncrement(&g_hoteAbsent) == 1) Note("mur hote_titan_absent", 0, 0, 0); return; }
    uint8_t *titan = Noeud(hote, 58), *anim = titan && Lisible(titan + 0x20, 4) ? *(uint8_t **)(titan + 0x20) : NULL;
    int libres = anim && Lisible(anim + 0xC0, 4) && *(DWORD *)(anim + 0xC0) == 0
              && *(volatile int *)At(VA_SCENE_ID) == -1;
    const Requete_t q1 = (Requete_t)At(VA_REQUETE1), q2 = (Requete_t)At(VA_REQUETE2), q3 = (Requete_t)At(VA_REQUETE3);
    for (DWORD p = 0; libres && p < 2; p++) if (q1(p) || q2(p) || q3(p)) libres = 0;
    if (libres && !g_libres && InterlockedIncrement(&g_murRemis) <= 50) Note("mur noeud66_remis", 0, 0, 0);
    g_libres = libres;
    if (!libres) return;
    uint8_t *mur = Noeud(hote, 66);
    if (mur) ((Collision_t)At(VA_COLLISION))(hote + 0x130, mur, 1);
}
__asm__(".text\n_AoT_ImageThunk:\n"
        "  pushal\n"
        "  pushl 36(%esp)\n"
        "  call _AoT_Image\n"
        "  addl $4, %esp\n"
        "  popal\n"
        "  jmp *_g_imageSuite\n");
void AoT_ImageThunk(void);

typedef DWORD (__fastcall *Op_t)(uint8_t *ctx);
typedef uint8_t *(__thiscall *ChercherDeg_t)(void *table, DWORD id);
typedef uint8_t *(__cdecl *Acteurs_t)(void);
static void *volatile g_opDegats;
static volatile LONG g_plafonds;
static DWORD __fastcall AoT_OpDegats(uint8_t *ctx)
{
    const Op_t suite = (Op_t)g_opDegats;
    if (!g_degats || !ctx || !Autorise() || !SuiteEnCours()) return suite(ctx);
    DWORD *cmd = *(DWORD **)(ctx + 0x28);
    if (!cmd || cmd[1] != 0x66) return suite(ctx);
    uint8_t *table = *(uint8_t *volatile *)At(VA_TABLE_DEGATS), *acteurs = ((Acteurs_t)At(VA_ACTEURS))();
    if (!table || !acteurs) return suite(ctx);
    const DWORD victime = *(DWORD *)(acteurs + 8);
    uint8_t *entree = ((ChercherDeg_t)At(VA_CHERCHER_DEG))(table, 0x66);
    if (victime > 1 || !entree) return suite(ctx);
    const WORD pv = *(WORD *)(At(VA_JOUEURS) + victime * BLOC_JOUEUR + 0x2C);
    short *deg = (short *)(entree + 4); const short avant = *deg;
    if (!pv || (int)pv > (int)avant) return suite(ctx);
    *deg = (short)(pv - 1);
    const DWORD r = suite(ctx);
    *deg = avant;
    if (InterlockedIncrement(&g_plafonds) <= 50) Note("suite impact_plafonne degats/pv", avant, pv, 0);
    return r;
}
static void *volatile g_appliSuite;
__attribute__((used)) void __cdecl AoT_Appliquer(void)
{
    if (!g_degats || !Autorise()) return;
    const DWORD scene = *(volatile DWORD *)At(VA_SCENE_ID);
    if (scene < SCENES_TITAN_1 || scene > SCENES_TITAN_N) return;
    for (DWORD p = 0; p < 2; p++) {
        uint8_t *j = At(VA_JOUEURS) + p * BLOC_JOUEUR;
        const WORD pv = *(WORD *)(j + 0x2C), attente = *(WORD *)(j + 0x5A);
        if (!pv || attente < pv) continue;
        *(WORD *)(j + 0x5A) = (WORD)(pv - 1);
        if (InterlockedIncrement(&g_plafonds) <= 50) Note("power_blow coup_plafonne joueur/degats/pv", (long)p + 1, attente, pv);
    }
}
__asm__(".text\n_AoT_AppliThunk:\n"
        "  pushal\n"
        "  call _AoT_Appliquer\n"
        "  popal\n"
        "  jmp *_g_appliSuite\n");
void AoT_AppliThunk(void);

static DWORD LireSuite(void)
{
    const uint8_t *op = At(VA_OPERANDE_B3);
    if (op[-3] != 0x8B || op[-2] != 0x34 || op[-1] != 0x8D) return 0;
    DWORD *table = *(DWORD **)op;
    if ((uint8_t *)table == At(VA_TABLE_B3_PC) || !Lisible(table, 64)) return 0;
    const DWORD id = table[15];
    return id >= 0x400 && id < 0x600 ? id : 0;
}
static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    DWORD id = 0;
    for (int t = 0; t < 6000 && !(id = LireSuite()); t++) Sleep(100);
    if (GetModuleHandleA("DOA5LR-AoTPowerBlowFix.asi")) {
        Fiche_Correctifs(0); Note("module_separe_present inactif", 0, 0, 0); return 0;
    }
    if (!id) { Note("pas_de_scene_de_suite (stage AoT absent)", 0, 0, 0); return 0; }

    static const uint8_t PRO_SAUT[] = {0x55, 0x8B, 0xEC, 0x51, 0x53};
    static const uint8_t PRO_IMAGE[] = {0x55, 0x8B, 0xEC, 0xF3, 0x0F, 0x10, 0x45, 0x08};
    uint8_t pro_arret[6] = {0x8B, 0x0D};
    const DWORD mgr = (DWORD)(uintptr_t)At(VA_SCENE_MGR); memcpy(pro_arret + 2, &mgr, 4);
    uint8_t *appel = At(VA_APPEL_KO);
    const int appelOk = appel[0] == 0xE8 && appel + 5 + *(int32_t *)(appel + 1) == At(VA_SAUT);
    const int saut = appelOk && ac_detour(At(VA_SAUT), PRO_SAUT, 5, (void *)AoT_SautThunk, &g_sautSuite) != NULL;
    const int arret = ac_detour(At(VA_ARRET), pro_arret, 6, (void *)AoT_ArretThunk, &g_arretSuite) != NULL;
    int mur = 0, degats = 0, coup = 0;
    if (g_mur) {
        static const uint8_t PRO[3] = {0x55, 0x8B, 0xEC};
        mur = !memcmp(At(VA_REQUETE1), PRO, 3) && !memcmp(At(VA_REQUETE2), PRO, 3) && !memcmp(At(VA_REQUETE3), PRO, 3)
           && !memcmp(At(VA_COLLISION), PRO, 3)
           && ac_detour(At(VA_IMAGE), PRO_IMAGE, 8, (void *)AoT_ImageThunk, &g_imageSuite) != NULL;
    }
    if (g_degats) {
        void **slot = (void **)At(VA_OPS_SCENE) + 0x135;
        degats = *slot == (void *)At(VA_OP_DEGATS) && ac_slot(slot, (void *)AoT_OpDegats, &g_opDegats) != NULL;
        coup = ac_call(At(VA_APPEL_APPLI), At(VA_APPLI), (void *)AoT_AppliThunk, &g_appliSuite) != NULL;
    }
    g_suite = id;
    Note("pose suite/saut/arret", (long)id, saut, arret);
    Note("pose mur/degats/coup", mur, degats, coup);
    return 0;
}

void AoTFix_Demarrer(void)
{
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    g_mur = Reglage("AoTFix", "Wall", 1);
    g_degats = Reglage("AoTFix", "KOOnHand", 1);
    AjouterTic(AoT_Tic);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/*  DOA5LR-60fps 1.0 (01/10/2026) - menus, intros, poses de victoire et cinematiques du mode Histoire a 60 fps.
 *
 *  Reprise minimale de 60fps-menus 0.13c (projet DOA5Tools) : meme comportement, code mort et diagnostics retires.
 *  Cible : game.exe 1.10C + AutoLink 3.30 (dinput8Hooked.dll). HORS LIGNE SEULEMENT : tout est coupe des qu'une
 *  session en ligne ou un lecteur en ligne existe (0.13b : desynchros et erreurs reseau en salon).
 *
 *  Principe : AutoLink demande au jeu le mode 30 fps (setter natif 423330) pour certaines scenes et compense avec son
 *  facteur de vitesse. Dans les seuls contextes reconnus ci-dessous, on demande 60 fps (mode 0) et on divise ce
 *  facteur par 2 ; tout est rendu des que le contexte disparait. Tables de temps et code de combat non modifies.
 *   - Menus : selection des persos (GUI 4), du stage (5) et menu principal (9), instance visible et active.
 *   - Intros (GUI -1) : intros de combat hors ligne mesurees (persos/animations verifiees).
 *   - Histoire : cinematiques RealTimeMovie hors ligne, acteurs immobiles ou pilotes par la scene.
 *   - Poses de victoire hors ligne (GUI 0, aucun lecteur en ligne, AutoLink demande 30 fps).
 *
 *  Crochets (pointeurs, verifies avant pose, retires en bloc si l'un echoue) : pointeur du setter dans AutoLink,
 *  entree 3 et destructeur des trois menus, entrees 1/2/4 de RealTimeMovie.
 *  DOA5LR-60fps.ini, section [60fps] : Menus, Intros, WinPoses, Story (1 = actif). Relu en continu.
 *  Retire depuis 0.13c : journal, traces d'intro, chemins en ligne inactifs (GUI 0 spectateur, poses "source30",
 *  lecteur en ligne et sa verification), option MatchContext (sans effet sans ces chemins).
 *
 *  Compilation (LLVM-MinGW, 32 bits) :
 *    i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -o DOA5LR-60fps.asi fps60.c
 */
/*  Version integree a DOA5Tools : reglages [60fps] de DOA5Tools.ini ; sans AutoLink, ne fait rien. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

static BYTE *base, *albase;
static WCHAR ini[MAX_PATH];
static volatile LONG enabled, forced;
static BYTE *tracked;
static DWORD last_tick;
static BOOL scale_owned;
static float saved_fps_factor, written_fps_factor, written_scale;
static void (__cdecl *native_set)(int);
static void (__cdecl *native_refresh)(void);
static volatile LONG intro_enabled, intro_active, win_enabled, story_enabled;
static BYTE *intro_blocked;
static int upstream_mode;
static BOOL refreshing;
/* globaux (non static) : utilises par les crochets en assembleur */
void *menu_original, *main_delete_original, *select_delete_original, *stage_delete_original;
void *movie_begin_original, *movie_update_original, *movie_end_original;

/* seule la fonction de rafraichissement du jeu recalcule les valeurs de temps derivees */
static void refresh_timing(void)
{
    if (refreshing) return;
    refreshing = TRUE; native_refresh(); refreshing = FALSE;
}

static BOOL readmem(const void *p, void *out, SIZE_T n)
{
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), p, out, n, &done) && done == n;
}
static BOOL equal(const void *p, const void *expected, SIZE_T n)
{
    BYTE b[128];
    return n <= sizeof b && readmem(p, b, n) && !memcmp(b, expected, n);
}
static DWORD u32(const BYTE *p) { DWORD v; memcpy(&v, p, 4); return v; }

/* facteur de vitesse d'AutoLink (albase+0xbd634) et echelle du jeu (base+0xdd8fbc) */
static void release_scale(void)
{
    if (!scale_owned) return;
    float *fps = (float *)(albase + 0xbd634), *scale = (float *)(base + 0xdd8fbc);
    if (!memcmp(fps, &written_fps_factor, 4)) *fps = saved_fps_factor;
    if (!memcmp(scale, &written_scale, 4))
        *scale = *fps * *(float *)(albase + 0xbd638) * *(float *)(albase + 0xbd268);
    scale_owned = FALSE;
}
static void apply_scale(void)
{
    float *fps = (float *)(albase + 0xbd634), *scale = (float *)(base + 0xdd8fbc);
    saved_fps_factor = *fps;
    written_fps_factor = saved_fps_factor * 0.5f;
    written_scale = written_fps_factor * *(float *)(albase + 0xbd638) * *(float *)(albase + 0xbd268);
    *fps = written_fps_factor; *scale = written_scale; scale_owned = TRUE;
}

/* session en ligne : lecteur en ligne (base+0xf8f208) ou etat Online::Matching (base+0xf81898+4) */
static BOOL online_session(void)
{
    return *(DWORD *)(base + 0xf8f208) != 0 || *(DWORD *)(base + 0xf81898 + 4) != 0;
}

/* menu : instance vivante et visible d'un des trois menus verifies */
static BOOL eligible(BYTE *object)
{
    BYTE header[0x6c];
    if (online_session()) return FALSE;
    if (!object || !readmem(object, header, sizeof header)) return FALSE;
    const DWORD vt = u32(header), id = u32(header + 4);
    if (!((vt == (DWORD)(base + 0xc0f22c) && id == 9) ||
          (vt == (DWORD)(base + 0xc0cea4) && id == 4) ||
          (vt == (DWORD)(base + 0xc0d2cc) && id == 5))) return FALSE;
    return header[0x6a] == 1 && header[0x6b] == 1 &&
           *(DWORD *)(base + 0xf8a7ac) == id && *(DWORD *)(base + 0xfce5cc) == 0;
}

/* RealTimeMovie active (phase 3 a 7, objet courant de type RealTimeMovie, actif) */
static BOOL movie_running(void)
{
    const DWORD phase = *(DWORD *)(base + 0xfce5cc);
    BYTE *object = *(BYTE **)(base + 0xfce5e8), header[8];
    return phase >= 3 && phase <= 7 && object && object != intro_blocked && readmem(object, header, sizeof header) &&
           u32(header) == (DWORD)(base + 0x9b932c) && header[4] == 1;
}

/* intros de combat hors ligne (GUI -1) : persos et animations mesures en 0.7.1 */
static BOOL intro_eligible(void)
{
    if (!intro_enabled || *(DWORD *)(base + 0xf8a7ac) != 0xffffffff || *(DWORD *)(base + 0xf8f208) != 0) return FALSE;
    if (!movie_running()) return FALSE;
    BYTE *a = base + 0xfd0540, *b = a + 0x6c8;
    if (!((a[4] == 4 && b[4] == 13) || (a[4] == 13 && b[4] == 4))) return FALSE;
    for (int p = 0; p < 2; p++) {
        BYTE *actor = base + 0xfd0540 + p * 0x6c8;
        const DWORD anim = *(DWORD *)(actor + 0x64), pc = *(DWORD *)(base + 0xfcc6d8 + p * 4);
        const BOOL known = (actor[4] == 4 && (anim == 0x3ec || anim == 0x3ee)) || (actor[4] == 13 && anim == 0x3f5);
        if (known && *(DWORD *)(actor + 0x6c) == 0 &&
            pc >= (DWORD)(base + 0xd016fc) && pc <= (DWORD)(base + 0xd01706)) return TRUE;
    }
    return FALSE;
}

/* cinematiques du mode Histoire (hors ligne) : hors menus geres et ecran de resultat ; sous GUI 0 ou -1,
 * acteurs immobiles (action 0) ou pilotes par la scene (0x118). Combats, degats et poses gardent leurs actions. */
static BOOL story_eligible(void)
{
    if (!story_enabled || upstream_mode != 1 || *(DWORD *)(albase + 0xffff0) != 1) return FALSE;
    const DWORD gui = *(DWORD *)(base + 0xf8a7ac);
    if (gui == 4 || gui == 5 || gui == 9 || gui == 14) return FALSE;
    if (*(DWORD *)(base + 0xf8f208) != 0) return FALSE;
    if (gui == 0 || gui == 0xffffffff) {
        BYTE *a = base + 0xfd0540, *b = a + 0x6c8;
        const DWORD ma = *(DWORD *)(a + 0x6c), mb = *(DWORD *)(b + 0x6c);
        if (!((ma == 0 || ma == 0x118) && (mb == 0 || mb == 0x118))) return FALSE;
        if (gui == 0xffffffff && (*(DWORD *)(a + 0x64) >= 0xffff || *(DWORD *)(b + 0x64) >= 0xffff)) return FALSE;
    }
    return movie_running();
}

/* poses de victoire hors ligne : GUI 0, aucun lecteur en ligne, AutoLink demande 30 fps (le combat et le ralenti
 * du KO restent en mode 0) */
static BOOL win_eligible(void)
{
    if (!win_enabled || upstream_mode != 1 || *(DWORD *)(albase + 0xffff0) != 1) return FALSE;
    if (*(DWORD *)(base + 0xf8a7ac) != 0 || *(DWORD *)(base + 0xf8f208) != 0) return FALSE;
    return movie_running();
}

static BOOL scene_eligible(void)
{
    return !online_session() && (intro_eligible() || story_eligible() || win_eligible());
}

static void release_intro(void)
{
    if (!intro_active) return;
    release_scale();
    InterlockedExchange(&intro_active, 0);
    if (InterlockedExchange(&forced, 0) && upstream_mode >= 0 && upstream_mode <= 4 &&
        *(DWORD *)(base + 0x108b488) == 0) native_set(upstream_mode);
    refresh_timing();
}

/* AutoLink garde son propre mode demande : on ne filtre que ce qui part vers le setter du jeu, sa prochaine
 * demande hors contexte retablit donc le comportement normal. */
static void __cdecl filtered_set(int mode)
{
    /* AutoLink ecrit son facteur et son echelle juste avant cet appel : ils remplacent nos valeurs */
    const BOOL was_intro = intro_active;
    scale_owned = FALSE; upstream_mode = mode;
    const BOOL want_menu = enabled && mode == 1 && (DWORD)(GetTickCount() - last_tick) <= 100 && eligible(tracked);
    const BOOL want_intro = mode == 1 && scene_eligible();
    const BOOL want = want_menu || want_intro;
    if (want) apply_scale();
    InterlockedExchange(&intro_active, want_intro);
    InterlockedExchange(&forced, want);
    native_set(want ? 0 : mode);
    if (was_intro || want_intro) refresh_timing();
}

static void release_menu(void)
{
    tracked = NULL;
    if (intro_active) return;
    release_scale();
    if (InterlockedExchange(&forced, 0)) {
        const DWORD cached = *(DWORD *)(albase + 0xffff0);   /* mode retenu par AutoLink, jamais modifie */
        if (cached <= 4 && *(DWORD *)(base + 0x108b488) == 0) native_set((int)cached);
    }
}

void __cdecl menu_before(BYTE *object)
{
    if (eligible(object)) {
        tracked = object; last_tick = GetTickCount();
        const DWORD requested = *(DWORD *)(albase + 0xffff0);
        release_scale();                                    /* evite la derive 0.5 -> 0.25 -> ... */
        if (requested <= 4) {
            const BOOL want = enabled && requested == 1;
            if (want) apply_scale();
            InterlockedExchange(&forced, want);
            native_set(want ? 0 : (int)requested);
        }
    } else if (tracked == object) release_menu();
}
void __cdecl menu_leaving(BYTE *object) { if (tracked == object) release_menu(); }
void __cdecl movie_begin(BYTE *object) { if (intro_blocked == object) intro_blocked = NULL; }
void __cdecl movie_before(BYTE *object)
{
    if (object != *(BYTE **)(base + 0xfce5e8)) return;
    if (upstream_mode == 1 && scene_eligible()) {
        release_scale(); apply_scale();
        InterlockedExchange(&intro_active, 1); InterlockedExchange(&forced, 1);
        native_set(0); refresh_timing();
    } else release_intro();
}
void __cdecl movie_leaving(BYTE *object)
{
    if (object != *(BYTE **)(base + 0xfce5e8)) return;
    intro_blocked = object;
    release_intro();
}

/* Conserve l'ABI thiscall d'origine (arguments, valeur de retour, drapeaux, x87/SSE, registres). Les fonctions
 * d'aide s'executent AVANT la fonction du jeu : les transitions du jeu ont le dernier mot. */
#define BEFORE_HELPER(helper, target) \
    __asm__ volatile("pushfl\n pushal\n mov %esp,%ebp\n sub $544,%esp\n and $-16,%esp\n" \
        "fxsave 16(%esp)\n mov %ecx,(%esp)\n call _" helper "\n fxrstor 16(%esp)\n" \
        "mov %ebp,%esp\n popal\n popfl\n jmp *_" target "\n")
__attribute__((naked, thiscall)) void menu_hook(void *object) { BEFORE_HELPER("menu_before", "menu_original"); }
__attribute__((naked, thiscall)) int main_delete_hook(void *object, int flags) { BEFORE_HELPER("menu_leaving", "main_delete_original"); }
__attribute__((naked, thiscall)) int select_delete_hook(void *object, int flags) { BEFORE_HELPER("menu_leaving", "select_delete_original"); }
__attribute__((naked, thiscall)) int stage_delete_hook(void *object, int flags) { BEFORE_HELPER("menu_leaving", "stage_delete_original"); }
__attribute__((naked, thiscall)) int movie_begin_hook(void *object, int arg) { BEFORE_HELPER("movie_begin", "movie_begin_original"); }
__attribute__((naked, thiscall)) int movie_update_hook(void *object, float dt) { BEFORE_HELPER("movie_before", "movie_update_original"); }
__attribute__((naked, thiscall)) int movie_end_hook(void *object) { BEFORE_HELPER("movie_leaving", "movie_end_original"); }

typedef struct { void **slot; void *original; void *replacement; } Hook;
static BOOL exchange(Hook *h, BOOL install)
{
    DWORD old, unused;
    if (!VirtualProtect(h->slot, sizeof(void *), PAGE_READWRITE, &old)) return FALSE;
    void *expected = install ? h->original : h->replacement;
    void *value = install ? h->replacement : h->original;
    void *previous = InterlockedCompareExchangePointer(h->slot, value, expected);
    VirtualProtect(h->slot, sizeof(void *), old, &unused);
    return previous == expected;
}

/* signatures du jeu et d'AutoLink 3.30 : rien n'est pose si l'une differe */
static BOOL signatures(void)
{
    const DWORD table[15] = {1, 1, 1, 2, 1, 2, 3, 1, 3, 1, 1, 1, 2, 2, 1};
    BYTE setter[20] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 8, 0xa3, 0, 0, 0, 0, 0xc6, 5, 0, 0, 0, 0, 1, 0x5d, 0xc3};
    DWORD addr = (DWORD)(base + 0x108b488); memcpy(setter + 7, &addr, 4);
    addr = (DWORD)(base + 0x108b494); memcpy(setter + 13, &addr, 4);
    const BYTE dispatch[17] = {0x80, 0x79, 0x6a, 0, 0x74, 0x0a, 0x8b, 1, 0x8b, 0x90, 0x84, 0, 0, 0, 0xff, 0xe2, 0xc3};
    BYTE getter[7] = {0xd9, 5, 0, 0, 0, 0, 0xc3}; addr = (DWORD)(base + 0xdd8fb8); memcpy(getter + 2, &addr, 4);
    void *expected = base + 0x423330;
    void *expected_scale = base + 0xdd8fbc;
    BYTE alcall[6] = {0xff, 0x15, 0, 0, 0, 0}; addr = (DWORD)(albase + 0xffff4); memcpy(alcall + 2, &addr, 4);
    BYTE prefix[20] = {0x55, 0x8b, 0xec, 0x51, 0xf3, 0x0f, 0x10, 0x05, 0, 0, 0, 0, 0xf3, 0x0f, 0x10, 0x15, 0, 0, 0, 0};
    addr = (DWORD)(base + 0xdd8fbc); memcpy(prefix + 8, &addr, 4);
    addr = (DWORD)(base + 0xdd8fb8); memcpy(prefix + 16, &addr, 4);
    const BYTE tail[] = {0x5b, 0x8b, 0xe5, 0x5d, 0xc3};
    return equal(base + 0x9c1dac, table, sizeof table) && equal(base + 0x423330, setter, sizeof setter) &&
           equal(base + 0x690100, dispatch, sizeof dispatch) && equal(base + 0x3f53f0, getter, 7) &&
           equal(albase + 0xffff4, &expected, 4) && equal(albase + 0xfffe8, &expected_scale, 4) &&
           equal(albase + 0x4cd8a, alcall, 6) && equal(albase + 0x4cdba, alcall, 6) &&
           equal(base + 0x3f5480, prefix, sizeof prefix) && equal(base + 0x3f5570, tail, sizeof tail);
}

static void read_settings(void)
{
    InterlockedExchange(&enabled, GetPrivateProfileIntW(L"60fps", L"Menus", 1, ini) != 0);
    InterlockedExchange(&intro_enabled, GetPrivateProfileIntW(L"60fps", L"Intros", 1, ini) != 0);
    InterlockedExchange(&win_enabled, GetPrivateProfileIntW(L"60fps", L"WinPoses", 1, ini) != 0);
    InterlockedExchange(&story_enabled, GetPrivateProfileIntW(L"60fps", L"Story", 1, ini) != 0);
}

static DWORD WINAPI worker(void *unused)
{
    (void)unused;
    BOOL ready = FALSE;
    for (int n = 0; n < 600 && !ready; n++) {
        albase = (BYTE *)GetModuleHandleW(L"dinput8Hooked.dll");
        ready = albase && signatures();
        if (!ready) Sleep(100);
    }
    if (!ready) return 0;                                   /* AutoLink absent ou autre version : rien n'est fait */
    native_set = (void (__cdecl *)(int))(base + 0x423330);
    native_refresh = (void (__cdecl *)(void))(base + 0x3f5480);
    movie_begin_original = base + 0x321e70; movie_update_original = base + 0x320250; movie_end_original = base + 0x321030;
    upstream_mode = *(DWORD *)(base + 0x108b488);
    menu_original = base + 0x690100;
    main_delete_original = base + 0x6f0f50; select_delete_original = base + 0x691d40; stage_delete_original = base + 0x6944f0;
    read_settings();
    Hook hooks[] = {
        {(void **)(albase + 0xffff4), (void *)native_set, (void *)filtered_set},
        {(void **)(base + 0xc0f22c + 12), menu_original, (void *)menu_hook},
        {(void **)(base + 0xc0cea4 + 12), menu_original, (void *)menu_hook},
        {(void **)(base + 0xc0d2cc + 12), menu_original, (void *)menu_hook},
        {(void **)(base + 0xc0f22c), main_delete_original, (void *)main_delete_hook},
        {(void **)(base + 0xc0cea4), select_delete_original, (void *)select_delete_hook},
        {(void **)(base + 0xc0d2cc), stage_delete_original, (void *)stage_delete_hook},
        {(void **)(base + 0x9b932c + 4), movie_begin_original, (void *)movie_begin_hook},
        {(void **)(base + 0x9b932c + 8), movie_update_original, (void *)movie_update_hook},
        {(void **)(base + 0x9b932c + 16), movie_end_original, (void *)movie_end_hook}
    };
    const unsigned count = sizeof hooks / sizeof hooks[0];
    for (unsigned n = 0; n < count; n++) if (!equal(hooks[n].slot, &hooks[n].original, 4)) return 0;   /* deja modifie */
    unsigned installed = 0;
    while (installed < count && exchange(&hooks[installed], TRUE)) installed++;
    if (installed != count) { while (installed) exchange(&hooks[--installed], FALSE); return 0; }
    for (;;) { Sleep(1000); read_settings(); }              /* .ini modifiable jeu ouvert */
}

void Fps60_Demarrer(void)
{
    base = (BYTE *)GetModuleHandleW(NULL);
    MultiByteToWideChar(CP_ACP, 0, CheminIni(), -1, ini, MAX_PATH);   /* DOA5Tools.ini, section [60fps] */
    HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

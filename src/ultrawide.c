/* Based on DOA5LRFix by Lyall (https://codeberg.org/Lyall/DOA5LRFix).
 * Copyright (c) 2025 Lyall. MIT License, see third-party/LICENSE-DOA5LRFix.txt. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#ifdef DOA5TOOLS
#include "commun.h"
#endif

static uint8_t *g_base;
static int g_customRes = 1, g_resX, g_resY, g_fixHUD = 1;

static const float NATIF = 16.0f / 9.0f;
static volatile float g_aspect = 16.0f / 9.0f, g_mult = 1.0f;
static int g_courX, g_courY;
static void CalculerAspect(void)
{
    if (g_courX <= 0 || g_courY <= 0) return;
    g_aspect = (float)g_courX / (float)g_courY;
    g_mult = g_aspect / NATIF;
}

typedef void (__cdecl *Handler_t)(uint32_t *r);
typedef struct { DWORD rva; uint8_t n; const uint8_t *octets; const uint8_t *masque; int8_t rel32; } Site;
static int Verifier(const Site *s)
{
    const uint8_t *p = g_base + s->rva;
    for (int i = 0; i < s->n; i++) if (s->masque[i] && p[i] != s->octets[i]) return 0;
    return 1;
}
static int Crocheter(const Site *s, Handler_t h)
{
    uint8_t *site = g_base + s->rva;
    uint8_t *b = (uint8_t *)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), *p = b;
    if (!b) return 0;
    static const uint8_t avant[] = {
        0x9C, 0x60,
        0x89, 0xE5,
        0x81, 0xEC, 0x10, 0x02, 0x00, 0x00,
        0x83, 0xE4, 0xF0,
        0x0F, 0xAE, 0x04, 0x24,
        0x55 };
    memcpy(p, avant, sizeof avant); p += sizeof avant;
    *p++ = 0xE8; *(int32_t *)p = (int32_t)((uint8_t *)h - (p + 4)); p += 4;
    static const uint8_t apres[] = {
        0x83, 0xC4, 0x04,
        0x0F, 0xAE, 0x0C, 0x24,
        0x89, 0xEC,
        0x61, 0x9D };
    memcpy(p, apres, sizeof apres); p += sizeof apres;
    uint8_t *depl = p;
    memcpy(p, site, s->n); p += s->n;
    if (s->rel32 >= 0) {
        const int k = s->rel32;
        uint8_t *cible = site + k + 4 + *(int32_t *)(site + k);
        *(int32_t *)(depl + k) = (int32_t)(cible - (depl + k + 4));
    }
    *p++ = 0xE9; *(int32_t *)p = (int32_t)((site + s->n) - (p + 4)); p += 4;
    DWORD ancien;
    if (!VirtualProtect(b, 128, PAGE_EXECUTE_READ, &ancien)) return 0;
    FlushInstructionCache(GetCurrentProcess(), b, 128);
    if (!VirtualProtect(site, s->n, PAGE_EXECUTE_READWRITE, &ancien)) return 0;
    site[0] = 0xE9; *(int32_t *)(site + 1) = (int32_t)(b - (site + 5));
    for (int i = 5; i < s->n; i++) site[i] = 0x90;
    VirtualProtect(site, s->n, ancien, &ancien);
    FlushInstructionCache(GetCurrentProcess(), site, s->n);
    return 1;
}
enum { EDI, ESI, EBP, ESP_, EBX, EDX, ECX, EAX };
#define F(adresse) (*(volatile float *)(uintptr_t)(adresse))
#define I(adresse) (*(volatile int32_t *)(uintptr_t)(adresse))

static void __cdecl SurAspect(uint32_t *r) { F(r[EAX] + 0xC4) = g_aspect; }
static void __cdecl SurResolution(uint32_t *r)
{
    if (g_customRes) {
        r[EDX] = (uint32_t)g_resX; r[EDI] = (uint32_t)g_resY;
        I(r[ECX] + 0x0C) = g_resX; I(r[ECX] + 0x10) = g_resY;
    }
    const int x = (int)r[EDX], y = (int)r[EDI];
    if (x != g_courX || y != g_courY) { g_courX = x; g_courY = y; CalculerAspect(); }
}
static void __cdecl SurTailleHUD(uint32_t *r)
{
    if (g_aspect > NATIF) {
        F(r[EBX] + 0x08) = 2.0f / (720.0f * g_aspect);
        F(r[EBX] + 0x38) = (-1.0f - (1.0f / 1280.0f)) / g_mult;
    } else {
        F(r[EBX] + 0x1C) = -2.0f / (1280.0f / g_aspect);
        F(r[EBX] + 0x3C) = (1.0f - (1.0f / 720.0f)) * g_mult;
    }
}
static void __cdecl SurGuideH(uint32_t *r)
{
    if (g_aspect > NATIF) {
        r[EDX] = (uint32_t)(int)(720.0f * g_aspect);
        r[ECX] += (uint32_t)(int)((720.0f * g_aspect - 1280.0f) / 2.0f);
    }
}
static void __cdecl SurGuideV(uint32_t *r)
{
    if (g_aspect < NATIF) {
        r[ESI] = (uint32_t)(int)(1280.0f / g_aspect);
        r[ECX] += (uint32_t)(int)((1280.0f / g_aspect - 720.0f) / 2.0f);
    }
}
static void __cdecl SurVideos(uint32_t *r)
{
    if (g_aspect > NATIF) {
        F(r[EBP] - 0x18) = -1.0f / g_mult;
        F(r[EBP] - 0x10) = 1.0f / g_mult;
    } else {
        F(r[EBP] - 0x14) = 1.0f * g_mult;
        F(r[EBP] - 0x0C) = -1.0f * g_mult;
    }
}

static const uint8_t O_ECHELLE[] = {0x74, 0x0E}, M_2[] = {1, 1};
static const uint8_t O_ASPECT[] = {0xF3, 0x0F, 0x10, 0x80, 0xC4, 0x00, 0x00, 0x00}, M_8[] = {1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t O_RESOL[] = {0x77, 0x13, 0x89, 0x79, 0x18}, M_5[] = {1, 1, 1, 1, 1};
static const uint8_t O_HUD[] = {0x80, 0x7B, 0x7C, 0x00, 0x0F, 0x85, 0xA9, 0x01, 0x00, 0x00}, M_10[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t O_GUIDE_H[] = {0x89, 0x55, 0x08, 0x56, 0x8B, 0x35, 0, 0, 0, 0}, M_GUIDE_H[] = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0};
static const uint8_t O_GUIDE_V[] = {0xF2, 0x0F, 0x5E, 0xD0, 0xF2, 0x0F, 0x10, 0x05, 0, 0, 0, 0}, M_GUIDE_V[] = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0};
static const uint8_t O_VIDEOS[] = {0x8A, 0x55, 0xF8, 0x2A, 0xC2};
static const Site S_ECHELLE = {0x834FFE, 2, O_ECHELLE, M_2, -1};
static const Site S_ASPECT  = {0x37B6B4, 8, O_ASPECT, M_8, -1};
static const Site S_RESOL   = {0x3F5621, 5, O_RESOL, M_5, -1};
static const Site S_HUD     = {0x7D067B, 10, O_HUD, M_10, 6};
static const Site S_GUIDE_H = {0x259983, 10, O_GUIDE_H, M_GUIDE_H, -1};
static const Site S_GUIDE_V = {0x2599C1, 12, O_GUIDE_V, M_GUIDE_V, -1};
static const Site S_VIDEOS  = {0x746C10, 5, O_VIDEOS, M_5, -1};

static int PatcherOctet(DWORD rva, uint8_t v)
{
    DWORD ancien;
    if (!VirtualProtect(g_base + rva, 1, PAGE_EXECUTE_READWRITE, &ancien)) return 0;
    g_base[rva] = v;
    VirtualProtect(g_base + rva, 1, ancien, &ancien);
    FlushInstructionCache(GetCurrentProcess(), g_base + rva, 1);
    return 1;
}

static void Note(const char *libelle, long a, long b)
{
#ifdef DOA5TOOLS
    Journal("Ultrawide", libelle, a, b, 0);
#else
    (void)libelle; (void)a; (void)b;
#endif
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    int ok = 0;
    for (int t = 0; t < 60000 && !ok; t++) {

        ok = Verifier(&S_RESOL) && Verifier(&S_ASPECT) && Verifier(&S_ECHELLE) && Verifier(&S_HUD) &&
             Verifier(&S_GUIDE_H) && Verifier(&S_GUIDE_V) && Verifier(&S_VIDEOS);
        if (!ok) Sleep(1);
    }
    if (!ok) { Note("refus_version_game_exe", 0, 0); return 0; }
    int res = 0, hud = 0;
    if (g_customRes && (g_resX <= 0 || g_resY <= 0)) {
        DEVMODEW dm;
        memset(&dm, 0, sizeof dm);
        dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) { g_resX = (int)dm.dmPelsWidth; g_resY = (int)dm.dmPelsHeight; }

        if (g_resX > 0 && g_resY > 0) {
            const float a = (float)g_resX / (float)g_resY;
            if (a > NATIF - 0.01f && a < NATIF + 0.01f) { Note("ecran_16_9 -> inactif", 0, 0); return 0; }
        }
    }
    if (g_customRes) {
        if (g_resX <= 0 || g_resY <= 0) g_customRes = 0;
        else res = PatcherOctet(S_ECHELLE.rva, 0xEB) + Crocheter(&S_ASPECT, SurAspect);
    }

    if (PatcherOctet(S_RESOL.rva, 0x90) && PatcherOctet(S_RESOL.rva + 1, 0x90)) {
        static uint8_t o_resNop[5];
        static uint8_t m_resNop[5] = {1, 1, 1, 1, 1};
        memcpy(o_resNop, g_base + S_RESOL.rva, 5);
        const Site s = {S_RESOL.rva, 5, o_resNop, m_resNop, -1};
        res += Crocheter(&s, SurResolution);
    }
    if (g_fixHUD)
        hud = Crocheter(&S_HUD, SurTailleHUD) + Crocheter(&S_GUIDE_H, SurGuideH) +
              Crocheter(&S_GUIDE_V, SurGuideV) + Crocheter(&S_VIDEOS, SurVideos);
    Note("pret resolution/hud (nombre de crochets)", res, hud);
    return 0;
}

static void LireReglages(const char *ini)
{
    g_customRes = GetPrivateProfileIntA("Ultrawide", "CustomResolution", 1, ini) != 0;
    g_resX = GetPrivateProfileIntA("Ultrawide", "Width", 0, ini);
    g_resY = GetPrivateProfileIntA("Ultrawide", "Height", 0, ini);
    g_fixHUD = GetPrivateProfileIntA("Ultrawide", "FixHUD", 1, ini) != 0;
}

#ifdef DOA5TOOLS
void Ultrawide_Demarrer(void)
{
    LireReglages(CheminIni());
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) { SetThreadPriority(t, THREAD_PRIORITY_HIGHEST); CloseHandle(t); }
}
#else
BOOL WINAPI DllMain(HINSTANCE h, DWORD raison, LPVOID r)
{
    (void)r;
    if (raison == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        char ini[MAX_PATH];
        GetModuleFileNameA(h, ini, sizeof ini);
        char *sep = strrchr(ini, '\\'); if (sep) sep[1] = 0;
        strncat(ini, "DOA5LR-Ultrawide.ini", sizeof ini - strlen(ini) - 1);
        LireReglages(ini);
        HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
        if (t) { SetThreadPriority(t, THREAD_PRIORITY_HIGHEST); CloseHandle(t); }
    }
    return TRUE;
}
#endif

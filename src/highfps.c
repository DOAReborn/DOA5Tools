#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <d3d9.h>
#include "commun.h"

#define RVA_MODE_FPS     0x108B488
#define RVA_LECTEUR      0xF8F208
#define RVA_MATCHING     0xF8189C
#define RVA_GUI          0xF8A7AC
#define RVA_PHASE_COMBAT 0xDD94D4
#define RVA_PHASE_SCENE  0xFCE5CC
#define RVA_MODE_DRAPEAU 0x108B494
#define RVA_ECHELLE      0xDD8FBC
#define PAS_PAR_S        59.94
#define SEUIL_SAUT       2.0f

static BYTE *base;
static volatile LONG actifDemande = 1;
static double limiteFps = 120.0;
static int touche = VK_F3;
static int interpActive = 1;
static int effetsLisses = 1;
static volatile LONG modeRollback, rollbackActif;

static DWORD g_t0;

static void JournalHF(const char *libelle, long a, long b, long c, long d)
{
    Journal("HighFps", libelle, a, b, c);
    if (d) Journal("HighFps", "  (suite)", d, 0, 0);
}

typedef void (__cdecl *Lire_t)(void);
typedef int (__cdecl *NbPas_t)(void);
typedef double (__cdecl *Vitesse_t)(void);
typedef void (__cdecl *MajDt_t)(float dt, int fige);
static Lire_t    lireOrig;
static NbPas_t   nbPasOrig;
static Vitesse_t vitesseOrig;
static MajDt_t   majDtOrig;
static double diviseur = PAS_PAR_S;
static double facteur = 1.0;
static double accu;
static LARGE_INTEGER freq, dernier;
static BOOL actifImage, roundImage;
static int pasImage = -1;

static DWORD pasTotal, pasAff; static double fracAff = 1.0;
static DWORD numImage = 2;
static long nImages, nPas; static DWORD debutReleve;

static float Alpha(DWORD pasE)
{
    const double a = (double)(LONG)(pasAff - pasE) + fracAff;
    return a <= 0.0 ? 0.0f : a >= 1.0 ? 1.0f : (float)a;
}
static BOOL HorsLigne(void) { return *(volatile DWORD *)(base + RVA_LECTEUR) == 0 && *(volatile DWORD *)(base + RVA_MATCHING) == 0; }
static BOOL InterpEnService(void) { return interpActive && actifImage && roundImage; }
static void JournalLissage(void);

static int menus60 = 1;
static int scenesEnLigne = 0;
static volatile LONG modeJeu = -1;
static BOOL menuForce;
static float echelleJeu = 1.0f, echelleNous;
static DWORD guiVu = 0xFFFFFFFF, guiDepuis;

static BOOL CinematiqueEnCours(void)
{
    const DWORD phase = *(volatile DWORD *)(base + RVA_PHASE_SCENE);
    BYTE *obj = *(BYTE **)(base + 0xFCE5E8);
    return phase >= 3 && phase <= 7 && obj && !IsBadReadPtr(obj, 8) &&
           *(DWORD *)obj == (DWORD)(ULONG_PTR)(base + 0x9B932C) && obj[4] == 1;
}

static BOOL IntroVerifiee(void) { return *(volatile DWORD *)(base + RVA_GUI) == 0xFFFFFFFF && CinematiqueEnCours(); }

static BOOL PoseVerifiee(void) { return *(volatile DWORD *)(base + RVA_GUI) == 0 && CinematiqueEnCours(); }
static void PoserMode(int mode)
{
    *(volatile DWORD *)(base + RVA_MODE_FPS) = (DWORD)mode;
    *(volatile BYTE *)(base + RVA_MODE_DRAPEAU) = 1;
}

static void __cdecl FiltreMode(int mode)
{
    if (mode != modeJeu)
        JournalHF("mode demande gui scene combat", mode, (long)*(volatile DWORD *)(base + RVA_GUI),
                (long)*(volatile DWORD *)(base + RVA_PHASE_SCENE), (long)*(volatile DWORD *)(base + RVA_PHASE_COMBAT));
    InterlockedExchange(&modeJeu, mode);
    PoserMode(menuForce && mode == 1 ? 0 : mode);
}
static void LibererMenu(void)
{
    volatile float *e = (volatile float *)(base + RVA_ECHELLE);
    if (*e == echelleNous) *e = echelleJeu;
    PoserMode(modeJeu >= 0 && modeJeu <= 4 ? (int)modeJeu : 0);
    menuForce = FALSE;
    JournalHF("menu rendu mode", modeJeu, 0, 0, 0);
}
static DWORD sceneVue = 0xFFFFFFFF;
static void NoterIntro(void)
{
    BYTE *obj = *(BYTE **)(base + 0xFCE5E8), *a = base + 0xFD0540, *b = a + 0x6C8;
    const int table = obj && !IsBadReadPtr(obj, 8) ? (*(DWORD *)obj == (DWORD)(ULONG_PTR)(base + 0x9B932C)) * 10 + obj[4] : -1;
    JournalHF("intro scene table etat_a etat_b", (long)*(volatile DWORD *)(base + RVA_PHASE_SCENE), table, a[4], b[4]);
    JournalHF("intro anim_a anim_b action_a action_b", (long)*(DWORD *)(a + 0x64), (long)*(DWORD *)(b + 0x64), (long)*(DWORD *)(a + 0x6C), (long)*(DWORD *)(b + 0x6C));
    JournalHF("intro code_a code_b mode", (long)(*(DWORD *)(base + 0xFCC6D8) - (DWORD)(ULONG_PTR)base), (long)(*(DWORD *)(base + 0xFCC6DC) - (DWORD)(ULONG_PTR)base),
            (long)*(volatile DWORD *)(base + RVA_MODE_FPS), modeJeu);
}
static void SuivreMenus(void)
{
    const DWORD gui = *(volatile DWORD *)(base + RVA_GUI), now = GetTickCount();
    const DWORD sc = *(volatile DWORD *)(base + RVA_PHASE_SCENE);
    if (gui != guiVu || sc != sceneVue) { guiVu = gui; sceneVue = sc; guiDepuis = now; }
    static DWORD introNotee = 0xFFFFFFFF;
    if (gui == 0xFFFFFFFF && now - guiDepuis >= 1000 && introNotee != guiDepuis) { introNotee = guiDepuis; NoterIntro(); }
    const BOOL horsLigne = HorsLigne();

    static BOOL matchRollbackVu;
    if (modeRollback && rollbackActif) matchRollbackVu = TRUE;
    else if (horsLigne || sc == 0) matchRollbackVu = FALSE;
    const BOOL enLigneOk = scenesEnLigne && modeRollback && matchRollbackVu;
    const BOOL scene = (horsLigne || enLigneOk) && (IntroVerifiee() || PoseVerifiee());
    const BOOL menu = menus60 && modeJeu == 1 && ((horsLigne && now - guiDepuis >= 500) || scene);
    volatile float *e = (volatile float *)(base + RVA_ECHELLE);
    if (!menuForce) {
        if (menu && *(volatile DWORD *)(base + RVA_MODE_FPS) == 1) {
            echelleJeu = *e;
            echelleNous = echelleJeu * 0.5f;
            menuForce = TRUE; PoserMode(0); *e = echelleNous;
            JournalHF("menu 60 gui cinematique en_ligne", (long)gui, scene, !horsLigne, 0);
        }
    } else if (!menu) LibererMenu();
    else if (*e != echelleNous) { echelleJeu = *e; *e = echelleNous; }
}

static void Decider(void)
{
    SuivreMenus();
    const int n0 = modeRollback ? 1 : nbPasOrig();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    double ecoule = dernier.QuadPart ? (double)(t.QuadPart - dernier.QuadPart) / (double)freq.QuadPart : 1.0 / PAS_PAR_S;
    dernier = t;
    if (ecoule < 0.0) ecoule = 0.0;
    if (ecoule > 0.1) ecoule = 0.1;

    const DWORD gui = *(volatile DWORD *)(base + RVA_GUI), phase = *(volatile DWORD *)(base + RVA_PHASE_COMBAT);
    const DWORD modeFps = *(volatile DWORD *)(base + RVA_MODE_FPS);
    const BOOL round = (phase == 2 && (gui == 0 || gui == 0xFFFFFFFF || gui == 0x47)) || (phase == 1 && gui == 0x47)
                    || ((phase == 1 || phase == 3) && (gui == 0 || gui == 0xFFFFFFFF) && !CinematiqueEnCours());
    const BOOL autorise = HorsLigne() || (modeRollback && rollbackActif);
    const BOOL actif = actifDemande && n0 == 1 && autorise && modeFps == 0 && round && !menuForce;
    pasAff = pasTotal; fracAff = actifImage ? accu : 1.0;
    if (actif != actifImage) { actifImage = actif; accu = 0.0; JournalHF("actif gui phase", actif, (long)gui, (long)phase, 0); }
    roundImage = round;
    int n = n0;
    if (actif) {
        diviseur = limiteFps > 0.0 ? limiteFps : 100000.0;
        accu += ecoule * PAS_PAR_S;
        n = (int)accu;
        if (n > 4) { n = 4; accu = 0.0; } else accu -= n;
        facteur = effetsLisses && !modeRollback ? ecoule * PAS_PAR_S : (double)n;
        if (facteur > 16.0) facteur = 16.0;
    } else { diviseur = PAS_PAR_S; facteur = 1.0; }
    pasImage = n;
    pasTotal += (DWORD)n;
    nImages++; nPas += n;
    const DWORD now = GetTickCount();
    if (now - debutReleve >= 2000) {
        const DWORD duree = now - debutReleve ? now - debutReleve : 1;
        JournalHF("releve images_s pas_s_x10 actif", nImages * 1000 / (long)duree, nPas * 10000 / (long)duree, actifImage, 0);
        if (actifImage) JournalLissage();
        nImages = nPas = 0; debutReleve = now;
    }
}

int __cdecl HighFps_ImageAvecPas(int actifRollback)
{
    InterlockedExchange(&modeRollback, 1);
    InterlockedExchange(&rollbackActif, actifRollback != 0);
    if (!base || !nbPasOrig) return 1;
    if (pasImage < 0) Decider();
    return pasImage > 0 || !actifImage;
}

static void __cdecl LireManettes(void)
{
    if (pasImage < 0) Decider();
    if (pasImage > 0 || !actifImage) lireOrig();
}

static int __cdecl NbPasBoucle(void)
{
    if (pasImage < 0) Decider();
    const int n = modeRollback ? nbPasOrig() : pasImage;
    pasImage = -1;
    return n;
}

static double __cdecl VitesseImage(void) { return modeRollback ? vitesseOrig() : vitesseOrig() * facteur; }

static void __cdecl MajDt(float dt, int fige) { majDtOrig(modeRollback ? dt : dt * (float)facteur, fige); }

static float camVrai[8], camAnc[8], camAff[8]; static DWORD camPas; static int camValide, camAPoser;
static BYTE *camPosee; static DWORD camImage = 0xFFFFFFFF, camParImage;
static long camLissees, camSauts, camParImages;
static BYTE *ObjetCamera(void) { return ((BYTE *(__cdecl *)(int))(base + 0x2CE1F0))(-1); }
static void CalculerCamera(void)
{
    if (camImage == numImage) return;
    camImage = numImage; camAPoser = 0;
    if (!InterpEnService()) { camValide = 0; return; }
    BYTE *cam = ObjetCamera();
    if (!cam) return;
    float cur[8];
    memcpy(cur, cam + 0x84, sizeof cur);
    if (!camValide) { memcpy(camVrai, cur, sizeof cur); memcpy(camAnc, cur, sizeof cur); camPas = pasAff; camValide = 1; }
    else if (memcmp(cur, camVrai, sizeof cur)) {
        int saut = 0;
        for (int k = 0; k < 8; k++) { const float e = cur[k] - camVrai[k]; if (e > SEUIL_SAUT || e < -SEUIL_SAUT) saut = 1; }
        if (saut) camSauts++;

        if (!saut && camPas == pasAff) { camParImage = numImage + 8; camParImages++; }
        memcpy(camAnc, saut || camPas == pasAff ? cur : camVrai, sizeof cur);
        memcpy(camVrai, cur, sizeof cur); camPas = pasAff;
    }
    const float t = Alpha(camPas);
    if (t < 1.0f && memcmp(camAnc, camVrai, sizeof cur) && (LONG)(numImage - camParImage) > 0) {
        for (int k = 0; k < 8; k++) camAff[k] = camAnc[k] + (camVrai[k] - camAnc[k]) * t;
        camAPoser = 1;
    }
}

typedef void (__cdecl *PrepCam_t)(void);
static PrepCam_t prepCamOrig;
static void __cdecl PrepCamera(void)
{
    CalculerCamera();
    if (camAPoser) {
        BYTE *cam = ObjetCamera();
        if (cam) { memcpy(cam + 0x84, camAff, sizeof camAff); cam[0xF0] = 1; camPosee = cam; camLissees++; }
    }
    prepCamOrig();
}

#define MAX_PALETTES 64
#define MAX_VRAIES   64
#define MAX_AVANT    64
#define PROCHE_OS    0.25f
#define ECART_MAX    1.5f
typedef struct { float os[768 * 4]; float anc[768 * 4]; float cam[3]; DWORD pasE[256]; int n, pris; } Palette;
typedef struct { void *tex; DWORD age; int n; float os[768 * 4]; } VraieTex;
typedef struct { void *tex; BYTE *bits; DWORD pitch; UINT larg, haut; } Verrou;
static Palette *palPrec, *palCour; static int nPalPrec, nPalCour;
static VraieTex *vraies; static DWORD ageVraies;
static float *avantTex; static void *avantObj[MAX_AVANT];
static Verrou verrous[64]; static int nVerrous;
static long osLisses, osRigides, palVues, palSansPartenaire, palRepliees, palRepliRot;
static long regDessins, regLisses, regRepliees;

static void EchangerPalettes(void)
{
    Palette *t = palPrec; palPrec = palCour; palCour = t; nPalPrec = nPalCour; nPalCour = 0;
    for (int i = 0; i < nPalPrec; i++) palPrec[i].pris = 0;
}
static VraieTex *TrouverVraie(void *t, int creer)
{
    if (!vraies) return NULL;
    int vieux = 0;
    for (int i = 0; i < MAX_VRAIES; i++) {
        if (vraies[i].tex == t) return &vraies[i];
        if (vraies[i].age < vraies[vieux].age) vieux = i;
    }
    if (!creer) return NULL;
    vraies[vieux].tex = t; vraies[vieux].n = 0; return &vraies[vieux];
}
static int InverseAffine(const float *m, float *inv)
{
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (det > -1e-8f && det < 1e-8f) return 0;
    const float k = 1.0f / det;
    inv[0] = A * k; inv[1] = -(b * i - c * h) * k; inv[2] = (b * f - c * e) * k;
    inv[4] = B * k; inv[5] = (a * i - c * g) * k; inv[6] = -(a * f - c * d) * k;
    inv[8] = C * k; inv[9] = -(a * h - b * g) * k; inv[10] = (a * e - b * d) * k;
    for (int r = 0; r < 3; r++) inv[r * 4 + 3] = -(inv[r * 4] * m[3] + inv[r * 4 + 1] * m[7] + inv[r * 4 + 2] * m[11]);
    return 1;
}
static void MulAffine(const float *x, const float *y, float *out)
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) out[r * 4 + c] = x[r * 4] * y[c] + x[r * 4 + 1] * y[4 + c] + x[r * 4 + 2] * y[8 + c];
        out[r * 4 + 3] = x[r * 4] * y[3] + x[r * 4 + 1] * y[7] + x[r * 4 + 2] * y[11] + x[r * 4 + 3];
    }
}

static int InterpRigide(const float *p0, const float *p1, float t, float *out)
{
    float inv0[12], d[12];
    if (!InverseAffine(p0, inv0)) return 0;
    MulAffine(p1, inv0, d);
    const float m00 = d[0], m01 = d[1], m02 = d[2], m10 = d[4], m11 = d[5], m12 = d[6], m20 = d[8], m21 = d[9], m22 = d[10];
    float w, x, y, z; const float tr = m00 + m11 + m22;
    if (tr > 0.0f) { const float k = sqrtf(tr + 1.0f) * 2.0f; w = 0.25f * k; x = (m21 - m12) / k; y = (m02 - m20) / k; z = (m10 - m01) / k; }
    else if (m00 > m11 && m00 > m22) { const float k = sqrtf(1.0f + m00 - m11 - m22) * 2.0f; w = (m21 - m12) / k; x = 0.25f * k; y = (m01 + m10) / k; z = (m02 + m20) / k; }
    else if (m11 > m22) { const float k = sqrtf(1.0f + m11 - m00 - m22) * 2.0f; w = (m02 - m20) / k; x = (m01 + m10) / k; y = 0.25f * k; z = (m12 + m21) / k; }
    else { const float k = sqrtf(1.0f + m22 - m00 - m11) * 2.0f; w = (m10 - m01) / k; x = (m02 + m20) / k; y = (m12 + m21) / k; z = 0.25f * k; }
    float n = sqrtf(w * w + x * x + y * y + z * z);
    if (!(n > 1e-6f)) return 0;
    w /= n; x /= n; y /= n; z /= n;
    if (w < 0.0f) { w = -w; x = -x; y = -y; z = -z; }
    if (w < 0.5f) return -1;
    const float sinDemi = sqrtf(x * x + y * y + z * z);
    if (sinDemi < 1e-5f) return 0;
    const float demi = atan2f(sinDemi, w) * t, k = sinf(demi) / sinDemi;
    w = cosf(demi); x *= k; y *= k; z *= k;
    const float r[12] = {
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), 0,
        2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), 0,
        2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), 0 };
    MulAffine(r, p0, out);
    for (int l = 0; l < 3; l++) out[l * 4 + 3] = p0[l * 4 + 3] + (p1[l * 4 + 3] - p0[l * 4 + 3]) * t;
    return 1;
}
static void InterpTexture(void *t)
{
    if (!palPrec || !palCour) return;
    int i = 0; while (i < nVerrous && verrous[i].tex != t) i++;
    if (i == nVerrous) return;
    const Verrou *v = &verrous[i];
    if (v->larg != 768 || v->haut != 1 || v->pitch < 768 * 16 || !v->bits) return;
    float *cur = (float *)v->bits;
    VraieTex *vt = TrouverVraie(t, 1);
    if (vt) { memcpy(vt->os, cur, sizeof vt->os); vt->age = ++ageVraies; }

    int nOs = -1;
    for (int j = 0; j < MAX_AVANT; j++) if (avantTex && avantObj[j] == t) {
        const float *av = avantTex + j * 768 * 4;
        nOs = 0;
        for (int o = 255; o >= 0 && !nOs; o--) if (memcmp(av + o * 12, cur + o * 12, 48)) nOs = o + 1;
        avantObj[j] = NULL; break;
    }
    if (nOs == 0 && vt && vt->n > 0) nOs = vt->n;
    if (nOs <= 0 || nPalCour >= MAX_PALETTES) { palSansPartenaire++; return; }
    if (vt) vt->n = nOs;
    palVues++;
    CalculerCamera();
    Palette *nouv = &palCour[nPalCour++];
    memcpy(nouv->os, cur, sizeof nouv->os);
    nouv->n = nOs; nouv->pris = 0;
    if (camValide) memcpy(nouv->cam, camVrai, sizeof nouv->cam); else memset(nouv->cam, 0, sizeof nouv->cam);
    memcpy(nouv->anc, cur, sizeof nouv->anc);
    for (int o = 0; o < 256; o++) { nouv->pasE[o] = pasAff; for (int r = 0; r < 3; r++) nouv->anc[o * 12 + r * 4 + 3] += nouv->cam[r]; }

    int meilleur = -1; float d1 = 1e30f, d2 = 1e30f;
    for (int j = 0; j < nPalPrec; j++) {
        if (palPrec[j].pris || palPrec[j].n != nOs) continue;
        float dd = 0.0f;
        for (int o = 0; o < nOs; o++)
            for (int k = 3; k < 12; k += 4) { const float e = nouv->os[o * 12 + k] + nouv->cam[k / 4] - palPrec[j].os[o * 12 + k] - palPrec[j].cam[k / 4]; dd += e < 0 ? -e : e; }
        dd /= (float)nOs;
        if (dd < d1) { d2 = d1; d1 = dd; meilleur = j; } else if (dd < d2) d2 = dd;
    }
    if (meilleur >= 0 && !(d1 < PROCHE_OS || (d1 < 0.5f * d2 && d1 < ECART_MAX))) meilleur = -1;
    if (meilleur < 0) { palSansPartenaire++; return; }
    Palette *p = &palPrec[meilleur]; p->pris = 1;

    int saut = 0;
    for (int o = 0; o < nOs; o++) {
        float a[12], b[12];
        memcpy(a, nouv->os + o * 12, 48); memcpy(b, p->os + o * 12, 48);
        for (int r = 0; r < 3; r++) { a[r * 4 + 3] += nouv->cam[r]; b[r * 4 + 3] += p->cam[r]; }
        float *anc = nouv->anc + o * 12;
        int chg = 0;
        for (int k = 0; k < 12; k++) { const float e = a[k] - b[k]; if (e != 0.0f) chg = 1; if (e > SEUIL_SAUT || e < -SEUIL_SAUT) saut = 1; }
        if (chg) { memcpy(anc, p->pasE[o] == pasAff ? a : b, 48); nouv->pasE[o] = pasAff; }
        else { memcpy(anc, p->anc + o * 12, 48); nouv->pasE[o] = p->pasE[o]; }
    }

    long nLisses = 0;
    if (!saut) for (int o = 0; o < nOs; o++) {
        float a[12];
        memcpy(a, nouv->os + o * 12, 48);
        for (int r = 0; r < 3; r++) a[r * 4 + 3] += nouv->cam[r];
        const float *anc = nouv->anc + o * 12;
        const float t = Alpha(nouv->pasE[o]);
        if (!(t < 1.0f) || !memcmp(anc, a, 48)) continue;
        float *dst = cur + o * 12;
        const int rig = InterpRigide(anc, a, t, dst);
        if (rig < 0) { saut = 1; palRepliRot++; break; }
        if (rig > 0) osRigides++;
        else for (int k = 0; k < 12; k++) dst[k] = anc[k] + (a[k] - anc[k]) * t;
        for (int r = 0; r < 3; r++) dst[r * 4 + 3] -= nouv->cam[r];
        nLisses++;
    }
    if (saut) {
        memcpy(cur, nouv->os, (size_t)nOs * 48);
        for (int o = 0; o < nOs; o++) {
            memcpy(nouv->anc + o * 12, nouv->os + o * 12, 48);
            for (int r = 0; r < 3; r++) nouv->anc[o * 12 + r * 4 + 3] += nouv->cam[r];
            nouv->pasE[o] = pasAff;
        }
        palRepliees++; nLisses = 0;
    }
    osLisses += nLisses;
}
static void JournalLissage(void)
{
    JournalHF("lissage camera sauts par_image", camLissees, camSauts, camParImages, 0);
    JournalHF("lissage palettes vues sans_partenaire repliees dont_rotation", palVues, palSansPartenaire, palRepliees, palRepliRot);
    JournalHF("lissage os lisses rigides", osLisses, osRigides, 0, 0);
    JournalHF("lissage registres dessins os_lisses replies", regDessins, regLisses, regRepliees, 0);
    camLissees = camSauts = camParImages = palVues = palSansPartenaire = palRepliees = palRepliRot = osLisses = osRigides = 0;
    regDessins = regLisses = regRepliees = 0;
}

typedef HRESULT (WINAPI *SetVSC_t)(IDirect3DDevice9 *, UINT, const float *, UINT);
typedef HRESULT (WINAPI *SetVS_t)(IDirect3DDevice9 *, IDirect3DVertexShader9 *);
typedef HRESULT (WINAPI *DIP_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (WINAPI *DP_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
static SetVSC_t oSetVSC; static SetVS_t oSetVS; static DIP_t oDIP; static DP_t oDP;
#define REG_OS 11
#define NB_REG (256 - REG_OS)
#define MAX_POSES 1024
static int interpRegistres = 1;
static void *shaderCourant; static void *shaders[512]; static int nShaders;
static float attente[NB_REG][4]; static BYTE masqueAttente[NB_REG]; static int nAttente, attRegMin, attRegMax;
typedef struct { DWORD cle; WORD rang, utilise; int debut, nb;
                 float regs[NB_REG][4];
                 float anc[NB_REG][4];
                 DWORD pasOs[NB_REG / 3 + 1]; float cam[3]; long votesRel, votesMonde; } Pose;
static Pose *poses; static int nPoses;
typedef struct { DWORD cle; WORD n; } Rang;
static Rang rangs[512]; static int nRangs;

static WORD NumShader(void *sh)
{
    for (int i = 0; i < nShaders; i++) if (shaders[i] == sh) return (WORD)i;
    if (nShaders < 512) { shaders[nShaders] = sh; return (WORD)nShaders++; }
    return 511;
}
static Pose *TrouverPose(DWORD cle, WORD rang)
{
    for (int i = 0; i < nPoses; i++) if (poses[i].cle == cle && poses[i].rang == rang) return &poses[i];
    if (nPoses >= MAX_POSES) return NULL;
    Pose *p = &poses[nPoses++]; memset(p, 0, sizeof *p); p->cle = cle; p->rang = rang; return p;
}
static WORD RangDuDessin(DWORD cle)
{
    for (int i = 0; i < nRangs; i++) if (rangs[i].cle == cle) return rangs[i].n++;
    if (nRangs < 512) { rangs[nRangs].cle = cle; rangs[nRangs].n = 1; nRangs++; }
    return 0;
}
static void EnvoyerPlages(IDirect3DDevice9 *d, float (*src)[4], const BYTE *masque)
{
    int r = 0;
    while (r < NB_REG) {
        if (!masque[r]) { r++; continue; }
        int f = r; while (f < NB_REG && masque[f]) f++;
        oSetVSC(d, (UINT)(REG_OS + r), &src[r][0], (UINT)(f - r));
        r = f;
    }
}

static int RelCamera(Pose *p, const float *cam)
{
    float de = 0.0f;
    for (int k = 0; k < 3; k++) { const float e = cam[k] - p->cam[k]; de += e < 0 ? -e : e; }
    if (de > 1e-3f) {
        float sans = 0.0f, avec = 0.0f;
        for (int k = 0; k < 3; k++) {
            const float dr = attente[p->debut + k][3] - p->regs[p->debut + k][3];
            const float da = dr + cam[k] - p->cam[k];
            sans += dr < 0 ? -dr : dr; avec += da < 0 ? -da : da;
        }
        if (avec < 0.5f * sans) p->votesRel++;
        else if (sans < 0.5f * avec) p->votesMonde++;
        if (p->votesRel + p->votesMonde > 1000) { p->votesRel /= 2; p->votesMonde /= 2; }
    }
    return p->votesRel > p->votesMonde;
}

static void InterpAvantDessin(IDirect3DDevice9 *d, int dessin)
{
    if (!nAttente) return;
    static float envoi[NB_REG][4];
    const int debut = attRegMin - REG_OS, nb = attRegMax - attRegMin;
    memcpy(envoi[debut], attente[debut], (size_t)nb * 16);
    int bloc = dessin && nb >= 24 && nb % 3 == 0;
    for (int r = debut; bloc && r < debut + nb; r++) if (!masqueAttente[r]) bloc = 0;
    Pose *p = NULL;
    if (bloc) {
        const DWORD cle = ((DWORD)NumShader(shaderCourant) << 16) ^ ((DWORD)attRegMin << 8) ^ (DWORD)attRegMax;
        p = TrouverPose(cle, RangDuDessin(cle));
    }
    if (p) {
        CalculerCamera();
        float cam[3]; if (camValide) memcpy(cam, camVrai, sizeof cam); else memset(cam, 0, sizeof cam);
        const int nOs = nb / 3;
        regDessins++;
        if (!p->utilise || p->debut != debut || p->nb != nb) {
            p->debut = debut; p->nb = nb; p->votesRel = p->votesMonde = 0;
            for (int o = 0; o < nOs; o++) p->pasOs[o] = pasAff;
            for (int r = debut; r < debut + nb; r++) memcpy(p->anc[r], attente[r], 16);
        } else {
            const int rel = RelCamera(p, cam);

            int saut = 0;
            for (int o = 0; o < nOs; o++) {
                const int r0 = debut + o * 3;
                float a[12], b[12];
                for (int k = 0; k < 3; k++) {
                    memcpy(a + k * 4, attente[r0 + k], 16); memcpy(b + k * 4, p->regs[r0 + k], 16);
                    if (rel) { a[k * 4 + 3] += cam[k]; b[k * 4 + 3] += p->cam[k]; }
                }
                int chg = 0;
                for (int k = 0; k < 12; k++) { const float e = a[k] - b[k]; if (e != 0.0f) chg = 1; if (e > SEUIL_SAUT || e < -SEUIL_SAUT) saut = 1; }
                if (chg) { memcpy(p->anc[r0], p->pasOs[o] == pasAff ? a : b, 48); p->pasOs[o] = pasAff; }
            }

            long nLisses = 0;
            if (!saut) for (int o = 0; o < nOs; o++) {
                const int r0 = debut + o * 3;
                float a[12];
                for (int k = 0; k < 3; k++) { memcpy(a + k * 4, attente[r0 + k], 16); if (rel) a[k * 4 + 3] += cam[k]; }
                const float *anc = &p->anc[r0][0];
                const float t = Alpha(p->pasOs[o]);
                if (!(t < 1.0f) || !memcmp(anc, a, 48)) continue;
                float *dst = &envoi[r0][0];
                const int rig = InterpRigide(anc, a, t, dst);
                if (rig < 0) { saut = 1; break; }
                if (rig == 0) for (int k = 0; k < 12; k++) dst[k] = anc[k] + (a[k] - anc[k]) * t;
                if (rel) for (int k = 0; k < 3; k++) dst[k * 4 + 3] -= cam[k];
                nLisses++;
            }
            if (saut) {
                memcpy(envoi[debut], attente[debut], (size_t)nb * 16);
                for (int o = 0; o < nOs; o++) {
                    const int r0 = debut + o * 3;
                    for (int k = 0; k < 3; k++) { memcpy(p->anc[r0 + k], attente[r0 + k], 16); if (rel) p->anc[r0 + k][3] += cam[k]; }
                    p->pasOs[o] = pasAff;
                }
                regRepliees++; nLisses = 0;
            }
            regLisses += nLisses;
        }
        for (int r = debut; r < debut + nb; r++) memcpy(p->regs[r], attente[r], 16);
        memcpy(p->cam, cam, sizeof cam); p->utilise = 1;
    }
    EnvoyerPlages(d, envoi, masqueAttente);
    memset(masqueAttente, 0, sizeof masqueAttente); nAttente = 0;
}

static HRESULT WINAPI hkSetVSC(IDirect3DDevice9 *d, UINT reg, const float *v, UINT nb)
{
    if (!v || !interpRegistres || !InterpEnService() || reg + nb <= REG_OS || reg + nb > 256) {
        if (nAttente && v && reg + nb > REG_OS) InterpAvantDessin(d, 0);
        return oSetVSC(d, reg, v, nb);
    }
    if (reg < REG_OS) { oSetVSC(d, reg, v, REG_OS - reg); v += (REG_OS - reg) * 4; nb -= REG_OS - reg; reg = REG_OS; }
    for (UINT k = 0; k < nb; k++) { const int r = (int)(reg + k) - REG_OS; memcpy(attente[r], v + k * 4, 16); masqueAttente[r] = 1; }
    if (!nAttente) { attRegMin = (int)reg; attRegMax = (int)(reg + nb); }
    if ((int)reg < attRegMin) attRegMin = (int)reg;
    if ((int)(reg + nb) > attRegMax) attRegMax = (int)(reg + nb);
    nAttente++;
    return D3D_OK;
}
static HRESULT WINAPI hkSetVS(IDirect3DDevice9 *d, IDirect3DVertexShader9 *sh) { shaderCourant = sh; return oSetVS(d, sh); }
static HRESULT WINAPI hkDIP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT si, UINT pc) { InterpAvantDessin(d, 1); return oDIP(d, t, b, mi, nv, si, pc); }
static HRESULT WINAPI hkDP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT s, UINT pc) { InterpAvantDessin(d, 1); return oDP(d, t, s, pc); }

typedef HRESULT (WINAPI *PresentD_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (WINAPI *CreateDev_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (WINAPI *CreateTex_t)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *);
typedef HRESULT (WINAPI *LockTex_t)(IDirect3DTexture9 *, UINT, D3DLOCKED_RECT *, const RECT *, DWORD);
typedef HRESULT (WINAPI *UnlockTex_t)(IDirect3DTexture9 *, UINT);
typedef HRESULT (WINAPI *DescTex_t)(IDirect3DTexture9 *, UINT, D3DSURFACE_DESC *);
static PresentD_t oPresent; static CreateDev_t oCreate; static CreateTex_t oCreateTex;
#define MAX_TABLES 8
static void **tablesTex[MAX_TABLES]; static LockTex_t locksTex[MAX_TABLES]; static UnlockTex_t unlocksTex[MAX_TABLES]; static int nTablesTex;

static void *Remplacer(void **vt, int i, void *nouveau)
{
    DWORD old; void *ancien = vt[i];
    if (ancien == nouveau || !VirtualProtect(&vt[i], 4, PAGE_EXECUTE_READWRITE, &old)) return ancien;
    for (;;) {
        ancien = vt[i];
        if (ancien == nouveau) break;
        if (InterlockedCompareExchangePointer(&vt[i], nouveau, ancien) == ancien) break;
    }
    VirtualProtect(&vt[i], 4, old, &old); return ancien;
}
static HRESULT WINAPI hkLockTex(IDirect3DTexture9 *t, UINT niv, D3DLOCKED_RECT *r, const RECT *rc, DWORD f)
{
    void **vt = *(void ***)t; LockTex_t orig = NULL;
    for (int i = 0; i < nTablesTex; i++) if (tablesTex[i] == vt) orig = locksTex[i];
    if (!orig) return D3DERR_INVALIDCALL;
    const HRESULT hr = orig(t, niv, r, rc, f);
    if (!InterpEnService() || FAILED(hr) || !r || !r->pBits) return hr;
    D3DSURFACE_DESC ds; memset(&ds, 0, sizeof ds);
    ((DescTex_t)(*(void ***)t)[17])(t, niv, &ds);
    const int flottant = ds.Format == D3DFMT_A32B32G32R32F || ds.Format == D3DFMT_A16B16G16R16F || ds.Format == D3DFMT_R32F ||
                         ds.Format == D3DFMT_G32R32F || ds.Format == D3DFMT_G16R16F || ds.Format == D3DFMT_R16F;
    if (!flottant || ds.Width != 768 || ds.Height != 1 || r->Pitch < 768 * 16) return hr;
    VraieTex *vr = TrouverVraie(t, 0);
    if (vr && !rc) memcpy(r->pBits, vr->os, sizeof vr->os);
    if (avantTex && !rc) {
        int k = -1;
        for (int j = 0; j < MAX_AVANT; j++) { if (avantObj[j] == t) { k = j; break; } if (k < 0 && !avantObj[j]) k = j; }
        if (k >= 0) { memcpy(avantTex + k * 768 * 4, r->pBits, 768 * 16); avantObj[k] = t; }
    }
    if (nVerrous < 64) { Verrou *v = &verrous[nVerrous++]; v->tex = t; v->bits = (BYTE *)r->pBits; v->pitch = (DWORD)r->Pitch; v->larg = ds.Width; v->haut = ds.Height; }
    return hr;
}
static HRESULT WINAPI hkUnlockTex(IDirect3DTexture9 *t, UINT niv)
{
    void **vt = *(void ***)t; UnlockTex_t orig = NULL;
    for (int i = 0; i < nTablesTex; i++) if (tablesTex[i] == vt) orig = unlocksTex[i];
    if (!orig) return D3DERR_INVALIDCALL;
    if (InterpEnService()) InterpTexture(t);
    for (int j = 0; j < MAX_AVANT; j++) if (avantObj[j] == (void *)t) avantObj[j] = NULL;
    for (int i = 0; i < nVerrous; i++) if (verrous[i].tex == (void *)t) { verrous[i] = verrous[--nVerrous]; break; }
    return orig(t, niv);
}
static HRESULT WINAPI hkCreateTex(IDirect3DDevice9 *d, UINT w, UINT h, UINT niv, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9 **out, HANDLE *hd)
{
    const HRESULT hr = oCreateTex(d, w, h, niv, usage, fmt, pool, out, hd);
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = *(void ***)*out; int connu = 0;
        for (int i = 0; i < nTablesTex; i++) if (tablesTex[i] == vt) connu = 1;
        if (!connu && nTablesTex < MAX_TABLES && vt[19] != (void *)hkLockTex) {
            tablesTex[nTablesTex] = vt; locksTex[nTablesTex] = (LockTex_t)vt[19]; unlocksTex[nTablesTex] = (UnlockTex_t)vt[20]; nTablesTex++;
            Remplacer(vt, 19, (void *)hkLockTex); Remplacer(vt, 20, (void *)hkUnlockTex);
        }
    }
    return hr;
}
static HRESULT WINAPI hkPresent(IDirect3DDevice9 *d, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
    InterpAvantDessin(d, 0); nRangs = 0;
    if (camPosee) { memcpy(camPosee + 0x84, camVrai, sizeof camVrai); camPosee[0xF0] = 1; camPosee = NULL; }
    numImage++;
    EchangerPalettes();
    return oPresent(d, a, b, w, r);
}
static void BrancherDevice(IDirect3DDevice9 *dev, int source)
{
    static volatile LONG branche;
    if (oPresent || InterlockedExchange(&branche, 1)) return;
    void **vt = *(void ***)dev;
    oPresent = (PresentD_t)Remplacer(vt, 17, (void *)hkPresent);
    oCreateTex = (CreateTex_t)Remplacer(vt, 23, (void *)hkCreateTex);
    oDP = (DP_t)Remplacer(vt, 81, (void *)hkDP);
    oDIP = (DIP_t)Remplacer(vt, 82, (void *)hkDIP);
    oSetVS = (SetVS_t)Remplacer(vt, 92, (void *)hkSetVS);
    oSetVSC = (SetVSC_t)Remplacer(vt, 94, (void *)hkSetVSC);
    JournalHF("affichage_accroche source", source, 0, 0, 0);
}
static HRESULT WINAPI hkCreate(IDirect3D9 *d3d, UINT a, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    const HRESULT hr = oCreate(d3d, a, t, w, f, pp, out);
    if (SUCCEEDED(hr) && out && *out && t == D3DDEVTYPE_HAL) BrancherDevice(*out, 1);
    return hr;
}
static void SurDeviceHF(IDirect3DDevice9 *d) { BrancherDevice(d, 3); }
static void InstallerAffichage(void)
{
    void **vt = D3D9_Methodes();
    if (!vt) { JournalHF("REFUS_d3d9", 0, 0, 0, 0); return; }
    oCreate = (CreateDev_t)Remplacer(vt, 16, (void *)hkCreate);
    D3D9_SurDevice((SurDevice_t)SurDeviceHF);
}

static void SecoursAffichage(void)
{
    if (oPresent) return;
    HMODULE m = GetModuleHandleA("d3d9.dll");
    IDirect3D9 *(WINAPI *creer)(UINT) = m ? (IDirect3D9 *(WINAPI *)(UINT))GetProcAddress(m, "Direct3DCreate9") : NULL;
    IDirect3D9 *d3d = creer ? creer(D3D_SDK_VERSION) : NULL;
    if (!d3d) return;
    HWND w = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    D3DPRESENT_PARAMETERS pp; memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = w; pp.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9 *dev = NULL;
    CreateDev_t creerDev = oCreate ? oCreate : (CreateDev_t)(*(void ***)d3d)[16];
    const HRESULT hr = creerDev(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (SUCCEEDED(hr) && dev) { BrancherDevice(dev, 2); IDirect3DDevice9_Release(dev); }
    IDirect3D9_Release(d3d);
    if (w) DestroyWindow(w);
}

typedef struct { DWORD site, cible; void *nouveau; void **orig; } Appel;
static BYTE *Cible(DWORD site) { BYTE *s = base + site; return s[0] == 0xE8 ? s + 5 + *(LONG *)(s + 1) : NULL; }
static BOOL Ecrire(void *ou, const void *quoi, SIZE_T n)
{
    DWORD old;
    if (!VirtualProtect(ou, n, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy(ou, quoi, n);
    VirtualProtect(ou, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), ou, n);
    return TRUE;
}
static int Installer(void)
{
    Appel appels[] = {
        {0x3F4AA7, 0x43B7F0, (void *)LireManettes, (void **)&lireOrig},
        {0x3F4AF7, 0x423420, (void *)NbPasBoucle, (void **)&nbPasOrig},
        {0x3F4BB5, 0x333750, (void *)VitesseImage, (void **)&vitesseOrig},
        {0x3F4BDC, 0x333750, (void *)VitesseImage, NULL},
        {0x3F4CF4, 0x388870, (void *)MajDt, (void **)&majDtOrig},
        {0x36C105, 0x37B1B0, (void *)PrepCamera, (void **)&prepCamOrig},
    };
    const int nAppels = (int)(sizeof appels / sizeof appels[0]);
    static const BYTE limiteur[] = {0xDC, 0x35};

    for (int i = 0; i < nAppels; i++) if (Cible(appels[i].site) != base + appels[i].cible) return -(i + 1);
    if (memcmp(base + 0x823798, limiteur, 2) || *(DWORD *)(base + 0x82379A) != (DWORD)(ULONG_PTR)(base + 0xC231F0)) return -8;
    static const BYTE setter[] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xA3};
    if (menus60 && (memcmp(base + 0x423330, setter, sizeof setter) || *(DWORD *)(base + 0x423337) != (DWORD)(ULONG_PTR)(base + RVA_MODE_FPS))) return -9;
    modeJeu = (LONG)*(DWORD *)(base + RVA_MODE_FPS);
    for (int i = 0; i < nAppels; i++) if (appels[i].orig) *appels[i].orig = base + appels[i].cible;
    for (int i = 0; i < nAppels; i++) {
        const LONG rel = (LONG)((BYTE *)appels[i].nouveau - (base + appels[i].site + 5));
        if (!Ecrire(base + appels[i].site + 1, &rel, 4)) return -10 - i;
    }
    const DWORD adr = (DWORD)(ULONG_PTR)&diviseur;
    if (!Ecrire(base + 0x82379A, &adr, 4)) return -20;
    if (menus60) {
        BYTE saut[5] = {0xE9};
        const LONG relSaut = (LONG)((BYTE *)FiltreMode - (base + 0x423330 + 5));
        memcpy(saut + 1, &relSaut, 4);
        if (!Ecrire(base + 0x423330, saut, 5)) return -21;
    }
    return 1;
}

static DWORD WINAPI Demarrer(LPVOID param)
{
    (void)param;
    limiteFps = (double)Reglage("HighFps", "Fps", 120);
    if (limiteFps != 0.0 && (limiteFps < 60.0 || limiteFps > 360.0)) limiteFps = 120.0;
    touche = Reglage("HighFps", "Touche", VK_F3);
    if (touche <= 0 || touche > 0xFE) touche = VK_F3;
    interpActive = Reglage("HighFps", "Interpolation", 1) != 0;
    interpRegistres = Reglage("HighFps", "InterpRegistres", 1) != 0;
    menus60 = Reglage("HighFps", "Menus", 1) != 0;
    scenesEnLigne = Reglage("HighFps", "ScenesEnLigne", 0) != 0;
    effetsLisses = Reglage("HighFps", "EffetsLisses", 1) != 0;
    g_t0 = GetTickCount(); debutReleve = g_t0;
    QueryPerformanceFrequency(&freq);
    base = (BYTE *)GetModuleHandleW(NULL);
    JournalHF("demarrage version_2.10 fps touche interpolation registres_menus_effets_scenesenligne", (long)limiteFps, touche, interpActive, interpRegistres * 1000 + menus60 * 100 + effetsLisses * 10 + scenesEnLigne);
    if (interpActive) {
        palPrec = (Palette *)VirtualAlloc(NULL, sizeof(Palette) * MAX_PALETTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        palCour = (Palette *)VirtualAlloc(NULL, sizeof(Palette) * MAX_PALETTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        avantTex = (float *)VirtualAlloc(NULL, (SIZE_T)MAX_AVANT * 768 * 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        vraies = (VraieTex *)VirtualAlloc(NULL, sizeof(VraieTex) * MAX_VRAIES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        poses = (Pose *)VirtualAlloc(NULL, sizeof(Pose) * MAX_POSES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!palPrec || !palCour || !avantTex || !vraies || !poses) { interpActive = 0; JournalHF("REFUS_memoire", 0, 0, 0, 0); }
        else InstallerAffichage();
    }
    int r = 0;
    for (int essai = 0; essai < 1200; essai++) {
        r = Installer();
        if (r == 1 || r <= -10) break;
        Sleep(50);
    }
    JournalHF(r == 1 ? "pose_ok" : "REFUS_pose", r, 0, 0, 0);
    if (r != 1) return 0;
    BOOL avant = FALSE, secoursFait = FALSE;
    for (;;) {
        Sleep(30);
        DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        const BOOL appui = pid == GetCurrentProcessId() && (GetAsyncKeyState(touche) & 0x8000);
        if (appui && !avant) { actifDemande = !actifDemande; JournalHF("touche actif_demande", actifDemande, 0, 0, 0); }
        avant = appui;
        if (interpActive && !secoursFait && GetTickCount() - g_t0 > 10000) { secoursFait = TRUE; SecoursAffichage(); }
    }
    return 0;
}

static BOOL ModuleSepare(void)
{
    char p[MAX_PATH]; const DWORD n = GetModuleFileNameA(NULL, p, MAX_PATH);
    char *s = n && n < MAX_PATH ? strrchr(p, '\\') : NULL;
    if (!s || (size_t)(s + 1 - p) + 40 > MAX_PATH) return FALSE;
    strcpy(s + 1, "scripts\\DOA5LR-HighFps.asi"); if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) return TRUE;
    strcpy(s + 1, "DOA5LR-HighFps.asi"); return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}
void HighFps_Demarrer(void)
{
    if (GetModuleHandleA("DOA5LR-HighFps.asi") || ModuleSepare()) { Journal("HighFps", "module_separe_present", 0, 0, 0); return; }
    HANDLE t = CreateThread(NULL, 0, Demarrer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

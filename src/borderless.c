/*  DOA5LR-Borderless 2.0 (01/10/2026) - plein ecran sans bordures, sans passer par les options du jeu.
 *
 *  Reprise minimale de Borderless 1.1 (projet DOA5Tools). Aucun crochet dans le jeu : on attend la fenetre
 *  principale du processus, on retire cadre et barre de titre, et on la cale sur son moniteur (re-applique si
 *  le jeu recree ou redimensionne sa fenetre).
 *
 *  DOA5LR-Borderless.ini, section [Borderless] :
 *   Mode=2      : sans bordures (defaut). Au lancement, SCREEN_TYPE=WINDOW est impose dans le reglage du jeu
 *                 (Documents\KoeiTecmo\DOA5LR\DOA5LR.ini) AVANT que le jeu ne le lise, puis les bordures sont retirees.
 *   Mode=1      : fenetre classique imposee (WINDOW, bordures conservees).
 *   Mode=0      : le module ne touche a rien (reglage du jeu).
 *   ToggleKey=F11 (0 = aucune) : en jeu, Sans bordures (1 bip) -> Fenetre (2 bips) -> Plein ecran (3 bips,
 *                 applique au prochain lancement) -> ... ; le mode choisi est enregistre dans ce .ini.
 *
 *  Fichiers : lit et ecrit ce .ini et, pour la seule cle SCREEN_TYPE, DOA5LR.ini du jeu. Rien d'autre.
 *  Retire depuis 1.1 : journal, bandeau a l'ecran (les bips restent), option de test IniPath.
 *
 *  Compilation (LLVM-MinGW, 32 bits) :
 *    i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -o DOA5LR-Borderless.asi borderless.c -lshell32
 */
/*  Version integree a DOA5Tools : reglages [Borderless] de DOA5Tools.ini (Mode y est enregistre). */
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "commun.h"

static HWND g_hwnd;
static int g_mode = 2;                     /* 0 = inactif, 1 = fenetre, 2 = sans bordures */
static int g_toggleKey = VK_F11;
static WCHAR g_pluginIni[MAX_PATH], g_gameIni[MAX_PATH];
static LONG g_origStyle, g_origEx;
static RECT g_origRect;
static int g_origSaved;

static const LONG kStrip   = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME;
static const LONG kStripEx = WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE;

/* fenetre principale du jeu : visible, sans parent, pas une fenetre outil, au moins 320 x 240 */
static BOOL CALLBACK TrouverFenetre(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetParent(h)) return TRUE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    RECT c;
    GetClientRect(h, &c);
    if (c.right < 320 || c.bottom < 240) return TRUE;
    *(HWND *)lp = h;
    return FALSE;
}

static void SansBordures(HWND h)
{
    const LONG st = GetWindowLongW(h, GWL_STYLE), ex = GetWindowLongW(h, GWL_EXSTYLE);
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT voulu = mi.rcMonitor, actuel;
    GetWindowRect(h, &actuel);
    const BOOL styleOk = !(st & kStrip) && !(ex & kStripEx);
    if (styleOk && EqualRect(&voulu, &actuel)) return;
    if (!g_origSaved && (st & kStrip)) { g_origStyle = st; g_origEx = ex; g_origRect = actuel; g_origSaved = 1; }
    if (!styleOk) {
        SetWindowLongW(h, GWL_STYLE, (st & ~kStrip) | WS_POPUP | WS_VISIBLE);
        SetWindowLongW(h, GWL_EXSTYLE, ex & ~kStripEx);
    }
    SetWindowPos(h, NULL, voulu.left, voulu.top, voulu.right - voulu.left, voulu.bottom - voulu.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}
static void RemettreBordures(HWND h)       /* fenetre classique, taille d'origine */
{
    if (!h || !g_origSaved) return;
    SetWindowLongW(h, GWL_STYLE, g_origStyle | WS_VISIBLE);
    SetWindowLongW(h, GWL_EXSTYLE, g_origEx);
    const RECT r = g_origRect;
    SetWindowPos(h, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

/* Remplace la valeur de SCREEN_TYPE= dans DOA5LR.ini (ANSI ou UTF-16 LE, fins de ligne conservees).
 * Rien d'autre n'est modifie ; abandon si le fichier contient des caracteres hors Latin-1 (UTF-16). */
static void ImposerTypeEcran(const WCHAR *ini, const char *valeur)
{
    HANDLE h = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD taille = GetFileSize(h, NULL), lu = 0;
    if (taille == INVALID_FILE_SIZE || taille > 65536) { CloseHandle(h); return; }
    HANDLE tas = GetProcessHeap();
    BYTE *brut = (BYTE *)HeapAlloc(tas, 0, taille + 2);
    char *txt = (char *)HeapAlloc(tas, 0, taille + 2), *sortie = NULL;
    if (!brut || !txt) goto fin_lecture;
    if (!ReadFile(h, brut, taille, &lu, NULL)) goto fin_lecture;
    CloseHandle(h); h = INVALID_HANDLE_VALUE;
    const int large = lu >= 2 && brut[0] == 0xFF && brut[1] == 0xFE;
    DWORD n;
    if (large) {
        n = (lu - 2) / 2;
        for (DWORD i = 0; i < n; i++) {
            if (brut[3 + 2 * i]) goto fin;                       /* caractere hors Latin-1 : on ne touche a rien */
            txt[i] = (char)brut[2 + 2 * i];
        }
    } else { n = lu; memcpy(txt, brut, n); }
    txt[n] = 0;
    char *k = strstr(txt, "SCREEN_TYPE=");
    if (!k) goto fin;
    char *v = k + 12, *e = v;
    while (*e && *e != '\r' && *e != '\n') e++;
    const size_t lv = strlen(valeur);
    if ((size_t)(e - v) == lv && !memcmp(v, valeur, lv)) goto fin;   /* deja bon */
    const DWORD n2 = (DWORD)((v - txt) + lv + strlen(e));
    sortie = (char *)HeapAlloc(tas, 0, n2 + 1);
    if (!sortie) goto fin;
    memcpy(sortie, txt, v - txt);
    memcpy(sortie + (v - txt), valeur, lv);
    strcpy(sortie + (v - txt) + lv, e);
    h = CreateFileW(ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        if (large) {
            const BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(h, bom, 2, &w, NULL);
            for (DWORD i = 0; i < n2; i++) { const WCHAR c = (unsigned char)sortie[i]; WriteFile(h, &c, 2, &w, NULL); }
        } else WriteFile(h, sortie, n2, &w, NULL);
    }
fin_lecture:
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
fin:
    if (sortie) HeapFree(tas, 0, sortie);
    if (txt) HeapFree(tas, 0, txt);
    if (brut) HeapFree(tas, 0, brut);
}

static void LireReglages(void)
{
    MultiByteToWideChar(CP_ACP, 0, CheminIni(), -1, g_pluginIni, MAX_PATH);   /* DOA5Tools.ini */
    g_mode = GetPrivateProfileIntW(L"Borderless", L"Mode", 2, g_pluginIni);
    if (g_mode < 0 || g_mode > 2) g_mode = 2;
    WCHAR touche[16];
    GetPrivateProfileStringW(L"Borderless", L"ToggleKey", L"F11", touche, 16, g_pluginIni);
    if (touche[0] == L'F' || touche[0] == L'f') {
        const int f = _wtoi(touche + 1);
        g_toggleKey = (f >= 1 && f <= 24) ? VK_F1 + f - 1 : 0;
    } else g_toggleKey = _wtoi(touche);
    if (SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, g_gameIni) != S_OK) { g_gameIni[0] = 0; return; }
    wcsncat(g_gameIni, L"\\KoeiTecmo\\DOA5LR\\DOA5LR.ini", MAX_PATH - wcslen(g_gameIni) - 1);
}

static void ChangerMode(int m)
{
    g_mode = m;
    WCHAR v[4];
    wsprintfW(v, L"%d", m);
    WritePrivateProfileStringW(L"Borderless", L"Mode", v, g_pluginIni);
    if (g_gameIni[0]) ImposerTypeEcran(g_gameIni, m == 0 ? "FULLSCREEN" : "WINDOW");
    if (m == 2) { if (g_hwnd) SansBordures(g_hwnd); } else RemettreBordures(g_hwnd);
    for (int i = 0; i < (m == 2 ? 1 : m == 1 ? 2 : 3); i++) { MessageBeep(MB_OK); Sleep(120); }
}
static void LireTouche(void)
{
    static int avant;
    if (!g_toggleKey) return;
    const int bas = (GetAsyncKeyState(g_toggleKey) & 0x8000) != 0;
    if (bas && !avant) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId()) ChangerMode(g_mode == 2 ? 1 : g_mode == 1 ? 0 : 2);
    }
    avant = bas;
}

static DWORD WINAPI Boucle(LPVOID p)
{
    (void)p;
    for (;;) {
        if (!g_hwnd || !IsWindow(g_hwnd)) { g_hwnd = NULL; EnumWindows(TrouverFenetre, (LPARAM)&g_hwnd); }
        if (g_hwnd && g_mode == 2) SansBordures(g_hwnd);
        for (int i = 0; i < (g_hwnd ? 8 : 2); i++) { Sleep(125); LireTouche(); }
    }
    return 0;
}

void Borderless_Demarrer(void)
{
    LireReglages();
    if (g_mode >= 1 && g_gameIni[0]) ImposerTypeEcran(g_gameIni, "WINDOW");   /* avant que le jeu ne le lise */
    HANDLE t = CreateThread(NULL, 0, Boucle, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

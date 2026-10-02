/*  DOA5LR-Rematch 1.0 (01/10/2026) - rematch en salon avec le menu d'apres-combat natif du jeu.
 *
 *  Dans un salon dont la file compte 2 joueurs (rotation gagnant reste, perdant reste ou Kumite), le menu
 *  d'apres-combat (MenuBattleEnd) s'affiche. Si les deux choisissent Rejouer : nouveau combat direct, memes
 *  joueurs, par la scene Rematch du jeu. Sinon, ou si un 3e joueur entre dans la file : salon normal.
 *  Les DEUX joueurs du combat doivent avoir le module. Aucun reseau, aucun appel Steam, aucun fichier
 *  autre que le .ini (lecture) et le journal (ecriture, facultatif).
 *
 *  1.0  : = 0.24-test validee en jeu, journal desactive par defaut (Log=0).
 *  0.24 : 0.23 validee sauf le menu reduit : retirer une entree de la definition decalait l'affichage
 *         (ligne fantome, actions decalees d'un cran). Retrait abandonne : l'entree "Search Menu" (code 0x51,
 *         libelle 0x800010) affiche maintenant le libelle "Quit" du jeu (0x800003, celui du menu hors ligne)
 *         (LibelleQuit=1). Sonde des libelles retiree. DelaiRematch=0 par defaut (valide sur les deux machines).
 *  0.23 : version nettoyee de la 0.22 (validee en jeu) : observations retirees, journal minimal, Tirage=1.
 *  Historique des essais 0.1 a 0.22 : voir les notes de developpement.
 *
 *  Cible : game.exe 1.10C (SHA-256 88d12d42...). Adresses = RVA.
 *
 *  Ce que fait le module (appels E8 rel32 ou entrees de vtable redirigees, apres verification des octets) :
 *   - fin de combat (CStateWinPose, CStateResult) : en mode rematch, pas de table du salon, menu 14 ouvert ;
 *   - MenuBattleEnd : disposition 0x35 (rematch en ligne natif) au lieu de "pas de menu" ;
 *   - rotation de fin de match (51E000, hote seul) differee jusqu'a la decision : faite si pas de rematch,
 *     abandonnee sinon. Pour le jeu, l'hote est le joueur de la fiche 0 (P1) : la rotation peut le changer ;
 *   - InGameLobby_TraiterChoix : tous Rejouer -> l'hote demande l'action 10 (scene Rematch) ;
 *   - entree de InGameLobby : pas de table ni de chat si le rematch est decide ;
 *   - scene Rematch (hote) : attente a l'etape 1, etape 2 (nouveau tirage) sautee si Tirage=0, attente de la
 *     session prete a l'etape 3 (sinon l'action 6 n'est jamais envoyee : defaut du jeu en salon) ;
 *   - stage Random verrouille sur le stage joue si Tirage=0, remis ensuite.
 *
 *  Journal (Log=1, 0 par defaut) : DOA5LR-Rematch.log a cote du module. Libelles fixes et nombres du jeu uniquement
 *  (fonction Note) : aucun ID, pseudo, chemin, heure reelle, pointeur ni texte venu du jeu. 64 Ko au plus.
 *
 *  Compilation (LLVM-MinGW, 32 bits) :
 *    "<bin>\i686-w64-mingw32-gcc.exe" -O2 -s -shared -static -Wall -o DOA5LR-Rematch.asi rematch.c
 *  Installation : DOA5LR-Rematch.asi (+ DOA5LR-Rematch.ini facultatif) dans <jeu>\scripts\.
 */
/*  Version integree a DOA5Tools : reglages [Rematch] de DOA5Tools.ini, journal commun. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "commun.h"

#define VERSION_MODULE "1.0"

/* ---- adresses (game.exe 1.10C, RVA) ------------------------------------------------------- */
#define ATTENDU_DATE    0x5a1faa36
#define ATTENDU_TAILLE  0x021f8000
#define RVA_APPEL_DISPO     0x69A647   /* MenuBattleEnd_Init : call MenuBattleEnd_ChoisirDisposition */
#define RVA_DISPO           0x699FA0
#define RVA_ETIQUETTE       0x69A320   /* MenuBattleEnd_Etiquette : thiscall (menu, code) -> id de libelle */
#define RVA_APPEL_CHOIX     0x521C0C   /* InGameLobby_Suivante : call InGameLobby_TraiterChoix */
#define RVA_CHOIX           0x520FE0
#define RVA_TYPE_MATCH      0xF818A0   /* g_TypeMatchOnline : 3 = salon */
#define RVA_DECISION        0x207EDD0  /* g_DecisionApresCombat : 0 = tous Rejouer */
#define RVA_SESSION_OBTENIR 0x0C8B90
#define RVA_EST_HOTE        0x507F90   /* thiscall (session, -1) : joueur de la fiche 0 */
#define RVA_NB_JOUEURS      0x5066A0   /* fastcall (session) */
#define RVA_ACCORDS         0xF7A1A0   /* +0x78 + i*2 : [0] entree choisie, [1] a choisi */
#define RVA_ACTION_OBTENIR  0x0CDEC0
#define RVA_ACTION_DEMANDER 0x5290F0   /* thiscall (g_ActionSalon, action) : +0xC8, previent les autres */
#define RVA_ACTION_VALIDER  0x528720   /* thiscall (g_ActionSalon, action) : +0xC4 et +0xC8 */
#define RVA_VT_RESULTAT     0x9C1034   /* CStateResult, vtable secondaire, entree 0 */
#define RVA_RESULTAT_SUITE  0x417C80
#define RVA_CREER_SELECTMENU 0x417A00  /* fastcall (etat - 0x18) : cree CStateSelectMenu -> MenuBattleEnd */
#define RVA_SESSION_FIN     0x529520   /* thiscall (objet F7A238) : != 0 -> pas de menu (comme le jeu) */
#define RVA_OBJ_SESSION_FIN 0xF7A238
#define RVA_GARDE_SESSION_FIN 0xF7D9F0 /* bit 0 : objet F7A238 deja construit */
#define RVA_APPEL_PREP_TABLE 0x418CBC  /* CStateWinPose : call 0D9790(0x32, 0) */
#define RVA_PREP_MENU        0x0D9790
#define RVA_APPEL_OUVR_TABLE 0x418CC3  /* CStateWinPose : call 0D9890(0x32) */
#define RVA_OUVRIR_MENU      0x0D9890
#define RVA_APPEL_PREP_LOBBY 0x5215E4  /* InGameLobby (entree) : call 0D9790(0x32, 0) */
#define RVA_APPEL_OUVR_LOBBY 0x5215EB  /* InGameLobby : call 0D9890(0x32) */
#define RVA_APPEL_CHAT_LOBBY 0x5215F7  /* InGameLobby : call 4F06C0(0x6E, 0x19) = chat du salon */
#define RVA_AFFICHER_CALQUE  0x0F06C0
#define RVA_VT_REMATCH_SUIVANTE 0xBE2A30  /* vt_GameSceneRematch + 3*4 */
#define RVA_REMATCH_SUIVANTE    0x1D1260  /* SceneRematch_Suivante */
#define RVA_PARAM_COMBAT    0xF8A8B0   /* g_ParamCombat : +0x74 stage Random */
#define RVA_REGLE_ROTATION  0xF81764   /* 0 gagnant reste, 1 perdant reste, 2 tournoi, 5 kumite, 6 dojo */
#define RVA_STAGE_MENU      0xF83D64   /* g_StageMenu : stage choisi (74 = Random) */
#define RVA_STAGE_RETENU    0xFCDBDE   /* g_StageRetenu : stage joue (octet) */
#define RVA_APPEL_ROTATION  0x51E495   /* FinMatch_Etats (etat 1) : call Rotation_FinDeMatch */
#define RVA_ROTATION        0x51E000
#define ID_MENU_BATTLEEND   0x0E
#define DISPO_EN_LIGNE      0x35
#define CODE_SEARCH_MENU    0x51       /* entree "Search Menu" (choix 3) */
#define LIBELLE_QUIT        0x800003   /* "Quit" : libelle du code 0x51 hors ligne */
#define ACTION_REMATCH      10

/* debuts de fonctions verifies avant redirection */
static const uint8_t PRO_CADRE[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};        /* push ebp ; mov ebp,esp ; push -1 */
static const uint8_t PRO_ETIQUETTE[] = {0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x08};
static const DWORD APPELS_ETIQUETTE[] = {0x69B694, 0x69B7DC, 0x69CD05, 0x69E615, 0x69E707};

typedef DWORD (__cdecl *Dispo_t)(void);
typedef int (__fastcall *Etat_t)(void *self);
typedef void *(__cdecl *Obtenir_t)(void);
typedef uint8_t (__thiscall *EstHote_t)(void *session, int membre);
typedef uint8_t (__fastcall *NbJoueurs_t)(void *session);
typedef void (__thiscall *Action_t)(void *action, int valeur);
typedef uint8_t (__thiscall *SessionFin_t)(void *obj);
typedef void (__cdecl *PrepMenu_t)(int id, int option);
typedef void (__cdecl *OuvrirMenu_t)(int id);
typedef void (__cdecl *AfficherCalque_t)(int calque, int id);
typedef void (__fastcall *Rotation_t)(void *self);
typedef int (__thiscall *MenuEntier_t)(void *menu, int valeur);

static uint8_t *g_base;
static Dispo_t o_dispo;
static Etat_t o_choix, o_resultat, o_rematchSuivante;
static PrepMenu_t o_prepTable;
static OuvrirMenu_t o_ouvrTable;
static AfficherCalque_t o_chatLobby;
static Rotation_t o_rotation;
static MenuEntier_t o_etiquette;

/* ---- reglages (.ini) ---------------------------------------------------------------------- */
static int g_actif = 1;
static int g_delaiRematch = 0;     /* DelaiRematch : attente de l'hote a l'entree de la scene Rematch (ms) */
static int g_tirage = 1;           /* Tirage : 1 nouveau tirage des Random a chaque rematch, 0 memes persos/stage */
static int g_libelleQuit = 1;      /* LibelleQuit : "Quit" au lieu de "Search Menu" */

static volatile LONG g_menuForce;  /* 1 : le menu de ce combat a recu la disposition forcee */

/* ---- journal commun (doa5tools.c) ---- */
static void Note(const char *libelle, long a, long b, long c) { Journal("Rematch", libelle, a, b, c); }

/* ---- minuteurs ---------------------------------------------------------------------------- */
typedef struct { int actif; DWORD debut; } Minuteur;
static int Demarrer(Minuteur *m)          /* 1 si l'attente vient de commencer */
{
    if (m->actif) return 0;
    m->actif = 1; m->debut = GetTickCount();
    return 1;
}
static DWORD Ecoule(const Minuteur *m) { return m->actif ? GetTickCount() - m->debut : 0; }
static void Arreter(Minuteur *m) { m->actif = 0; }

/* ---- etat du jeu -------------------------------------------------------------------------- */
static int EnSalon(void) { return *(volatile uint32_t *)(g_base + RVA_TYPE_MATCH) == 3; }
static void *Session(void) { return ((Obtenir_t)(g_base + RVA_SESSION_OBTENIR))(); }
static int EstHote(void) { return ((EstHote_t)(g_base + RVA_EST_HOTE))(Session(), -1) != 0; }
static int NbJoueurs(void) { return ((NbJoueurs_t)(g_base + RVA_NB_JOUEURS))(Session()); }
static int AChoisi(int i) { return g_base[RVA_ACCORDS + 0x78 + i * 2 + 1] != 0; }
static int Entree(int i) { return g_base[RVA_ACCORDS + 0x78 + i * 2]; }   /* index dans le menu (0 = Rejouer) */

/* Mode rematch : salon, 2 joueurs dans la file, rotation dont la fin de match passe par InGameLobby
 * (0 gagnant reste, 1 perdant reste, 5 kumite ; pas 2 tournoi ni 6 dojo). */
static int ModeRematch(void)
{
    if (!g_actif || !EnSalon() || NbJoueurs() != 2) return 0;
    const uint32_t regle = *(volatile uint32_t *)(g_base + RVA_REGLE_ROTATION);
    return regle == 0 || regle == 1 || regle == 5;
}
/* Rematch decide : menu force, tous ont choisi Rejouer (entree 0) */
static int RematchDecide(void)
{
    if (!g_actif || !EnSalon() || !g_menuForce) return 0;
    if (*(volatile uint32_t *)(g_base + RVA_DECISION) != 0) return 0;
    const int n = NbJoueurs();
    if (n < 2) return 0;
    for (int i = 0; i < n && i < 2; ++i) if (!AChoisi(i) || Entree(i) != 0) return 0;
    return 1;
}

/* ---- stage Random verrouille (Tirage=0) : le stage joue remplace le choix Random, remis ensuite ----- */
static int g_stageVerrouille;
static uint32_t g_stageMenuOrigine;
static uint8_t g_stageRandomOrigine;
static void VerrouillerStage(void)
{
    uint8_t *pc = g_base + RVA_PARAM_COMBAT;
    if (g_stageVerrouille || !pc[0x74]) return;
    g_stageMenuOrigine = *(volatile uint32_t *)(g_base + RVA_STAGE_MENU);
    g_stageRandomOrigine = pc[0x74];
    *(volatile uint32_t *)(g_base + RVA_STAGE_MENU) = g_base[RVA_STAGE_RETENU];
    pc[0x74] = 0;
    g_stageVerrouille = 1;
}
static void RestaurerStage(void)
{
    if (!g_stageVerrouille) return;
    *(volatile uint32_t *)(g_base + RVA_STAGE_MENU) = g_stageMenuOrigine;
    g_base[RVA_PARAM_COMBAT + 0x74] = g_stageRandomOrigine;
    g_stageVerrouille = 0;
}

/* ---- rotation de fin de match differee (hote) ----------------------------------------------
 * 51E000 ne lit qu'un octet de son objet (+0x40, cote gagnant) : il est garde dans un objet de remplacement. */
static uint8_t g_objetRotation[0x44];
static int g_rotationDifferee;
static void __fastcall hk_rotation(void *self)
{
    if (ModeRematch()) {
        g_objetRotation[0x40] = ((const uint8_t *)self)[0x40];
        g_rotationDifferee = 1;
        return;
    }
    g_rotationDifferee = 0;
    o_rotation(self);
}
static void FaireRotationDifferee(void)
{
    if (!g_rotationDifferee) return;
    g_rotationDifferee = 0;
    o_rotation(g_objetRotation);
    Note("rotation_faite", 0, 0, 0);
}

/* ---- fin de combat : pas de table du salon (CStateWinPose), menu 14 (CStateResult) ---------- */
static void __cdecl hk_prepTable(int id, int option)
{
    if (ModeRematch()) return;
    o_prepTable(id, option);
}
static void __cdecl hk_ouvrTable(int id)
{
    if (ModeRematch()) return;
    RestaurerStage();
    o_ouvrTable(id);
}
static int __fastcall hk_resultat(void *self)
{
    if (ModeRematch() && (g_base[RVA_GARDE_SESSION_FIN] & 1) &&
        !((SessionFin_t)(g_base + RVA_SESSION_FIN))(g_base + RVA_OBJ_SESSION_FIN)) {
        ((PrepMenu_t)(g_base + RVA_PREP_MENU))(ID_MENU_BATTLEEND, 0);   /* le menu 14 n'est pas charge en salon */
        return ((Etat_t)(g_base + RVA_CREER_SELECTMENU))((uint8_t *)self - 0x18);
    }
    return o_resultat(self);
}

/* ---- menu d'apres-combat ------------------------------------------------------------------ */
static DWORD __cdecl hk_dispo(void)
{
    DWORD d = o_dispo();
    if (g_actif && EnSalon() && d == 0xFFFFFFFF) {
        d = DISPO_EN_LIGNE;
        InterlockedExchange(&g_menuForce, 1);
        Note("menu", NbJoueurs(), 0, 0);
    }
    return d;
}
/* LibelleQuit : dans le menu force, l'entree "Search Menu" (code 0x51 ; en salon, meme effet que la selection)
 * affiche le libelle "Quit" du jeu. Retirer l'entree de la definition decalait l'affichage (0.23). */
static int __fastcall hk_etiquette(void *menu, void *edx, int code)
{
    (void)edx;
    if (g_libelleQuit && g_menuForce && code == CODE_SEARCH_MENU &&
        *(int32_t *)((uint8_t *)menu + 0x2F54) == DISPO_EN_LIGNE) return LIBELLE_QUIT;
    return o_etiquette(menu, code);
}

/* ---- decision (InGameLobby_TraiterChoix, a chaque image tant que la scene n'a pas decide) ---- */
#define ATTENTE_MAX_MS 30000
static Minuteur g_attenteChoix;
static int __fastcall hk_choix(void *scene)
{
    if (!g_actif || !EnSalon() || !g_menuForce || ((uint8_t *)scene)[0xC]) {
        FaireRotationDifferee();                                 /* menu absent : jeu normal */
        return o_choix(scene);
    }
    const uint32_t decision = *(volatile uint32_t *)(g_base + RVA_DECISION);
    const int n = NbJoueurs(), hote = EstHote();
    int tous = n >= 2, quitte = n < 2;
    for (int i = 0; i < n && i < 2; ++i) {
        if (!AChoisi(i)) tous = 0;
        else if (Entree(i) != 0) quitte = 1;                     /* un joueur ne rejoue pas */
    }
    if (!tous && !quitte) {                                      /* menu ouvert : on attend les choix */
        Demarrer(&g_attenteChoix);
        if (Ecoule(&g_attenteChoix) < ATTENTE_MAX_MS) return 1;
        Note("attente_depassee", 0, 0, 0);
    }
    Arreter(&g_attenteChoix);
    InterlockedExchange(&g_menuForce, 0);
    const int rematch = decision == 0 && tous && !quitte;
    Note("decision rematch/hote/entrees", rematch, hote, Entree(0) * 10 + Entree(1));
    if (!rematch) {                                              /* jeu normal */
        RestaurerStage();
        FaireRotationDifferee();
        return o_choix(scene);
    }
    if (!g_tirage) VerrouillerStage();
    g_rotationDifferee = 0;                                      /* rematch : pas de rotation */
    if (hote) {
        /* comme la branche salon du jeu : Demander puis Valider (Valider ecrit l'action courante, +0xC4) */
        void *action = ((Obtenir_t)(g_base + RVA_ACTION_OBTENIR))();
        ((Action_t)(g_base + RVA_ACTION_DEMANDER))(action, ACTION_REMATCH);
        ((Action_t)(g_base + RVA_ACTION_VALIDER))(action, ACTION_REMATCH);
    }
    ((uint8_t *)scene)[0xC] = 1;                                 /* comme le jeu : decision prise */
    return 1;
}

/* ---- entree de InGameLobby : pas de table ni de chat par-dessus le combat relance ------------ */
static void __cdecl hk_prepTableLobby(int id, int option)
{
    if (RematchDecide()) return;
    o_prepTable(id, option);
}
static void __cdecl hk_ouvrTableLobby(int id)
{
    if (RematchDecide()) return;
    RestaurerStage();
    o_ouvrTable(id);
}
static void __cdecl hk_chatLobby(int calque, int id)
{
    if (RematchDecide()) return;
    o_chatLobby(calque, id);
}

/* ---- scene Rematch (entree 3 de sa vtable, a chaque image), cote hote ----------------------- */
static Minuteur g_attenteEtape1, g_attenteEtape3;
static int __fastcall hk_rematchSuivante(void *scene)
{
    volatile uint32_t *etape = (volatile uint32_t *)((uint8_t *)scene + 0xC);
    const int hote = g_actif && EnSalon() && EstHote();
    /* etape 1 : laisser l'autre joueur entrer dans la scene avant d'avancer (sinon l'action 6 est perdue) */
    if (hote && *etape == 1) {
        Demarrer(&g_attenteEtape1);
        if (Ecoule(&g_attenteEtape1) < (DWORD)g_delaiRematch) return 1;
    } else Arreter(&g_attenteEtape1);
    /* etape 2 : nouveau tirage des Random (persos et stage) puis envoi de la session ; sautee si Tirage=0 */
    if (hote && *etape == 2 && !g_tirage) { *etape = 3; return 1; }
    /* etape 3 : defaut du jeu (1D146A) : si la session n'est pas prete (Session+0xCC), l'action 6 n'est pas
     * envoyee mais l'etape 4 la valide quand meme. L'hote attend la session prete (10 s au plus). */
    if (hote && *etape == 3 && !((volatile uint8_t *)Session())[0xCC]) {
        Demarrer(&g_attenteEtape3);
        if (Ecoule(&g_attenteEtape3) < 10000) return 1;
        Note("session_jamais_prete", 0, 0, 0);
    }
    Arreter(&g_attenteEtape3);
    return o_rematchSuivante(scene);
}

/* ---- redirection d'un call E8 rel32, apres verification de la cible et de son debut ---------- */
static int Rediriger(DWORD rvaAppel, DWORD rvaCible, const uint8_t *debut, size_t taille, void *crochet, void **original)
{
    uint8_t *appel = g_base + rvaAppel, *cible = g_base + rvaCible;
    if (appel[0] != 0xE8) return 0;
    if (appel + 5 + *(int32_t *)(appel + 1) != cible) return 0;
    if (memcmp(cible, debut, taille)) return 0;
    DWORD ancien, ig;
    if (!VirtualProtect(appel, 5, PAGE_EXECUTE_READWRITE, &ancien)) return 0;
    *original = cible;
    *(int32_t *)(appel + 1) = (int32_t)((uint8_t *)crochet - (appel + 5));
    VirtualProtect(appel, 5, ancien, &ig);
    FlushInstructionCache(GetCurrentProcess(), appel, 5);
    return 1;
}
#define REDIRIGER(appel, cible, debut, crochet, orig) \
    Rediriger(appel, cible, debut, sizeof debut, (void *)(crochet), (void **)&(orig))

/* ---- remplacement d'une entree de vtable, apres verification de sa valeur ------------------ */
static int RemplacerEntree(DWORD rvaEntree, DWORD rvaAttendue, void *crochet, void **original)
{
    void **entree = (void **)(g_base + rvaEntree);
    if (*entree != (void *)(g_base + rvaAttendue)) return 0;
    DWORD ancien, ig;
    if (!VirtualProtect(entree, sizeof(void *), PAGE_READWRITE, &ancien)) return 0;
    *original = *entree;
    InterlockedExchange((volatile LONG *)entree, (LONG)(uintptr_t)crochet);
    VirtualProtect(entree, sizeof(void *), ancien, &ig);
    return 1;
}

static int VersionOk(void)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(g_base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp == ATTENDU_DATE && nt->OptionalHeader.SizeOfImage == ATTENDU_TAILLE;
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    if (!VersionOk()) { Note("refus_version_game_exe", 0, 0, 0); return 0; }
    /* le code est dechiffre par SteamStub au demarrage : on attend que les octets attendus soient la */
    int pret = 0;
    for (int t = 0; t < 600 && !pret; t++) {
        pret = g_base[RVA_APPEL_DISPO] == 0xE8 && !memcmp(g_base + RVA_DISPO, PRO_CADRE, sizeof PRO_CADRE);
        if (!pret) Sleep(100);
    }
    if (!pret) { Note("refus_code_inattendu", 0, 0, 0); return 0; }
    /* indispensables : tout ou rien, dans l'ordre */
    int ok = REDIRIGER(RVA_APPEL_DISPO, RVA_DISPO, PRO_CADRE, hk_dispo, o_dispo)
          && REDIRIGER(RVA_APPEL_CHOIX, RVA_CHOIX, PRO_CADRE, hk_choix, o_choix)
          && !memcmp(g_base + RVA_CREER_SELECTMENU, PRO_CADRE, sizeof PRO_CADRE)
          && RemplacerEntree(RVA_VT_RESULTAT, RVA_RESULTAT_SUITE, (void *)hk_resultat, (void **)&o_resultat)
          && REDIRIGER(RVA_APPEL_PREP_TABLE, RVA_PREP_MENU, PRO_CADRE, hk_prepTable, o_prepTable)
          && REDIRIGER(RVA_APPEL_OUVR_TABLE, RVA_OUVRIR_MENU, PRO_CADRE, hk_ouvrTable, o_ouvrTable)
          && REDIRIGER(RVA_APPEL_PREP_LOBBY, RVA_PREP_MENU, PRO_CADRE, hk_prepTableLobby, o_prepTable)
          && REDIRIGER(RVA_APPEL_OUVR_LOBBY, RVA_OUVRIR_MENU, PRO_CADRE, hk_ouvrTableLobby, o_ouvrTable)
          && REDIRIGER(RVA_APPEL_CHAT_LOBBY, RVA_AFFICHER_CALQUE, PRO_CADRE, hk_chatLobby, o_chatLobby)
          && RemplacerEntree(RVA_VT_REMATCH_SUIVANTE, RVA_REMATCH_SUIVANTE, (void *)hk_rematchSuivante, (void **)&o_rematchSuivante)
          && REDIRIGER(RVA_APPEL_ROTATION, RVA_ROTATION, PRO_CADRE, hk_rotation, o_rotation);
    /* confort : libelle "Quit" (5 appels de MenuBattleEnd_Etiquette) */
    int etiq = 0;
    if (ok && g_libelleQuit)
        for (size_t i = 0; i < sizeof APPELS_ETIQUETTE / sizeof APPELS_ETIQUETTE[0]; ++i)
            etiq += REDIRIGER(APPELS_ETIQUETTE[i], RVA_ETIQUETTE, PRO_ETIQUETTE, hk_etiquette, o_etiquette);
    Note("pret module/libelles", ok, etiq, 0);
    Note("reglages tirage/delai/libelle_quit", g_tirage, g_delaiRematch, g_libelleQuit);
    return 0;
}

void Rematch_Demarrer(void)
{
    g_delaiRematch = Reglage("Rematch", "DelaiRematch", 0);
    if (g_delaiRematch < 0 || g_delaiRematch > 10000) g_delaiRematch = 0;
    g_tirage = Reglage("Rematch", "Tirage", 1) != 0;
    g_libelleQuit = Reglage("Rematch", "LibelleQuit", 1) != 0;
    Note("debut " VERSION_MODULE, 0, 0, 0);
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

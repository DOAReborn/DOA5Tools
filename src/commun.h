#ifndef DOA5TOOLS_COMMUN_H
#define DOA5TOOLS_COMMUN_H
#include <windows.h>
#include <stdint.h>

void Journal(const char *module, const char *libelle, long a, long b, long c);

int Reglage(const char *section, const char *cle, int defaut);

const char *CheminIni(void);

void **D3D9_Methodes(void);

struct IDirect3DDevice9;
typedef void (*SurDevice_t)(struct IDirect3DDevice9 *dev);
void D3D9_SurDevice(SurDevice_t f);

typedef void (*Tic_t)(void);
void AjouterTic(Tic_t f);

void Lobby_Demarrer(void);
void JoinFix_Demarrer(void);
void Rematch_Demarrer(void);
void WiFiWired_Demarrer(void);
void Borderless_Demarrer(void);
void Skip_Demarrer(void);
void Ultrawide_Demarrer(void);
void ReplayMenu_Demarrer(void);
void LobbyChat_Demarrer(void);
void StageSlots_Demarrer(void);
void VersionTitre_Demarrer(void);
void IntroOutroSkip_Demarrer(void);
void Fiche_Demarrer(void);
void LobbyName_Demarrer(void);
void HighFps_Demarrer(void);
void AoTFix_Demarrer(void);
void ConsoleRestart_Demarrer(void);
void ArcadeConsole_Demarrer(void);
void ConnFilter_Demarrer(void);
void ModOptions_Demarrer(void);
void Gamepad_Demarrer(void);

int Clavier_NbActions(void);
const char *Clavier_Action(int i);
int Clavier_Touche(int i);
void Clavier_NomTouche(int dik, char *texte, int taille);
int Clavier_Definir(int i, int dik);
void Clavier_Capturer(int i);
int Clavier_EnCapture(void);
void Clavier_ParDefaut(void);

uint8_t WiFiWired_Type(void);
int StageSlots_MapsActives(void);
int StageSlots_Packs(void);
int Marque_TousNiveaux(void);
int Marque_TousDOA5Tools(void);
int Marque_DuMembre(int id);
int Marque_TousCorrectifs(void);
void Fiche_Correctifs(int actifs);

extern const wchar_t TEXTE_VERSION_DOA5TOOLS[];

#endif

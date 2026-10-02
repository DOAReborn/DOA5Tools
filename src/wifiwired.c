/*  DOA5LR-WiFi-Wired 1.0 (02/10/2026) - type de connexion reel (cable / Wi-Fi) dans la fiche joueur du salon.
 *
 *  Reprise minimale de WiFi-Wired-Detector 0.8.8 (projet DOA5Tools). Cible : game.exe 1.10C.
 *  La fiche joueur envoyee aux autres (NameCardPacket, type 0x13) contient un octet "type de connexion" (+0x117 :
 *  1 = cable [n], 2 = sans fil <n>). Sur PC, le jeu y met toujours 1. Le module y met le vrai type, juste avant
 *  l'envoi (crochet sur NameCardPacket::Serialize, entree 2 de sa vtable BE22E4). Les autres joueurs voient donc
 *  le bon tag ; chacun voit celui des autres s'ils ont aussi le module.
 *
 *  Donnee (lues en memoire seulement) : le type de connexion de la machine, un seul bit,
 *  deduit au lancement du type de la carte reseau active qui porte la passerelle (Ethernet = cable, 802.11 = Wi-Fi).
 *  Aucune adresse, aucun nom de carte ni identifiant n'est garde ; rien n'est ecrit dans un journal.
 *  Retire depuis 0.8.8 : echanges HELLO, redondance, relais, ping, statistiques, journaux, sonde du salon, correction
 *  de sa propre fiche locale (lisait le pseudo Steam), LinkOverride.
 *
 *  Compilation (LLVM-MinGW, 32 bits) :
 *    i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -o DOA5LR-WiFi-Wired.asi wifiwired.c -liphlpapi
 */
/*  Version integree a DOA5Tools : [DOA5Tools] WiFiWired=0 pour couper. Pas de journal. */
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "commun.h"

#define RVA_NCP_VTABLE      0xBE22E4   /* vtable NameCardPacket */
#define RVA_NCP_SERIALIZE   0x515540   /* entree 2 : Serialize(stream), thiscall, ret 4 */
#define NCP_TYPE_CONNEXION  0x117      /* octet du paquet : 1 = cable, 2 = sans fil */

static volatile uint8_t g_type = 1;    /* cable par defaut, comme le jeu */

/* type de la carte active qui porte une passerelle : Wi-Fi si l'une d'elles est 802.11, sinon cable */
static uint8_t DetecterType(void)
{
    const ULONG drapeaux = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST |
                           GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
    ULONG taille = 0;
    uint8_t type = 1;
    if (GetAdaptersAddresses(AF_UNSPEC, drapeaux, NULL, NULL, &taille) != ERROR_BUFFER_OVERFLOW || !taille) return type;
    IP_ADAPTER_ADDRESSES *liste = (IP_ADAPTER_ADDRESSES *)malloc(taille);
    if (liste && GetAdaptersAddresses(AF_UNSPEC, drapeaux, NULL, liste, &taille) == NO_ERROR)
        for (IP_ADAPTER_ADDRESSES *a = liste; a; a = a->Next)
            if (a->OperStatus == IfOperStatusUp && a->FirstGatewayAddress && a->IfType == IF_TYPE_IEEE80211) { type = 2; break; }
    if (liste) { SecureZeroMemory(liste, taille); free(liste); }
    return type;
}

typedef uint8_t (__thiscall *Serialiser_t)(void *paquet, void *flux);
static Serialiser_t o_serialiser;
static uint8_t __fastcall hk_serialiser(void *paquet, void *edx, void *flux)
{
    (void)edx;
    ((uint8_t *)paquet)[NCP_TYPE_CONNEXION] = g_type;
    return o_serialiser(paquet, flux);
}

static DWORD WINAPI Installer(LPVOID p)
{
    (void)p;
    g_type = DetecterType();
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    void **slot = (void **)(base + RVA_NCP_VTABLE) + 2;
    for (int t = 0; t < 600 && *slot != (void *)(base + RVA_NCP_SERIALIZE); t++) Sleep(100);   /* SteamStub */
    if (*slot != (void *)(base + RVA_NCP_SERIALIZE)) return 0;                                /* autre version */
    DWORD ancien;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &ancien)) return 0;
    o_serialiser = (Serialiser_t)*slot;
    InterlockedExchange((volatile LONG *)slot, (LONG)(uintptr_t)hk_serialiser);
    VirtualProtect(slot, sizeof(void *), ancien, &ancien);
    return 0;
}

void WiFiWired_Demarrer(void)
{
    HANDLE t = CreateThread(NULL, 0, Installer, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

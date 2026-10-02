#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "commun.h"

#define RVA_NCP_VTABLE      0xBE22E4
#define RVA_NCP_SERIALIZE   0x515540
#define NCP_TYPE_CONNEXION  0x117

static volatile uint8_t g_type = 1;

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
    for (int t = 0; t < 600 && *slot != (void *)(base + RVA_NCP_SERIALIZE); t++) Sleep(100);
    if (*slot != (void *)(base + RVA_NCP_SERIALIZE)) return 0;
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

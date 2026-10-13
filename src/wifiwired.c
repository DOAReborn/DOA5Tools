#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "commun.h"

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

static volatile LONG g_actif;
uint8_t WiFiWired_Type(void) { return g_actif ? g_type : 0; }

static DWORD WINAPI Detecter(LPVOID p)
{
    (void)p;
    g_type = DetecterType();
    InterlockedExchange(&g_actif, 1);
    return 0;
}

void WiFiWired_Demarrer(void)
{
    HANDLE t = CreateThread(NULL, 0, Detecter, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

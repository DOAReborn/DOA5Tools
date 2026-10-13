#ifndef ACCROCHE_H
#define ACCROCHE_H
#include <windows.h>
#include <string.h>

static inline void ac_rw(void *p,SIZE_T n){DWORD o;VirtualProtect(p,n,PAGE_EXECUTE_READWRITE,&o);}

static inline void *ac_slot(void **slot,void *hook,void *volatile *suite) {
    ac_rw(slot,sizeof(void*));
    for(;;) {
        void *cur=*(void *volatile*)slot;
        if(cur==hook)return NULL;
        *suite=cur;
        if(InterlockedCompareExchangePointer(slot,hook,cur)==cur)return cur;
    }
}

static inline BOOL ac_foreign(const void *p) {
    MEMORY_BASIC_INFORMATION mi;HMODULE game=GetModuleHandleW(NULL);
    if(!p || !VirtualQuery(p,&mi,sizeof mi) || mi.State!=MEM_COMMIT)return FALSE;
    return (HMODULE)mi.AllocationBase!=game;
}

static inline void *ac_detour(BYTE *t,const BYTE *pro,int len,void *hook,void *volatile *suite) {
    if(len<5 || len>16)return NULL;
    ac_rw(t,8);
    for(;;) {
        LONG64 old=*(volatile LONG64*)t;BYTE cur[8];memcpy(cur,&old,8);
        void *next;
        if(!memcmp(t,pro,len)) {
            BYTE *tr=VirtualAlloc(NULL,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);if(!tr)return NULL;
            memcpy(tr,t,len);tr[len]=0xe9;*(LONG*)(tr+len+1)=(LONG)((t+len)-(tr+len+5));
            next=tr;
        } else if(cur[0]==0xe9) {
            next=t+5+*(LONG*)(cur+1);
            if(next==hook)return NULL;
            if(!ac_foreign(next))return NULL;
        } else return NULL;
        BYTE nw[8];memcpy(nw,cur,8);nw[0]=0xe9;*(LONG*)(nw+1)=(LONG)((BYTE*)hook-(t+5));
        LONG64 v;memcpy(&v,nw,8);*suite=next;
        if(InterlockedCompareExchange64((volatile LONG64*)t,v,old)==old){FlushInstructionCache(GetCurrentProcess(),t,8);return next;}

    }
}

static inline void *ac_call(BYTE *site,const void *expect,void *hook,void *volatile *suite) {
    if(site[0]!=0xe8)return NULL;
    ac_rw(site,5);
    for(;;) {
        LONG cur=*(volatile LONG*)(site+1);BYTE *tgt=site+5+cur;
        if((void*)tgt==hook)return NULL;
        if((void*)tgt!=expect && !ac_foreign(tgt))return NULL;
        LONG nw=(LONG)((BYTE*)hook-(site+5));*suite=tgt;
        if(InterlockedCompareExchange((volatile LONG*)(site+1),nw,cur)==cur){FlushInstructionCache(GetCurrentProcess(),site,5);return tgt;}
    }
}
#endif

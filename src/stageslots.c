#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "commun.h"

#define NOTE(libelle,a,b,c) Journal("StageSlots",libelle,(long)(a),(long)(b),(long)(c))
#define VERSION_STAGESLOTS "1.3"

typedef struct {const WCHAR *section,*cle,*valeur;} Reglage_t;
static const Reglage_t CONFIG[]={
    {L"Tiles",L"Tile25",L"0x2b|1|bonus"},
    {L"Tiles",L"Tile26",L"0x2c|2|bonus"},
    {L"Tiles",L"Tile27",L"0x12|1|bonus"},
    {L"Tiles",L"Tile28",L"0x43|2|bonus"},
    {L"Tiles",L"Tile29",L"0x1c|1|bonus"},
    {L"Stages",L"0x2b",L"#0x1b0026|danger_zone.raw8"},
    {L"Stages",L"0x2c",L"#0x1b0027|crimson.raw8"},
    {L"Stages",L"0x2d",L"#0x1b0027|crimson_2.raw8"},
    {L"Stages",L"0x12",L"FIREWORKS|fireworks.raw8"},
    {L"Stages",L"0x17",L"AZUCHI|azuchi.raw8"},
    {L"Stages",L"0x43",L"AZUCHI|azuchi.raw8"},
    {L"Stages",L"0x44",L"AZUCHI|azuchi_2.raw8"},
    {L"Aliases",L"0x43",L"0x17"},
    {L"Aliases",L"0x44",L"0x20"},
    {L"Stages",L"0x1c",L"ATTACK ON TITAN|attack_on_titan.raw8"},
    {L"Advanced",L"ExtraStages",L"0x12,0x17,0x1c,0x20"},
    {NULL,NULL,NULL}
};
static DWORD cfg_string(const WCHAR *section,const WCHAR *cle,const WCHAR *defaut,WCHAR *sortie,DWORD n,const WCHAR *fichier) {
    (void)fichier;
    if(!n)return 0;
    if(!cle) {
        DWORD k=0;
        for(const Reglage_t *r=CONFIG;r->section;r++) {
            if(lstrcmpiW(r->section,section))continue;
            DWORD l=lstrlenW(r->cle)+1;
            if(k+l+1>n)break;
            memcpy(sortie+k,r->cle,l*sizeof(WCHAR));k+=l;
        }
        sortie[k]=0;if(k+1<n)sortie[k+1]=0;
        return k;
    }
    const WCHAR *v=defaut?defaut:L"";
    if(!lstrcmpiW(section,L"General") && !lstrcmpiW(cle,L"HideGroups"))
        v=Reglage("StageSlots","Extra",1)?L"":L"bonus";
    else for(const Reglage_t *r=CONFIG;r->section;r++)
        if(!lstrcmpiW(r->section,section) && !lstrcmpiW(r->cle,cle)){v=r->valeur;break;}
    lstrcpynW(sortie,v,(int)n);
    return (DWORD)lstrlenW(sortie);
}
static UINT cfg_int(const WCHAR *section,const WCHAR *cle,INT defaut,const WCHAR *fichier) {
    (void)fichier;
    if(!lstrcmpiW(section,L"General") && !lstrcmpiW(cle,L"RememberToggle"))return 1;
    return (UINT)defaut;
}

static BOOL cfg_write(const WCHAR *section,const WCHAR *cle,const WCHAR *valeur,const WCHAR *fichier) {
    (void)fichier;
    if(lstrcmpiW(section,L"General") || lstrcmpiW(cle,L"HideGroups"))return FALSE;
    return WritePrivateProfileStringA("StageSlots","Extra",valeur && valeur[0]?"0":"1",CheminIni());
}
#define GetPrivateProfileStringW cfg_string
#define GetPrivateProfileIntW cfg_int
#define WritePrivateProfileStringW cfg_write

static BYTE *base;
static WCHAR ini[MAX_PATH],folder[MAX_PATH];

#define RVA(va) (base+((va)-0x400000))
#define GUI ((volatile DWORD*)RVA(0x138a7ac))

static BOOL readmem(const void *p,void *buf,SIZE_T n) {
    SIZE_T done=0;
    return ReadProcessMemory(GetCurrentProcess(),p,buf,n,&done) && done==n;
}
static DWORD rd32(const void *p) {DWORD v=0;readmem(p,&v,4);return v;}

static BOOL en_ligne_jeu(void) {
    BYTE b=0;
    if(readmem(RVA(0x1383d80),&b,1) && (b&1))return TRUE;
    if(!readmem(RVA(0x1382884),&b,1) || !(b&1))return FALSE;
    BYTE flags=0,f9d=0;DWORD fa8=0;BYTE *o=RVA(0x1381820);
    if(!readmem(o+0xa64,&flags,1) || !readmem(o+0xfa8,&fa8,4) || !readmem(o+0xf9d,&f9d,1))return TRUE;
    return (flags&8) && (fa8==0 || f9d==0);
}
static BOOL en_ligne_brut(void) {
    return rd32(RVA(0x138f208))!=0 || rd32(RVA(0x1381898+4))!=0 || en_ligne_jeu();
}

static BOOL online_session(void) {return en_ligne_brut() && !Marque_TousNiveaux();}
static double ini_float(const WCHAR *section,const WCHAR *key,double def) {
    WCHAR buf[32];
    GetPrivateProfileStringW(section,key,L"",buf,32,ini);
    return buf[0]?wcstod(buf,NULL):def;
}

static const BYTE dispatch_signature[17]={0x80,0x79,0x6a,0,0x74,0x0a,0x8b,1,0x8b,0x90,0x84,0,0,0,0xff,0xe2,0xc3};

#define TILE_COUNT 25
#define EXT_COUNT 27
#define MAX_TILES 48
#define TILE_INDEX    ((DWORD*)RVA(0x10107f0))
#define TILE_VARIANTS ((BYTE*)RVA(0x101076c))

#define FNV_INDEX         0x4EB96862u
#define FNV_VARIANTES     0xE1C5E0EBu
#define FNV_VARIANTES_LAB 0x8CB1FDA2u
static DWORD original_index[TILE_COUNT];
static BYTE original_variants[TILE_COUNT];
static DWORD fnv1a(const void *p,size_t n){const BYTE *b=p;DWORD h=0x811C9DC5u;for(size_t i=0;i<n;i++){h^=b[i];h*=0x01000193u;}return h;}
static DWORD tile_index[MAX_TILES];
static BYTE tile_variants[MAX_TILES];

static DWORD table_index[TILE_COUNT];
static BYTE table_variants[TILE_COUNT];
static int tile_total=EXT_COUNT;
static BOOL console_tiles;

static BOOL write_tables(const DWORD *index,const BYTE *variants) {
    DWORD old1,old2,unused;
    if(!VirtualProtect(TILE_INDEX,sizeof(DWORD)*TILE_COUNT,PAGE_READWRITE,&old1))return FALSE;
    if(!VirtualProtect(TILE_VARIANTS,TILE_COUNT,PAGE_READWRITE,&old2)) {
        VirtualProtect(TILE_INDEX,sizeof(DWORD)*TILE_COUNT,old1,&unused);return FALSE;
    }
    memcpy(TILE_INDEX,index,sizeof(DWORD)*TILE_COUNT);
    memcpy(TILE_VARIANTS,variants,TILE_COUNT);
    VirtualProtect(TILE_VARIANTS,TILE_COUNT,old2,&unused);
    VirtualProtect(TILE_INDEX,sizeof(DWORD)*TILE_COUNT,old1,&unused);
    return TRUE;
}

static WCHAR hide_session[256];static BOOL hide_session_set;
static BOOL group_hidden(const WCHAR *group) {
    WCHAR list[256];
    if(hide_session_set)lstrcpynW(list,hide_session,256);
    else GetPrivateProfileStringW(L"General",L"HideGroups",L"",list,256,ini);
    size_t n=wcslen(group);
    for(WCHAR *p=list;*p;) {
        while(*p==L' ' || *p==L',')p++;
        WCHAR *e=p;while(*e && *e!=L',')e++;
        WCHAR *t=e;while(t>p && t[-1]==L' ')t--;
        if(t>p && (size_t)(t-p)==n && !_wcsnicmp(p,group,n))return TRUE;
        p=e;
    }
    return FALSE;
}

static BYTE live_count;
static int (__cdecl *extra_ready)(int);
static int (__cdecl *extra_state)(void);

static BOOL stage_jouable(DWORD s) {
    if(s==0x2b)return live_count>=26;
    if(s==0x2c)return live_count>=27;
    if(s==0x2d)return live_count>=27 && extra_state && extra_state()==1;
    if(s==0x43)s=0x17;
    if(s==0x44)s=0x20;
    if(s==0x12 || s==0x17 || s==0x1c || s==0x20)return extra_ready && extra_ready((int)s);
    return TRUE;
}
static int load_tiles(void) {
    memcpy(tile_index,original_index,sizeof(original_index));
    memcpy(tile_variants,original_variants,sizeof(original_variants));
    int changes=0,w=TILE_COUNT;
    for(int t=0;t<MAX_TILES;t++) {
        WCHAR key[16],line[96];
        swprintf(key,16,L"Tile%d",t);
        if(!GetPrivateProfileStringW(L"Tiles",key,L"",line,96,ini)) {
            if(t==25)lstrcpyW(line,L"0x2b|1");
            else if(t==26)lstrcpyW(line,L"0x2c|2");
            else {if(t>=EXT_COUNT)break;continue;}
        }
        WCHAR *sep=wcschr(line,L'|'),*grp=sep?wcschr(sep+1,L'|'):NULL;
        DWORD index=wcstoul(line,NULL,0),variants=sep?wcstoul(sep+1,NULL,0):1;
        if(index>0x4a || variants<1 || variants>5 || index+variants-1>0x4a) {
            NOTE("tuile_ignoree",t,index,variants);continue;
        }
        if(grp) {
            grp++;while(*grp==L' ')grp++;
            WCHAR *e=grp+wcslen(grp);while(e>grp && e[-1]==L' ')*--e=0;
            if(*grp && t>=TILE_COUNT && group_hidden(grp)) {
                NOTE("tuile_masquee_groupe",t,index,0);continue;
            }
        }
        if(t>=TILE_COUNT && !stage_jouable(index)) {NOTE("tuile_sans_donnees",t,index,0);continue;}
        if(t>=TILE_COUNT)while(variants>1 && !stage_jouable(index+variants-1))variants--;
        int at=t<TILE_COUNT?t:w++;
        tile_index[at]=index;tile_variants[at]=(BYTE)variants;
        if(at<TILE_COUNT)changes++;
        NOTE("tuile",at,index,variants);
    }
    tile_total=w;

    memcpy(table_index,tile_index,sizeof(table_index));
    memcpy(table_variants,tile_variants,sizeof(table_variants));
    if(w>TILE_COUNT && tile_index[TILE_COUNT-1]==0x1b && Reglage("StageSlots","PS4Order",1)) {
        DWORD dojo=tile_index[TILE_COUNT-1];BYTE v=tile_variants[TILE_COUNT-1];
        memmove(tile_index+TILE_COUNT-1,tile_index+TILE_COUNT,(w-TILE_COUNT)*sizeof(DWORD));
        memmove(tile_variants+TILE_COUNT-1,tile_variants+TILE_COUNT,w-TILE_COUNT);
        tile_index[w-1]=dojo;tile_variants[w-1]=v;
        NOTE("ordre_ps4_dojo_en_dernier",w-1,0,0);
    }
    return changes;
}

void *fill_tiles_original;
__attribute__((used)) void __cdecl add_entries_before(BYTE *menu) {
    if(tile_total<=EXT_COUNT || menu[0x84]!=0 || online_session())return;
    void (__attribute__((thiscall)) *add_entry)(BYTE*,DWORD,DWORD,DWORD,DWORD)=
        (void(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD,DWORD,DWORD))RVA(0xbd80a0);

    for(int key=EXT_COUNT;key<=tile_total;key++)add_entry(menu,0xd,key,0xffffffff,0xffffffff);
}
#define BEFORE_HELPER(helper,target) \
    __asm__ volatile("pushfl\n pushal\n mov %esp,%ebp\n sub $544,%esp\n and $-16,%esp\n" \
        "fxsave 16(%esp)\n mov %ecx,(%esp)\n call _" helper "\n fxrstor 16(%esp)\n" \
        "mov %ebp,%esp\n popal\n popfl\n jmp *_" target "\n")
__attribute__((naked,thiscall)) void add_entries_hook(void *menu) {BEFORE_HELPER("add_entries_before","fill_tiles_original");}

typedef struct {DWORD va;BYTE size;BYTE original[8];BYTE patched[8];const char *name;} CodePatch;
static CodePatch patches[10];

static BYTE live_count=25;
static DWORD live_list=0;
static int patch_count;
static BOOL patches_applied;

static void add_patch(DWORD va,BYTE size,DWORD original,DWORD patched,const char *name) {
    CodePatch *c=&patches[patch_count++];
    c->va=va;c->size=size;memcpy(c->original,&original,4);memcpy(c->patched,&patched,4);c->name=name;
}
static void add_call_patch(DWORD va,const BYTE *original,BYTE size,void *target,const char *name) {
    CodePatch *c=&patches[patch_count++];
    c->va=va;c->size=size;memcpy(c->original,original,size);c->name=name;
    c->patched[0]=0xe8;*(DWORD*)(c->patched+1)=(DWORD)target-(DWORD)(RVA(va)+5);
    for(int i=5;i<size;i++)c->patched[i]=0x90;
}
static void prepare_patches(void) {
    patch_count=0;
    add_patch(0xb2aa82,1,live_count,tile_total,"carousel limit");
    add_patch(0xb2aa7e,1,(BYTE)(10-live_count),(BYTE)(10-tile_total),"other mode limit");
    add_patch(0xb280c3,1,live_count,tile_total,"tile index loop");
    add_patch(0xb24cc9,1,0x19,tile_total,"variant lookup");

    add_patch(0xb2aa64,4,(DWORD)(RVA(0xb28060)-RVA(0xb2aa68)),(DWORD)((BYTE*)add_entries_hook-RVA(0xb2aa68)),"tile fill call");
    add_patch(0xb24c99,4,(DWORD)TILE_INDEX,(DWORD)tile_index,"index table (variants)");
    add_patch(0xb280c9,4,live_list?live_list:(DWORD)TILE_INDEX,(DWORD)tile_index,"index table (carousel)");
    add_patch(0xb24cd5,4,(DWORD)TILE_VARIANTS,(DWORD)tile_variants,"variant table");
}
static BOOL write_patches(BOOL apply) {
    for(int i=0;i<patch_count;i++) {
        CodePatch *c=&patches[i];BYTE cur[8];
        if(!readmem(RVA(c->va),cur,c->size) || memcmp(cur,apply?c->original:c->patched,c->size)) {
            NOTE("code_inattendu",c->va,0,0);return FALSE;
        }
    }
    for(int i=0;i<patch_count;i++) {
        CodePatch *c=&patches[i];DWORD old,unused;
        if(!VirtualProtect(RVA(c->va),c->size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
        memcpy(RVA(c->va),apply?c->patched:c->original,c->size);
        VirtualProtect(RVA(c->va),c->size,old,&unused);
        FlushInstructionCache(GetCurrentProcess(),RVA(c->va),c->size);
    }
    return TRUE;
}

static BOOL retile_patches(void) {
    const struct {DWORD va;BYTE value;} v[]={{0xb2aa82,(BYTE)tile_total},{0xb2aa7e,(BYTE)(10-tile_total)},
                                           {0xb280c3,(BYTE)tile_total},{0xb24cc9,(BYTE)tile_total}};
    for(unsigned k=0;k<sizeof v/sizeof v[0];k++) {
        CodePatch *c=NULL;
        for(int i=0;i<patch_count;i++)if(patches[i].va==v[k].va)c=&patches[i];
        BYTE cur;
        if(!c || !readmem(RVA(c->va),&cur,1) || cur!=c->patched[0])return FALSE;
        DWORD old,unused;
        if(!VirtualProtect(RVA(c->va),1,PAGE_EXECUTE_READWRITE,&old))return FALSE;
        *RVA(c->va)=v[k].value;c->patched[0]=v[k].value;
        VirtualProtect(RVA(c->va),1,old,&unused);
        FlushInstructionCache(GetCurrentProcess(),RVA(c->va),1);
    }
    return TRUE;
}

static void *detour(DWORD va,const BYTE *prologue,int size,void *hook) {
    BYTE *target=RVA(va),cur[8];
    if(!readmem(target,cur,size))return NULL;

    BOOL chained=cur[0]==0xe9 && memcmp(cur,prologue,size);
    if(!chained && memcmp(cur,prologue,size))return NULL;
    BYTE *tramp=VirtualAlloc(NULL,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!tramp)return NULL;
    if(chained) {
        tramp[0]=0xe9;*(DWORD*)(tramp+1)=(DWORD)(target+5+*(int*)(cur+1))-(DWORD)(tramp+5);
        size=5;NOTE("detour_enchaine",va,0,0);
    } else {
        memcpy(tramp,prologue,size);
        tramp[size]=0xe9;*(DWORD*)(tramp+size+1)=(DWORD)(target+size)-(DWORD)(tramp+size+5);
    }
    DWORD old,unused;
    if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(tramp,0,MEM_RELEASE);return NULL;}
    target[0]=0xe9;*(DWORD*)(target+1)=(DWORD)hook-(DWORD)(target+5);
    for(int i=5;i<size;i++)target[i]=0x90;
    VirtualProtect(target,size,old,&unused);
    FlushInstructionCache(GetCurrentProcess(),target,size);
    return tramp;
}
static const BYTE prologue_index_arg[6]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE prologue_preview[5]={0x55,0x8b,0xec,0x56,0x57};
static const BYTE prologue_unlock[5]={0x55,0x8b,0xec,0x6a,0xff};

#define INDEX_COUNT 0x4b
static DWORD picture_override[INDEX_COUNT];
static const WCHAR *name_override[INDEX_COUNT];
static DWORD name_message[INDEX_COUNT];
void *picture_original,*name_original;
static BYTE (__cdecl *message_lang)(DWORD,DWORD);
static const WCHAR *(__cdecl *message_text)(DWORD,DWORD,DWORD);

__attribute__((used)) DWORD __cdecl picture_c(DWORD index) {
    if(index<INDEX_COUNT && picture_override[index])return picture_override[index];
    return ((DWORD(__cdecl*)(DWORD))picture_original)(index);
}
__attribute__((used)) const WCHAR *__cdecl name_c(DWORD index) {
    if(index<INDEX_COUNT) {
        if(name_override[index])return name_override[index];
        if(name_message[index]) {
            const WCHAR *t=message_text(message_lang(name_message[index],0x3fef),name_message[index],0x3fef);
            if(t)return t;
        }
    }
    return ((const WCHAR*(__cdecl*)(DWORD))name_original)(index);
}
#define PRESERVING_ENTRY(entry,target) \
    __attribute__((naked)) static void entry(void) { \
        __asm__ volatile("push %ecx\n push %edx\n push 12(%esp)\n call _" #target "\n" \
                         "add $4,%esp\n pop %edx\n pop %ecx\n ret\n"); }
PRESERVING_ENTRY(hook_picture,picture_c)
PRESERVING_ENTRY(hook_name,name_c)

__attribute__((used)) BYTE no_preview[INDEX_COUNT];
__attribute__((used)) BYTE alias_to[INDEX_COUNT];
void *preview_original,*select_original;
__attribute__((naked)) static void hook_preview(void) {
    __asm__ volatile(
        "push %eax\n mov 8(%esp),%eax\n cmp $0x4b,%eax\n jae 1f\n"
        "cmpb $0,_no_preview(%eax)\n jne 2f\n"
        "movl $0xff,_bg_stage\n"
        "movzbl _alias_to(%eax),%eax\n cmp $0xff,%eax\n je 1f\n mov %eax,8(%esp)\n"
        "1: pop %eax\n jmp *_preview_original\n"
        "2: push %ecx\n push %edx\n push %eax\n push %ecx\n call _nopreview_c\n add $8,%esp\n"
        "pop %edx\n pop %ecx\n pop %eax\n ret $4\n");
}

__attribute__((used)) BYTE open_no_preview;
__attribute__((used)) void *open_skip_addr;
__attribute__((naked)) static void open_preview_stub(void) {
    __asm__ volatile(
        "add %eax,%edi\n cmp $0x4b,%edi\n jae 1f\n"
        "cmpb $0,_no_preview(%edi)\n je 3f\n"
        "mov %edi,-0x1c(%ebp)\n movb $1,_open_no_preview\n mov %edi,_bg_stage\n"
        "push %eax\n mov _open_skip_addr,%eax\n mov %eax,4(%esp)\n pop %eax\n ret\n"
        "3: movl $0xff,_bg_stage\n push %eax\n movzbl _alias_to(%edi),%eax\n cmp $0xff,%eax\n je 2f\n mov %eax,%edi\n 2: pop %eax\n"
        "1: mov %edi,-0x1c(%ebp)\n ret\n");
}

static WORD open_stop_anims[8]={1,0x27};
static int open_stop_count=2;
static const BYTE open_preview_code[5]={0x03,0xf8,0x89,0x7d,0xe4};

static BYTE shown_of[INDEX_COUNT];
__attribute__((used)) DWORD __cdecl last_stage_c(DWORD player) {
    DWORD last=((DWORD(__cdecl*)(DWORD))RVA(0xa895c0))(player)&0xff;
    return last<INDEX_COUNT && shown_of[last]!=0xff?shown_of[last]:last;
}

__attribute__((naked)) static void hook_select(void) {
    __asm__ volatile(
        "mov 4(%esp),%eax\n cmp $0x4b,%eax\n jae 1f\n"
        "movzbl _alias_to(%eax),%eax\n cmp $0xff,%eax\n je 1f\n mov %eax,4(%esp)\n"
        "1: jmp *_select_original\n");
}

__attribute__((used)) BYTE force_unlock[INDEX_COUNT];
void *unlock_original;
__attribute__((naked)) static void hook_unlock(void) {
    __asm__ volatile(
        "mov 8(%esp),%eax\n cmp $0x4b,%eax\n jae 1f\n"
        "movzbl _force_unlock(%eax),%eax\n test %eax,%eax\n je 1f\n and $1,%eax\n ret\n"
        "1: jmp *_unlock_original\n");
}

#define TW 124
#define TH 72
#define SPARE_COUNT 3
#define MAX_THUMBS 19
#define VIRTUAL_BASE 0xca0100
static const DWORD spare_pictures[SPARE_COUNT]={0xca0019,0xca001b,0xca001c};
typedef struct {
    DWORD picture;WCHAR file[128];
    BYTE idx[TW*TH];BYTE pal[256*4];
    BOOL ok,logged;IDirect3DTexture9 *img_tex,*pal_tex;
} Thumb;
static Thumb thumbs[MAX_THUMBS];
static int thumb_count;
static int spare_count() {return thumb_count<SPARE_COUNT?thumb_count:SPARE_COUNT;}
static int virtual_count() {return thumb_count>SPARE_COUNT?thumb_count-SPARE_COUNT:0;}
static DWORD pending_picture[INDEX_COUNT];
static DWORD texture_vtable;

static int cmp_float(const void *a,const void *b) {float x=*(const float*)a,y=*(const float*)b;return (x>y)-(x<y);}

static BOOL load_raw_thumb(const WCHAR *path,Thumb *t) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    DWORD got=0,size=GetFileSize(f,NULL);BOOL ok=size==TW*TH+1024 && ReadFile(f,t->idx,TW*TH,&got,NULL) && got==TW*TH &&
        ReadFile(f,t->pal,1024,&got,NULL) && got==1024;
    CloseHandle(f);return ok;
}
static BOOL convert_picture(const WCHAR *path,Thumb *t,double bright,double contrast,double sat) {
    size_t n=wcslen(path);
    if(n>5 && !_wcsicmp(path+n-5,L".raw8"))return load_raw_thumb(path,t);
    IWICImagingFactory *f=NULL;IWICBitmapDecoder *dec=NULL;IWICBitmapFrameDecode *frame=NULL;
    IWICFormatConverter *conv=NULL;IWICBitmapClipper *clip=NULL;IWICBitmapScaler *scale=NULL;
    static BYTE bgr[TW*TH*3];BOOL ok=FALSE;UINT w=0,h=0;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&f)))return FALSE;
    if(SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(f,path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec)) &&
       SUCCEEDED(IWICBitmapDecoder_GetFrame(dec,0,&frame)) &&
       SUCCEEDED(IWICImagingFactory_CreateFormatConverter(f,&conv)) &&
       SUCCEEDED(IWICFormatConverter_Initialize(conv,(IWICBitmapSource*)frame,&GUID_WICPixelFormat24bppBGR,
           WICBitmapDitherTypeNone,NULL,0.0,WICBitmapPaletteTypeCustom)) &&
       SUCCEEDED(IWICFormatConverter_GetSize(conv,&w,&h)) && w>=TW/2 && h>=TH/2) {
        WICRect r;
        if((double)w/h>(double)TW/TH){r.Height=h;r.Width=(INT)(h*(double)TW/TH);r.X=(w-r.Width)/2;r.Y=0;}
        else {r.Width=w;r.Height=(INT)(w*(double)TH/TW);r.X=0;r.Y=(h-r.Height)/2;}
        if(SUCCEEDED(IWICImagingFactory_CreateBitmapClipper(f,&clip)) &&
           SUCCEEDED(IWICBitmapClipper_Initialize(clip,(IWICBitmapSource*)conv,&r)) &&
           SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(f,&scale)) &&
           SUCCEEDED(IWICBitmapScaler_Initialize(scale,(IWICBitmapSource*)clip,TW,TH,WICBitmapInterpolationModeFant)) &&
           SUCCEEDED(IWICBitmapScaler_CopyPixels(scale,NULL,TW*3,sizeof(bgr),bgr)))ok=TRUE;
    }
    if(scale)IWICBitmapScaler_Release(scale);
    if(clip)IWICBitmapClipper_Release(clip);
    if(conv)IWICFormatConverter_Release(conv);
    if(frame)IWICBitmapFrameDecode_Release(frame);
    if(dec)IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(f);
    if(!ok)return FALSE;

    enum {N=TW*TH};
    static float px[N][3],lum[N],sorted[N];
    static double sum[256][3],count[256];
    double mean=0;
    for(int i=0;i<N;i++) {
        for(int c=0;c<3;c++)px[i][c]=(float)(bgr[i*3+2-c]*bright);
        mean+=0.299*px[i][0]+0.587*px[i][1]+0.114*px[i][2];
    }
    mean/=N;
    for(int i=0;i<N;i++) {
        for(int c=0;c<3;c++)px[i][c]=(float)(mean+(px[i][c]-mean)*contrast);
        float g=0.299f*px[i][0]+0.587f*px[i][1]+0.114f*px[i][2];
        for(int c=0;c<3;c++){float v=(float)(g+(px[i][c]-g)*sat);px[i][c]=v<0?0:v>255?255:v;}
        lum[i]=sorted[i]=0.299f*px[i][0]+0.587f*px[i][1]+0.114f*px[i][2];
    }
    qsort(sorted,N,sizeof(float),cmp_float);
    float lo=sorted[N/200],hi=sorted[N-1-N/200];if(hi-lo<1)hi=lo+1;
    memset(sum,0,sizeof(sum));memset(count,0,sizeof(count));
    for(int i=0;i<N;i++) {
        int k=1+(int)floor((lum[i]-lo)/(hi-lo)*253+0.5);k=k<1?1:k>254?254:k;
        t->idx[i]=(BYTE)k;count[k]++;
        for(int c=0;c<3;c++)sum[k][c]+=px[i][c];
    }
    double prev=-1;
    for(int k=0;k<256;k++) {
        double ws=0,acc[3]={0,0,0};
        for(int j=1;j<255;j++) {
            if(!count[j])continue;
            double wt=exp(-((j-k)*(j-k))/(2*10.0*10.0));
            if(wt<1e-4)continue;
            ws+=wt*count[j];for(int c=0;c<3;c++)acc[c]+=wt*sum[j][c];
        }
        double col[3]={0,0,0};
        if(ws>0)for(int c=0;c<3;c++)col[c]=acc[c]/ws;
        double l=0.299*col[0]+0.587*col[1]+0.114*col[2];
        if(l<prev && l>0){double s=prev/l;for(int c=0;c<3;c++)col[c]=col[c]*s>255?255:col[c]*s;l=prev;}
        prev=l;
        t->pal[k*4+0]=(BYTE)col[2];t->pal[k*4+1]=(BYTE)col[1];t->pal[k*4+2]=(BYTE)col[0];t->pal[k*4+3]=255;
    }
    memset(t->pal+255*4,255,4);
    t->pal[0]=t->pal[1]=t->pal[2]=255;t->pal[3]=0;
    for(int y=0;y<TH;y++)for(int x=0;x<TW;x++)
        if(x<2 || y<2 || x>=TW-2 || y>=TH-2)t->idx[y*TW+x]=255;
    t->idx[0]=t->idx[TW-1]=t->idx[(TH-1)*TW]=t->idx[TH*TW-1]=0;
    return TRUE;
}

static DWORD find_texture_vtable(void) {
    typedef IDirect3D9 *(WINAPI *Create_t)(UINT);
    HMODULE d3d=LoadLibraryW(L"d3d9.dll");if(!d3d)return 0;
    Create_t create=(Create_t)GetProcAddress(d3d,"Direct3DCreate9");if(!create)return 0;
    IDirect3D9 *d=create(D3D_SDK_VERSION);if(!d)return 0;
    HWND w=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,8,8,NULL,NULL,NULL,NULL);
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));
    pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.BackBufferWidth=8;pp.BackBufferHeight=8;
    IDirect3DDevice9 *dev=NULL;DWORD vt=0;
    if(SUCCEEDED(IDirect3D9_CreateDevice(d,D3DADAPTER_DEFAULT,D3DDEVTYPE_NULLREF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev))) {
        IDirect3DTexture9 *t=NULL;
        if(SUCCEEDED(IDirect3DDevice9_CreateTexture(dev,4,4,1,0,D3DFMT_L8,D3DPOOL_SYSTEMMEM,&t,NULL))) {
            vt=*(DWORD*)t;IDirect3DTexture9_Release(t);
        }
        IDirect3DDevice9_Release(dev);
    }
    IDirect3D9_Release(d);
    if(w)DestroyWindow(w);
    return vt;
}
static IDirect3DTexture9 *checked_texture(DWORD p,UINT w,UINT h,D3DFORMAT fmt) {
    if(!texture_vtable || p<0x10000 || (p&3) || rd32((void*)p)!=texture_vtable)return NULL;
    IDirect3DTexture9 *t=(IDirect3DTexture9*)p;D3DSURFACE_DESC d;
    if(FAILED(IDirect3DTexture9_GetLevelDesc(t,0,&d)) || d.Width!=w || (h && d.Height!=h) || d.Format!=fmt)return NULL;
    return t;
}

static BOOL resolve_thumb(Thumb *t,IDirect3DTexture9 **img,IDirect3DTexture9 **pal,int *row,BOOL report) {
    DWORD (__cdecl *picture_record)(DWORD)=(DWORD(__cdecl*)(DWORD))RVA(0xbcf680);
    DWORD rec=picture_record(t->picture);
    if(!rec){if(report)NOTE("vignette_sans_fiche",t->picture,0,0);return FALSE;}
    DWORD texi=rd32((void*)(rec+0x14)),group=t->picture>>16;
    if(group>=rd32(RVA(0x24825d4)))return FALSE;
    DWORD entry=rd32(RVA(0x24825d8))+group*0x28,blk=rd32((void*)(entry+0xc));
    DWORD tb=blk?rd32((void*)(blk+0x20)):0;if(tb)tb+=blk;
    DWORD ti=tb?rd32((void*)(tb+4)):0;if(ti)ti+=blk;
    char tag[8];
    if(!ti || !readmem((void*)ti,tag,8) || memcmp(tag,"texinfo",8) || texi>=rd32((void*)(ti+0x10))) {
        if(report)NOTE("vignette_sans_texinfo",t->picture,0,0);
        return FALSE;
    }
    DWORD e=ti+0x20+texi*12;float v=0;readmem((void*)(e+8),&v,4);
    *row=(int)(v*8);

    *img=checked_texture(rd32((void*)rd32((void*)rd32((void*)e))),TW,TH,D3DFMT_L8);
    *pal=checked_texture(rd32((void*)rd32((void*)rd32((void*)(e+4)))),256,0,D3DFMT_A8R8G8B8);
    if(!*img || !*pal || *row<0 || *row>=8) {
        if(report)NOTE("vignette_textures_inconnues",t->picture,0,0);
        return FALSE;
    }
    return TRUE;
}
static BOOL apply_thumb(Thumb *t,BOOL report) {
    IDirect3DTexture9 *img,*pal;int row;D3DLOCKED_RECT lr;
    if(!resolve_thumb(t,&img,&pal,&row,report))return FALSE;
    if(FAILED(IDirect3DTexture9_LockRect(img,0,&lr,NULL,0))){NOTE("vignette_verrou_image",t->picture,0,0);return FALSE;}
    for(int y=0;y<TH;y++)memcpy((BYTE*)lr.pBits+y*lr.Pitch,t->idx+y*TW,TW);
    IDirect3DTexture9_UnlockRect(img,0);
    RECT rr={0,row,256,row+1};
    if(FAILED(IDirect3DTexture9_LockRect(pal,0,&lr,&rr,0))){NOTE("vignette_verrou_palette",t->picture,0,0);return FALSE;}
    memcpy(lr.pBits,t->pal,sizeof(t->pal));
    IDirect3DTexture9_UnlockRect(pal,0);
    if(!t->logged)NOTE("vignette_peinte",t->picture,row,0);
    t->logged=TRUE;t->img_tex=img;t->pal_tex=pal;
    return TRUE;
}

typedef struct {
    BYTE record[0x30];
    DWORD texinfo[3];
    DWORD desc[4];
    BYTE object[0x40];
} Virtual;
typedef struct {DWORD desc[4];BYTE object[0x40];IDirect3DTexture9 *tex;} VirtualPalette;
static Virtual *virtuals;
static VirtualPalette *virtual_palettes;
static volatile BOOL virtuals_ready;
static BOOL virtuals_failed;
void *record_original,*texinfo_original;

static BOOL is_virtual(DWORD id,DWORD *k) {
    if(!virtuals_ready || id<VIRTUAL_BASE || id>=VIRTUAL_BASE+(DWORD)virtual_count())return FALSE;
    *k=id-VIRTUAL_BASE;return TRUE;
}
__attribute__((used)) DWORD __cdecl record_c(DWORD id) {
    DWORD k;
    if(is_virtual(id,&k))return (DWORD)virtuals[k].record;
    return ((DWORD(__cdecl*)(DWORD))record_original)(id);
}
__attribute__((used)) DWORD __cdecl texinfo_c(DWORD id) {
    DWORD k;
    if(is_virtual(id,&k))return (DWORD)virtuals[k].texinfo;
    return ((DWORD(__cdecl*)(DWORD))texinfo_original)(id);
}
PRESERVING_ENTRY(hook_record,record_c)
PRESERVING_ENTRY(hook_texinfo,texinfo_c)
static const BYTE prologue_lookup[7]={0x55,0x8b,0xec,0x56,0x8b,0x75,0x08};

static IDirect3DTexture9 *new_texture(IDirect3DDevice9 *dev,UINT w,UINT h,D3DFORMAT fmt,const BYTE *rows,UINT row_bytes) {
    IDirect3DTexture9 *t=NULL;D3DLOCKED_RECT lr;
    if(FAILED(IDirect3DDevice9_CreateTexture(dev,w,h,1,0,fmt,D3DPOOL_MANAGED,&t,NULL)))return NULL;
    if(FAILED(IDirect3DTexture9_LockRect(t,0,&lr,NULL,0))){IDirect3DTexture9_Release(t);return NULL;}
    for(UINT y=0;y<h;y++)memcpy((BYTE*)lr.pBits+y*lr.Pitch,rows+y*row_bytes,row_bytes);
    IDirect3DTexture9_UnlockRect(t,0);
    return t;
}

static void create_virtuals(void) {
    Thumb probe={0};probe.picture=spare_pictures[0];
    IDirect3DTexture9 *img,*pal;int row;
    if(!resolve_thumb(&probe,&img,&pal,&row,FALSE))return;
    virtuals_failed=TRUE;
    DWORD rec=((DWORD(__cdecl*)(DWORD))RVA(0xbcf680))(spare_pictures[0]);
    DWORD texi=rd32((void*)(rec+0x14)),entry=rd32(RVA(0x24825d8))+(spare_pictures[0]>>16)*0x28,blk=rd32((void*)(entry+0xc));
    DWORD tb=rd32((void*)(blk+0x20))+blk,ti=rd32((void*)(tb+4))+blk,e=ti+0x20+texi*12;
    DWORD img_obj=rd32((void*)rd32((void*)e))-8,pal_obj=rd32((void*)rd32((void*)(e+4)))-8;
    BYTE tmpl_rec[0x30],tmpl_img[0x40],tmpl_pal[0x40];
    if(!readmem((void*)rec,tmpl_rec,sizeof(tmpl_rec)) || !readmem((void*)img_obj,tmpl_img,sizeof(tmpl_img)) ||
       !readmem((void*)pal_obj,tmpl_pal,sizeof(tmpl_pal)) || *(DWORD*)(tmpl_img+8)!=(DWORD)img || *(DWORD*)(tmpl_pal+8)!=(DWORD)pal) {
        NOTE("images_virtuelles_objets_inattendus",0,0,0);return;
    }
    IDirect3DDevice9 *dev=NULL;
    if(FAILED(IDirect3DTexture9_GetDevice(img,&dev)) || !dev){NOTE("images_virtuelles_sans_device",0,0,0);return;}
    int n=virtual_count(),np=(n+7)/8;
    virtuals=VirtualAlloc(NULL,sizeof(Virtual)*n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    virtual_palettes=VirtualAlloc(NULL,sizeof(VirtualPalette)*np,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!virtuals || !virtual_palettes){IDirect3DDevice9_Release(dev);return;}
    for(int p=0;p<np;p++) {
        static BYTE rows[8*256*4];memset(rows,0,sizeof(rows));
        for(int r=0;r<8 && p*8+r<n;r++)memcpy(rows+r*1024,thumbs[SPARE_COUNT+p*8+r].pal,1024);
        VirtualPalette *vp=&virtual_palettes[p];
        vp->tex=new_texture(dev,256,8,D3DFMT_A8R8G8B8,rows,1024);
        if(!vp->tex){NOTE("images_virtuelles_palette_echec",0,0,0);IDirect3DDevice9_Release(dev);return;}
        memcpy(vp->object,tmpl_pal,0x40);*(DWORD*)(vp->object+4)=0x10000;*(DWORD*)(vp->object+8)=(DWORD)vp->tex;
        vp->desc[0]=(DWORD)(vp->object+8);vp->desc[1]=1;vp->desc[2]=0x2000+p;vp->desc[3]=(DWORD)vp->object;
    }
    for(int k=0;k<n;k++) {
        Thumb *t=&thumbs[SPARE_COUNT+k];Virtual *v=&virtuals[k];
        IDirect3DTexture9 *tex=new_texture(dev,TW,TH,D3DFMT_L8,t->idx,TW);
        if(!tex){NOTE("images_virtuelles_texture_echec",0,0,0);IDirect3DDevice9_Release(dev);return;}
        memcpy(v->object,tmpl_img,0x40);*(DWORD*)(v->object+4)=0x10000;*(DWORD*)(v->object+8)=(DWORD)tex;
        v->desc[0]=(DWORD)(v->object+8);v->desc[1]=1;v->desc[2]=0x1000+k;v->desc[3]=(DWORD)v->object;
        float row_v=((k%8)+0.5f)/8;
        v->texinfo[0]=(DWORD)v->desc;v->texinfo[1]=(DWORD)virtual_palettes[k/8].desc;memcpy(&v->texinfo[2],&row_v,4);
        memcpy(v->record,tmpl_rec,0x30);*(DWORD*)(v->record+0x14)=0x100+k;
    }
    IDirect3DDevice9_Release(dev);
    virtuals_ready=TRUE;virtuals_failed=FALSE;
    for(int i=0;i<INDEX_COUNT;i++)if(pending_picture[i])picture_override[i]=pending_picture[i];
    NOTE("images_virtuelles_pretes",n,0,0);
}

#define BG_MAX 32
#define BG_W 1280
#define BG_H 720
static WCHAR *bg_file[BG_MAX];static int bg_count;
static signed char bg_of_stage[INDEX_COUNT];
__attribute__((used)) volatile DWORD bg_stage=0xff;
static volatile DWORD menu_seen_tick;
static BOOL stop_preview=TRUE;
static volatile LONG bg_request=-1;static HANDLE bg_event;
static BYTE *volatile bg_pixels;static volatile LONG bg_pixels_slot=-1;
typedef struct {int slot;IDirect3DTexture9 *tex;DWORD used;} BgTex;
static BgTex bg_tex[3];
typedef HRESULT(WINAPI *SetTexture_t)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
typedef HRESULT(WINAPI *DrawPrim_t)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
typedef HRESULT(WINAPI *DrawIdx_t)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
typedef HRESULT(WINAPI *DrawPrimUP_t)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
typedef HRESULT(WINAPI *DrawIdxUP_t)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
static SetTexture_t settex_original;static DrawPrim_t drawprim_original;static DrawIdx_t drawidx_original;
static DrawPrimUP_t drawprimup_original;static DrawIdxUP_t drawidxup_original;
static void *stage0_tex;
typedef struct {void *tex;BOOL backdrop;} TexClass;
static TexClass tex_class[256];static int tex_class_count;
static BOOL bg_logged_draw;

static BYTE *decode_background(const WCHAR *path) {
    IWICImagingFactory *f=NULL;IWICBitmapDecoder *dec=NULL;IWICBitmapFrameDecode *frame=NULL;
    IWICFormatConverter *conv=NULL;IWICBitmap *bmp=NULL;IWICBitmapClipper *clip=NULL;IWICBitmapScaler *scale=NULL;
    BYTE *full=NULL,*out=NULL;UINT w=0,h=0;BOOL ok=FALSE;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&f)))return NULL;
    if(SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(f,path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec)) &&
       SUCCEEDED(IWICBitmapDecoder_GetFrame(dec,0,&frame)) &&
       SUCCEEDED(IWICImagingFactory_CreateFormatConverter(f,&conv)) &&
       SUCCEEDED(IWICFormatConverter_Initialize(conv,(IWICBitmapSource*)frame,&GUID_WICPixelFormat32bppBGRA,
           WICBitmapDitherTypeNone,NULL,0.0,WICBitmapPaletteTypeCustom)) &&
       SUCCEEDED(IWICFormatConverter_GetSize(conv,&w,&h)) && w>=64 && h>=36 && w<=8192 && h<=8192 &&
       (full=malloc((size_t)w*h*4)) && SUCCEEDED(IWICFormatConverter_CopyPixels(conv,NULL,w*4,w*h*4,full))) {

        #define DARK_COL(x) ({BOOL d=TRUE;for(UINT y=0;y<h && d;y+=4){const BYTE *q=full+((size_t)y*w+(x))*4;d=q[0]<24&&q[1]<24&&q[2]<24;}d;})
        #define DARK_ROW(y) ({BOOL d=TRUE;for(UINT x=0;x<w && d;x+=4){const BYTE *q=full+((size_t)(y)*w+x)*4;d=q[0]<24&&q[1]<24&&q[2]<24;}d;})
        UINT x0=0,x1=w,y0=0,y1=h;
        while(x1-x0>w/2 && DARK_COL(x0))x0++;
        while(x1-x0>w/2 && DARK_COL(x1-1))x1--;
        while(y1-y0>h/2 && DARK_ROW(y0))y0++;
        while(y1-y0>h/2 && DARK_ROW(y1-1))y1--;
        WICRect r;UINT cw=x1-x0,ch=y1-y0;
        if((double)cw/ch>16.0/9){r.Height=ch;r.Width=(INT)(ch*16.0/9);r.X=x0+(cw-r.Width)/2;r.Y=y0;}
        else {r.Width=cw;r.Height=(INT)(cw*9.0/16);r.X=x0;r.Y=y0+(ch-r.Height)/2;}
        if(SUCCEEDED(IWICImagingFactory_CreateBitmapFromMemory(f,w,h,&GUID_WICPixelFormat32bppBGRA,w*4,w*h*4,full,&bmp)) &&
           SUCCEEDED(IWICImagingFactory_CreateBitmapClipper(f,&clip)) &&
           SUCCEEDED(IWICBitmapClipper_Initialize(clip,(IWICBitmapSource*)bmp,&r)) &&
           SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(f,&scale)) &&
           SUCCEEDED(IWICBitmapScaler_Initialize(scale,(IWICBitmapSource*)clip,BG_W,BG_H,WICBitmapInterpolationModeFant)) &&
           (out=malloc(BG_W*BG_H*4)) && SUCCEEDED(IWICBitmapScaler_CopyPixels(scale,NULL,BG_W*4,BG_W*BG_H*4,out)))ok=TRUE;
        NOTE("fond_decode",w,h,0);
    }
    if(scale)IWICBitmapScaler_Release(scale);
    if(clip)IWICBitmapClipper_Release(clip);
    if(bmp)IWICBitmap_Release(bmp);
    if(conv)IWICFormatConverter_Release(conv);
    if(frame)IWICBitmapFrameDecode_Release(frame);
    if(dec)IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(f);
    free(full);
    if(!ok){free(out);return NULL;}
    return out;
}
static DWORD WINAPI bg_worker(void *unused) {
    (void)unused;
    CoInitializeEx(NULL,COINIT_MULTITHREADED);
    for(;;) {
        WaitForSingleObject(bg_event,INFINITE);
        LONG slot=InterlockedExchange(&bg_request,-1);
        if(slot<0 || slot>=bg_count || bg_pixels_slot==slot)continue;
        BYTE *px=decode_background(bg_file[slot]);
        NOTE("fond",slot,px!=NULL,0);
        if(!px)continue;
        BYTE *old=InterlockedExchangePointer((void*volatile*)&bg_pixels,NULL);free(old);
        bg_pixels_slot=slot;InterlockedExchangePointer((void*volatile*)&bg_pixels,px);
    }
    return 0;
}

static BOOL is_backdrop(void *tex) {
    for(int i=0;i<tex_class_count;i++)if(tex_class[i].tex==tex)return tex_class[i].backdrop;
    BOOL b=FALSE;
    if(IDirect3DBaseTexture9_GetType((IDirect3DBaseTexture9*)tex)==D3DRTYPE_TEXTURE) {
        D3DSURFACE_DESC d;
        if(SUCCEEDED(IDirect3DTexture9_GetLevelDesc((IDirect3DTexture9*)tex,0,&d)))
            b=d.Width==1280 && d.Height==720 && d.Format==D3DFMT_L8 && d.Pool==D3DPOOL_MANAGED;
    }
    if(tex_class_count<256){tex_class[tex_class_count].tex=tex;tex_class[tex_class_count++].backdrop=b;}
    if(b)NOTE("fond_menu_reconnu",0,0,0);
    return b;
}
static HRESULT WINAPI settex_hook(IDirect3DDevice9 *dev,DWORD stage,IDirect3DBaseTexture9 *tex) {
    if(stage==0)stage0_tex=tex;
    return settex_original(dev,stage,tex);
}

static IDirect3DTexture9 *current_background(IDirect3DDevice9 *dev) {
    DWORD st=bg_stage;
    if(st>=INDEX_COUNT || bg_of_stage[st]<0)return NULL;
    int slot=bg_of_stage[st];DWORD now=GetTickCount();
    for(int i=0;i<3;i++)if(bg_tex[i].tex && bg_tex[i].slot==slot){bg_tex[i].used=now;return bg_tex[i].tex;}
    if(bg_pixels_slot==slot && bg_pixels) {
        BYTE *px=InterlockedExchangePointer((void*volatile*)&bg_pixels,NULL);
        if(!px)return NULL;
        IDirect3DTexture9 *t=NULL;D3DLOCKED_RECT lr;
        if(SUCCEEDED(IDirect3DDevice9_CreateTexture(dev,BG_W,BG_H,1,0,D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&t,NULL))) {
            if(SUCCEEDED(IDirect3DTexture9_LockRect(t,0,&lr,NULL,0))) {
                for(int y=0;y<BG_H;y++)memcpy((BYTE*)lr.pBits+y*lr.Pitch,px+y*BG_W*4,BG_W*4);
                IDirect3DTexture9_UnlockRect(t,0);
            } else {IDirect3DTexture9_Release(t);t=NULL;}
        }
        free(px);bg_pixels_slot=-1;
        if(!t)return NULL;
        int k=0;
        for(int i=1;i<3;i++)if(!bg_tex[i].tex || (bg_tex[k].tex && bg_tex[i].used<bg_tex[k].used))k=i;
        if(bg_tex[k].tex)IDirect3DTexture9_Release(bg_tex[k].tex);
        bg_tex[k].tex=t;bg_tex[k].slot=slot;bg_tex[k].used=now;
        return t;
    }
    if(bg_request!=slot){InterlockedExchange(&bg_request,slot);SetEvent(bg_event);}
    return NULL;
}

static BOOL draw_background(IDirect3DDevice9 *dev) {
    if(!stage0_tex || bg_stage==0xff || GetTickCount()-menu_seen_tick>300 || !is_backdrop(stage0_tex))return FALSE;
    IDirect3DTexture9 *t=current_background(dev);
    if(!t)return FALSE;
    IDirect3DStateBlock9 *sb=NULL;
    if(FAILED(IDirect3DDevice9_CreateStateBlock(dev,D3DSBT_ALL,&sb)))return FALSE;
    D3DVIEWPORT9 vp;IDirect3DDevice9_GetViewport(dev,&vp);
    IDirect3DBaseTexture9 *t1=NULL;IDirect3DDevice9_GetTexture(dev,1,&t1);
    IDirect3DDevice9_SetVertexShader(dev,NULL);IDirect3DDevice9_SetPixelShader(dev,NULL);
    IDirect3DDevice9_SetFVF(dev,D3DFVF_XYZRHW|D3DFVF_TEX1);
    settex_original(dev,0,(IDirect3DBaseTexture9*)t);settex_original(dev,1,NULL);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_TEXCOORDINDEX,0);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev,1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev,1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_SRGBTEXTURE,0);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ALPHABLENDENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ALPHATESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ZENABLE,D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ZWRITEENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_STENCILENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_SCISSORTESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_CULLMODE,D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_FOGENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_LIGHTING,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_COLORWRITEENABLE,0xf);
    float x0=vp.X-0.5f,y0=vp.Y-0.5f,x1=x0+vp.Width,y1=y0+vp.Height;
    struct {float x,y,z,w,u,v;} q[4]={{x0,y0,0,1,0,0},{x1,y0,0,1,1,0},{x0,y1,0,1,0,1},{x1,y1,0,1,1,1}};
    HRESULT hr=drawprimup_original(dev,D3DPT_TRIANGLESTRIP,2,q,sizeof(q[0]));
    IDirect3DStateBlock9_Apply(sb);IDirect3DStateBlock9_Release(sb);
    settex_original(dev,1,t1);if(t1)IDirect3DBaseTexture9_Release(t1);
    if(!bg_logged_draw){bg_logged_draw=TRUE;NOTE("fond_dessine",vp.Width,vp.Height,hr);}
    return TRUE;
}
static HRESULT WINAPI drawprim_hook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,UINT a,UINT b) {
    return draw_background(d)?D3D_OK:drawprim_original(d,t,a,b);
}
static HRESULT WINAPI drawidx_hook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,INT a,UINT b,UINT c,UINT e,UINT f) {
    return draw_background(d)?D3D_OK:drawidx_original(d,t,a,b,c,e,f);
}
static HRESULT WINAPI drawprimup_hook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,UINT a,const void *v,UINT s) {
    return draw_background(d)?D3D_OK:drawprimup_original(d,t,a,v,s);
}
static HRESULT WINAPI drawidxup_hook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,UINT a,UINT b,UINT c,const void *i,D3DFORMAT f,const void *v,UINT s) {
    return draw_background(d)?D3D_OK:drawidxup_original(d,t,a,b,c,i,f,v,s);
}
static void hook_device(IDirect3DTexture9 *any) {
    IDirect3DDevice9 *dev=NULL;
    if(settex_original || FAILED(IDirect3DTexture9_GetDevice(any,&dev)) || !dev)return;
    void **vt=*(void***)dev;DWORD old;
    if(VirtualProtect(&vt[65],(84-65+1)*4,PAGE_EXECUTE_READWRITE,&old)) {
        drawprim_original=(DrawPrim_t)vt[81];drawidx_original=(DrawIdx_t)vt[82];
        drawprimup_original=(DrawPrimUP_t)vt[83];drawidxup_original=(DrawIdxUP_t)vt[84];
        settex_original=(SetTexture_t)vt[65];
        vt[81]=(void*)drawprim_hook;vt[82]=(void*)drawidx_hook;vt[83]=(void*)drawprimup_hook;vt[84]=(void*)drawidxup_hook;
        vt[65]=(void*)settex_hook;
        VirtualProtect(&vt[65],(84-65+1)*4,old,&old);
        NOTE("fonds_crochets",bg_count,0,0);
    }
    IDirect3DDevice9_Release(dev);
}

__attribute__((used)) void __cdecl nopreview_c(BYTE *menu,DWORD shown) {
    bg_stage=shown;
    if(!stop_preview)return;
    DWORD loaded=rd32(menu+0x90)&0xff,asked=rd32(menu+0x94)&0xff;
    if(loaded==0xff && asked==0xff)return;
    BYTE *pv=RVA(0x138a898);
    ((void(__thiscall*)(void*))RVA(0x716290))(pv);
    ((void(__thiscall*)(void*))RVA(0x716160))(pv);
    pv[6]=0xff;pv[8]=0xff;
    *(DWORD*)(menu+0x90)=0xff;*(DWORD*)(menu+0x94)=0xff;
    menu[0xf3]=0;
    NOTE("apercu_arrete",shown,loaded,0);
}
static void *seen_menu;
static int menu_frames;
static DWORD last_update;
void *menu_update_original;

static volatile BOOL reload_tiles;

__attribute__((unused)) static void toggle_groups(char *n8,int size) {
    WCHAR cur[256],tog[256];
    if(hide_session_set)lstrcpynW(cur,hide_session,256);
    else GetPrivateProfileStringW(L"General",L"HideGroups",L"",cur,256,ini);
    GetPrivateProfileStringW(L"General",L"ToggleGroups",L"bonus",tog,256,ini);
    const WCHAR *next=cur[0]?L"":tog;
    lstrcpynW(hide_session,next,256);hide_session_set=TRUE;
    if(GetPrivateProfileIntW(L"General",L"RememberToggle",0,ini))WritePrivateProfileStringW(L"General",L"HideGroups",next,ini);
    WideCharToMultiByte(CP_UTF8,0,next,-1,n8,size,NULL,NULL);
}
void __cdecl menu_update_before(BYTE *menu) {
    if(rd32(menu)!=(DWORD)RVA(0x100d2cc))return;
    DWORD now=GetTickCount();
    if(menu!=seen_menu || now-last_update>300) {
        seen_menu=menu;menu_frames=0;tex_class_count=0;
        for(int i=0;i<spare_count();i++){thumbs[i].ok=thumbs[i].logged=FALSE;thumbs[i].img_tex=thumbs[i].pal_tex=NULL;}
    }
    last_update=now;
    menu_seen_tick=now;
    if(bg_count && !settex_original)for(int i=0;i<spare_count();i++)if(thumbs[i].img_tex){hook_device(thumbs[i].img_tex);break;}
    if(open_no_preview) {
        open_no_preview=0;
        for(int i=0;i<open_stop_count;i++)
            ((void(__thiscall*)(void*,DWORD))RVA(0xa99280))(menu+0xfc,open_stop_anims[i]);
        NOTE("ouverture_sans_apercu",open_stop_count,0,0);
    }
    if(menu_frames>600)return;
    if(virtual_count() && !virtuals_ready && !virtuals_failed)create_virtuals();
    BOOL last=++menu_frames==600,refresh=menu_frames%60==0;
    for(int i=0;i<spare_count();i++) {
        Thumb *t=&thumbs[i];
        if(refresh)t->ok=FALSE;
        if(t->ok) {
            IDirect3DTexture9 *img,*pal;int row;
            if(resolve_thumb(t,&img,&pal,&row,FALSE) && (img!=t->img_tex || pal!=t->pal_tex))t->ok=FALSE;
        }
        if(!t->ok)t->ok=apply_thumb(t,last);
    }
}

__attribute__((naked,thiscall)) void menu_update_hook(void *object) {BEFORE_HELPER("menu_update_before","menu_update_original");}
static BOOL install_menu_hook(void) {
    void **slot=(void**)RVA(0x100d2cc+0x84);
    menu_update_original=RVA(0xb2bf30);
    if(*slot!=menu_update_original){NOTE("menu_deja_detourne",0,0,0);return FALSE;}
    DWORD old,unused;
    if(!VirtualProtect(slot,4,PAGE_READWRITE,&old))return FALSE;
    BOOL ok=InterlockedCompareExchangePointer(slot,(void*)menu_update_hook,menu_update_original)==menu_update_original;
    VirtualProtect(slot,4,old,&unused);
    return ok;
}

static WCHAR names_storage[8192];
static int load_stages(void) {
    static WCHAR keys[4096];
    WCHAR *next=names_storage;int count=0;
    double bright=ini_float(L"Advanced",L"ThumbBrightness",1.15),contrast=ini_float(L"Advanced",L"ThumbContrast",1.10),
        sat=ini_float(L"Advanced",L"ThumbSaturation",0.90);
    GetPrivateProfileStringW(L"Stages",NULL,L"",keys,4096,ini);
    for(WCHAR *k=keys;*k;k+=wcslen(k)+1) {
        DWORD index=wcstoul(k,NULL,0);
        WCHAR line[256];
        GetPrivateProfileStringW(L"Stages",k,L"",line,256,ini);
        if(index>=INDEX_COUNT || !line[0])continue;
        WCHAR *pic=wcschr(line,L'|');if(pic)*pic++=0;
        WCHAR *bgpic=pic?wcschr(pic,L'|'):NULL;if(bgpic)*bgpic++=0;
        while(bgpic && *bgpic==L' ')bgpic++;
        if(bgpic && *bgpic) {
            int slot=-1;
            for(int i=0;i<bg_count;i++)if(!lstrcmpiW(bg_file[i]+wcslen(folder),bgpic))slot=i;
            if(slot<0 && bg_count<BG_MAX) {
                WCHAR *path=malloc(MAX_PATH*sizeof(WCHAR));
                if(path){swprintf(path,MAX_PATH,L"%ls%ls",folder,bgpic);bg_file[bg_count]=path;slot=bg_count++;}
            }
            if(slot>=0)bg_of_stage[index]=(signed char)slot;
            NOTE("niveau_fond",index,slot,0);
        }
        if(line[0]==L'#')name_message[index]=wcstoul(line+1,NULL,0);
        else if(line[0] && next+wcslen(line)+1<names_storage+8192){wcscpy(next,line);name_override[index]=next;next+=wcslen(line)+1;}
        while(pic && *pic==L' ')pic++;
        if(pic && *pic) {
            int slot=-1;
            for(int i=0;i<thumb_count;i++)if(!lstrcmpiW(thumbs[i].file,pic))slot=i;
            if(slot<0 && thumb_count<MAX_THUMBS) {
                Thumb *t=&thumbs[thumb_count];WCHAR path[MAX_PATH];
                lstrcpynW(t->file,pic,128);
                t->picture=thumb_count<SPARE_COUNT?spare_pictures[thumb_count]:VIRTUAL_BASE+(thumb_count-SPARE_COUNT);
                swprintf(path,MAX_PATH,L"%ls%ls",folder,pic);
                if(convert_picture(path,t,bright,contrast,sat)){slot=thumb_count++;NOTE("vignette",index,t->picture,0);}
                else NOTE("vignette_illisible",index,0,0);
            } else if(slot<0)NOTE("vignettes_trop_nombreuses",index,MAX_THUMBS,0);
            if(slot>=0) {
                if(slot<SPARE_COUNT)picture_override[index]=thumbs[slot].picture;
                else pending_picture[index]=thumbs[slot].picture;
            }
        }
        NOTE("niveau",index,name_message[index]?1:name_override[index]?2:0,picture_override[index]||pending_picture[index]);
        count++;
    }
    return count;
}

static volatile DWORD chara_tick;
void *chara_update_original;

static DWORD signal_sound=0x10000005;
static DWORD hide_sounds[8]={0x10000};static int hide_count=1,hide_next;
static volatile DWORD signal_pending;
static void play_signal(void) {
    DWORD id=signal_pending;signal_pending=0;
    if(!id)return;
    DWORD r=((DWORD(__cdecl*)(DWORD,DWORD))RVA(0x957c40))(id,0);
    NOTE("son_signal",id,0,0);(void)r;
}
__attribute__((used)) void __cdecl chara_update_before(void) {
    chara_tick=GetTickCount();
    if(signal_pending)play_signal();
}
__attribute__((naked)) static void chara_update_hook(void) {
    __asm__ volatile("pushfl\n pushal\n call _chara_update_before\n popal\n popfl\n jmp *_chara_update_original\n");
}
__attribute__((unused)) static BOOL hook_chara_select(void) {
    void **slot=(void**)RVA(0x100cea4+33*4);DWORD old;
    if(*slot!=(void*)RVA(0xabdae0))return FALSE;
    if(!VirtualProtect(slot,4,PAGE_READWRITE,&old))return FALSE;
    chara_update_original=*slot;*slot=(void*)chara_update_hook;
    VirtualProtect(slot,4,old,&old);
    return TRUE;
}

void *text_original;
static WCHAR toggle_hint[160];
static BOOL groups_shown(void) {
    WCHAR cur[256];
    if(hide_session_set)lstrcpynW(cur,hide_session,256);
    else GetPrivateProfileStringW(L"General",L"HideGroups",L"",cur,256,ini);
    return !cur[0];
}
__attribute__((used)) const WCHAR *__cdecl text_c(DWORD lang,DWORD id,DWORD flags) {
    const WCHAR *t=((const WCHAR*(__cdecl*)(DWORD,DWORD,DWORD))text_original)(lang,id,flags);
    if((id!=0x300ea && id!=0x300eb) || !toggle_hint[0] || !t || en_ligne_brut())return t;
    static WCHAR buf[2][400];WCHAR *b=buf[id&1];

    WCHAR hint[200];const WCHAR *state=groups_shown()?L"@[color:green]ON@[color:resume]":L"@[color:red]OFF@[color:resume]";
    const WCHAR *mark=wcsstr(toggle_hint,L"%s");
    if(mark)swprintf(hint,200,L"%.*ls%ls%ls",(int)(mark-toggle_hint),toggle_hint,state,mark+2);
    else lstrcpynW(hint,toggle_hint,200);
    swprintf(b,400,L"%ls  %ls",hint,t);
    return b;
}
__attribute__((naked,unused)) static void hook_text(void) {
    __asm__ volatile("push %ecx\n push %edx\n push 20(%esp)\n push 20(%esp)\n push 20(%esp)\n"
                     "call _text_c\n add $12,%esp\n pop %edx\n pop %ecx\n ret\n");
}
static DWORD toggle_buttons=0x280;
__attribute__((unused)) static DWORD WINAPI combo_worker(void *unused) {
    (void)unused;
    BOOL was=FALSE;
    for(;;Sleep(16)) {
        BOOL both=FALSE;
        for(int d=0;d<5;d++){DWORD h=rd32(RVA(0x2457928+d*0x2c));if((h&toggle_buttons)==toggle_buttons){both=TRUE;break;}}
        if(both && !was && GetTickCount()-chara_tick<300 && !en_ligne_brut() && console_tiles) {
            char n8[256];toggle_groups(n8,sizeof n8);reload_tiles=TRUE;
            if(n8[0]) {
                signal_pending=hide_sounds[hide_next];
                if(hide_count>1)NOTE("son_masquage",hide_next+1,hide_sounds[hide_next],0);
                hide_next=(hide_next+1)%hide_count;
            } else signal_pending=signal_sound;
            NOTE("bascule_groupes",n8[0]==0,0,0);
        }
        was=both;
    }
    return 0;
}

static int (__cdecl *extra_state)(void);
static int (__cdecl *extra_ready)(int);

static BOOL fichier_present(const WCHAR *nom) {
    WCHAR p[MAX_PATH],c[MAX_PATH];DWORD n=GetModuleFileNameW(NULL,p,MAX_PATH);
    if(!n || n>=MAX_PATH)return FALSE;
    WCHAR *sep=wcsrchr(p,L'\\');if(!sep)return FALSE;
    sep[1]=0;
    swprintf(c,MAX_PATH,L"%lsscripts\\%ls",p,nom);if(GetFileAttributesW(c)!=INVALID_FILE_ATTRIBUTES)return TRUE;
    swprintf(c,MAX_PATH,L"%ls%ls",p,nom);return GetFileAttributesW(c)!=INVALID_FILE_ATTRIBUTES;
}

static HMODULE module_maps(const WCHAR *ancien) {
    HMODULE m=GetModuleHandleW(ancien);
    return m?m:GetModuleHandleW(L"DOA5LR-Maps.asi");
}
static void attendre_modules_maps(void) {
    static const WCHAR *const noms[4]={L"DOA5LR-Crimson.asi",L"DOA5LR-DNZ-Complete.asi",L"DOA5LR-ExtraStages.asi",L"DOA5LR-Maps.asi"};
    for(int i=0;i<4;i++) {
        if(!fichier_present(noms[i]))continue;
        int n=0;while(!GetModuleHandleW(noms[i]) && n++<120)Sleep(250);
        NOTE("module_maps_charge",i,GetModuleHandleW(noms[i])!=NULL,n);
    }
}
static void wait_map_modules(void) {
    attendre_modules_maps();
    HMODULE extra=module_maps(L"DOA5LR-ExtraStages.asi");
    if(extra) {
        extra_state=(int(__cdecl*)(void))GetProcAddress(extra,"ExtraStagesState");
        extra_ready=(int(__cdecl*)(int))GetProcAddress(extra,"ExtraStagesHostReady");
        if(!extra_state || !extra_ready){extra_state=NULL;extra_ready=NULL;NOTE("extrastages_ancien",0,0,0);}
        else {
            int n=0;while(!extra_state() && n++<600)Sleep(250);
            NOTE("extrastages",extra_state(),0,0);

        }
    }
    if(module_maps(L"DOA5LR-Crimson.asi") || module_maps(L"DOA5LR-DNZ-Complete.asi")) {
        BYTE want=module_maps(L"DOA5LR-Crimson.asi")?27:26;
        int n=0;while(*RVA(0xb280c3)!=want && n++<1200)Sleep(250);
        Sleep(1500);
        NOTE("modules_maps_cases",*RVA(0xb280c3),0,0);
    }
}

static BOOL adopt_menu(void) {
    BYTE count=*RVA(0xb280c3),limit=*RVA(0xb2aa82),mask=*RVA(0xb2aa7e);DWORD list=*(DWORD*)RVA(0xb280c9);
    if(count==25 && limit==25 && mask==0xf1 && list==(DWORD)TILE_INDEX){live_count=25;live_list=0;return TRUE;}
    DWORD entries[27];static const DWORD console[2]={0x2b,0x2c};
    if((count==26 || count==27) && limit==count && mask==(BYTE)(10-count) &&
       readmem((BYTE*)list,entries,count*4) && !memcmp(entries,original_index,sizeof(original_index)) &&
       !memcmp(entries+25,console,(count-25)*4)) {
        live_count=count;live_list=list;
        NOTE("menu_modules_maps_adopte",count,0,0);
        return TRUE;
    }
    NOTE("menu_deja_modifie",count,0,0);
    return FALSE;
}

static BOOL variants_known(const BYTE *cur) {
    DWORD h=fnv1a(cur,TILE_COUNT);
    if(h==FNV_VARIANTES_LAB)NOTE("labdestroyed_garde",0,0,0);
    if(h!=FNV_VARIANTES && h!=FNV_VARIANTES_LAB)return FALSE;
    memcpy(original_variants,cur,TILE_COUNT);
    return TRUE;
}

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    BYTE probe[sizeof(dispatch_signature)];BOOL ready=FALSE;
    for(int n=0;n<1200 && !ready;n++) {
        ready=readmem(RVA(0xa90100),probe,sizeof(probe)) && !memcmp(probe,dispatch_signature,sizeof(probe));
        if(!ready)Sleep(100);
    }
    if(!ready){NOTE("refus_version_game_exe",0,0,0);return 0;}
    if(!GetPrivateProfileIntW(L"General",L"Enabled",1,ini)){NOTE("desactive_ini",0,0,0);return 0;}
    NOTE("debut " VERSION_STAGESLOTS,0,0,0);
    wait_map_modules();
    {
        HMODULE lab=GetModuleHandleW(L"DOA5LR-LabDestroyed.asi");
        int (__cdecl *lab_state)(void)=lab?(int(__cdecl*)(void))GetProcAddress(lab,"LabDestroyedState"):NULL;
        if(lab_state){int n=0;while(!lab_state() && n++<240)Sleep(250);}
    }
    DWORD cur_index[TILE_COUNT];BYTE cur_variants[TILE_COUNT];
    if(!readmem(TILE_INDEX,cur_index,sizeof(cur_index)) || fnv1a(cur_index,sizeof(cur_index))!=FNV_INDEX ||
       !readmem(TILE_VARIANTS,cur_variants,sizeof(cur_variants)) || !variants_known(cur_variants)) {
        NOTE("tables_inattendues",0,0,0);return 0;
    }
    memcpy(original_index,cur_index,sizeof(cur_index));
    CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(!adopt_menu())return 0;

    memset(bg_of_stage,-1,sizeof(bg_of_stage));
    stop_preview=GetPrivateProfileIntW(L"Advanced",L"StopPreview",1,ini)!=0;
    int stages=load_stages();
    if(bg_count) {
        if(!thumb_count)NOTE("fonds_sans_vignette",0,0,0);
        else if((bg_event=CreateEventW(NULL,FALSE,FALSE,NULL)))CloseHandle(CreateThread(NULL,0,bg_worker,NULL,0,NULL));
    }
    if(stages) {
        message_lang=(BYTE(__cdecl*)(DWORD,DWORD))RVA(0xa77220);
        message_text=(const WCHAR*(__cdecl*)(DWORD,DWORD,DWORD))RVA(0xb45ba0);
        picture_original=detour(0xb24ee0,prologue_index_arg,6,(void*)hook_picture);
        name_original=detour(0xb250f0,prologue_index_arg,6,(void*)hook_name);
        NOTE("noms_images",picture_original && name_original,0,0);
    }
    if(thumb_count) {
        texture_vtable=find_texture_vtable();
        if(!texture_vtable)NOTE("vtable_texture_absente",0,0,0);
        else {BOOL hooked=install_menu_hook();NOTE("vignettes_crochet",hooked,0,0);(void)hooked;}
        if(texture_vtable && virtual_count()) {
            record_original=detour(0xbd5b90,prologue_lookup,7,(void*)hook_record);
            texinfo_original=detour(0xbd5b20,prologue_lookup,7,(void*)hook_texinfo);
            if(!record_original || !texinfo_original){NOTE("images_virtuelles_crochets_echec",0,0,0);virtuals_failed=TRUE;}
            else NOTE("images_virtuelles_prevues",virtual_count(),0,0);
        }
    }

    int changes=load_tiles();
    console_tiles=GetPrivateProfileIntW(L"General",L"ConsoleTiles",1,ini)!=0;
    if(console_tiles) {
        fill_tiles_original=RVA(0xb28060);
        prepare_patches();
        if(tile_total>EXT_COUNT)NOTE("cases_en_plus",tile_total-EXT_COUNT,tile_total,0);
        WCHAR list[512];int blocked=0,aliases=0;
        memset(alias_to,0xff,sizeof(alias_to));memset(shown_of,0xff,sizeof(shown_of));
        {
            static WCHAR keys[2048];
            GetPrivateProfileStringW(L"Aliases",NULL,L"",keys,2048,ini);
            for(WCHAR *k=keys;*k;k+=wcslen(k)+1) {
                DWORD shown=wcstoul(k,NULL,0),real;WCHAR v[32];
                GetPrivateProfileStringW(L"Aliases",k,L"",v,32,ini);
                real=wcstoul(v,NULL,0);
                if(shown>=0x4a || real>=0x4a || !v[0]){NOTE("alias_ignore",shown,0,0);continue;}
                alias_to[shown]=(BYTE)real;shown_of[real]=(BYTE)shown;aliases++;
                NOTE("alias",shown,real,0);
            }
        }
        GetPrivateProfileStringW(L"Advanced",L"NoPreview",L"0x2b,0x2c,0x2d,0x12,0x17,0x1c",list,512,ini);
        for(WCHAR *p=list;*p;) {
            WCHAR *end;DWORD v=wcstoul(p,&end,0);
            if(end==p){p++;continue;}
            if(v<INDEX_COUNT){no_preview[v]=1;blocked++;}
            p=end;
        }
        GetPrivateProfileStringW(L"Advanced",L"OpenStopAnims",L"1,0x27",list,512,ini);
        open_stop_count=0;
        for(WCHAR *p=list;*p && open_stop_count<8;) {
            WCHAR *end;DWORD v=wcstoul(p,&end,0);
            if(end==p){p++;continue;}
            open_stop_anims[open_stop_count++]=(WORD)v;p=end;
        }
        if(blocked || aliases) {
            preview_original=detour(0xb28b60,prologue_preview,5,(void*)hook_preview);
            NOTE("apercu_crochet",blocked,aliases,preview_original!=NULL);
            open_skip_addr=RVA(0xb2ad74);
            add_call_patch(0xb2ac4c,open_preview_code,5,(void*)open_preview_stub,"menu open preview");
        }
        if(aliases) {
            add_patch(0xb27e01,4,(DWORD)(RVA(0xa895c0)-RVA(0xb27e05)),(DWORD)((BYTE*)last_stage_c-RVA(0xb27e05)),"last stage call");
            select_original=detour(0xb29540,prologue_unlock,5,(void*)hook_select);
            NOTE("alias_confirmation",select_original!=NULL,0,0);
        }
    }
    {
        WCHAR list[512];int unlocked=0;
        for(int pass=0;pass<2;pass++) {
            GetPrivateProfileStringW(L"Advanced",pass?L"ForceLock":L"ForceUnlock",pass?L"0x12,0x17,0x1c":L"",list,512,ini);
            for(WCHAR *p=list;*p;) {
                WCHAR *end;DWORD v=wcstoul(p,&end,0);
                if(end==p){p++;continue;}
                if(v<INDEX_COUNT){force_unlock[v]=pass?2:1;unlocked++;}
                p=end;
            }
        }

        GetPrivateProfileStringW(L"Advanced",L"ExtraStages",L"",list,512,ini);
        for(WCHAR *p=list;*p;) {
            WCHAR *end;DWORD v=wcstoul(p,&end,0);
            if(end==p){p++;continue;}
            p=end;if(v>=INDEX_COUNT)continue;
            BOOL ready=extra_ready && extra_ready((int)v);
            for(DWORD i=0;i<INDEX_COUNT;i++)if(i==v || alias_to[i]==v) {
                force_unlock[i]=ready?1:2;no_preview[i]=!ready;unlocked++;
            }
            NOTE("niveau_extrastages",v,ready,0);
        }

        if(console_tiles && !preview_original) for(DWORD i=0;i<INDEX_COUNT;i++)if(force_unlock[i]==2 && no_preview[i]) {
            preview_original=detour(0xb28b60,prologue_preview,5,(void*)hook_preview);
            open_skip_addr=RVA(0xb2ad74);
            add_call_patch(0xb2ac4c,open_preview_code,5,(void*)open_preview_stub,"menu open preview");
            NOTE("apercu_crochet_extrastages",preview_original!=NULL,0,0);break;
        }
        if(live_count>=26){no_preview[0x2b]=0;NOTE("dangerzone_apercu_3d",0,0,0);}
        if(live_count>=27){no_preview[0x2c]=no_preview[0x2d]=0;NOTE("crimson_apercu_3d",0,0,0);}
        if(unlocked) {
            unlock_original=detour(0xa892c0,prologue_unlock,5,(void*)hook_unlock);
            NOTE("deverrouillage_force",unlocked,unlock_original!=NULL,0);
        }
    }
    if(!changes && !console_tiles && !stages){NOTE("rien_a_changer",0,0,0);return 0;}

    BOOL tables_applied=FALSE;
    if(console_tiles) {
        {WCHAR v[32];GetPrivateProfileStringW(L"General",L"SignalSound",L"0x10000005",v,32,ini);signal_sound=wcstoul(v,NULL,0);
         {WCHAR l[256];GetPrivateProfileStringW(L"General",L"HideSound",L"0x10000",l,256,ini);hide_count=0;
          for(WCHAR *p=l;*p && hide_count<8;){WCHAR *e;DWORD x=wcstoul(p,&e,0);if(e==p){p++;continue;}hide_sounds[hide_count++]=x;p=e;}
          if(!hide_count){hide_sounds[0]=0;hide_count=1;}}
         GetPrivateProfileStringW(L"General",L"ToggleButtons",L"0x280",v,32,ini);toggle_buttons=wcstoul(v,NULL,0);
         if(!toggle_buttons)toggle_buttons=0x280;}

    }
    for(;;) {
        BOOL online=online_session();
        if(*GUI!=5) {
            if(reload_tiles && !online) {
                reload_tiles=FALSE;
                bg_stage=0xff;
                load_tiles();
                if(patches_applied) {
                    BOOL ok=retile_patches();
                    NOTE("carrousel_reconstruit",tile_total,ok,0);(void)ok;
                }
            }
            if(!online) {
                if(changes && !tables_applied && (tables_applied=write_tables(table_index,table_variants)))NOTE("cases_hors_ligne",0,0,0);
                if(console_tiles && !patches_applied) {
                    patches_applied=write_patches(TRUE);
                    if(patches_applied)NOTE("cases_console",tile_total,0,0);
                    else console_tiles=FALSE;
                }
            } else {
                if(tables_applied && write_tables(original_index,original_variants)){tables_applied=FALSE;NOTE("en_ligne_cases_restaurees",0,0,0);}
                if(patches_applied && write_patches(FALSE)){patches_applied=FALSE;NOTE("en_ligne_carrousel_restaure",0,0,0);}
            }
        }
        Sleep(250);
    }
}

int StageSlots_Packs(void) {
    if(!console_tiles || !groups_shown() || live_count<27)return 0;
    int p=1;
    if(extra_state && extra_state()==1)p|=2;
    if(extra_ready && extra_ready(0x17) && extra_ready(0x20))p|=4;
    if(extra_ready && extra_ready(0x12))p|=8;
    if(extra_ready && extra_ready(0x1c))p|=16;
    return p;
}
int StageSlots_MapsActives(void) {return StageSlots_Packs()!=0;}

int __cdecl DOA5Tools_NiveauxRandom(void) {
    return en_ligne_brut() ? Marque_TousNiveaux() : (console_tiles && groups_shown());
}

void StageSlots_Demarrer(void) {
    if(GetModuleHandleW(L"DOA5LR-StageSlots.asi")){NOTE("module_separe_present",0,0,0);return;}
    base=(BYTE*)GetModuleHandleW(NULL);
    WCHAR dossier[MAX_PATH];
    if(!MultiByteToWideChar(CP_ACP,0,CheminIni(),-1,dossier,MAX_PATH))return;
    WCHAR *sep=wcsrchr(dossier,L'\\');if(!sep)return;
    sep[1]=0;
    swprintf(ini,MAX_PATH,L"%lsDOA5Tools.ini",dossier);
    swprintf(folder,MAX_PATH,L"%lsDOA5LR-StageSlots\\",dossier);
    HANDLE h=CreateThread(NULL,0,worker,NULL,0,NULL);if(h)CloseHandle(h);
}

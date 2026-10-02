#include <windows.h>
#include <d3d9.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include "commun.h"

static BYTE *base;
#define RVA(rva) (base+(rva))
#define JOURNAL(libelle,a,b,c) Journal("ReplayMenu",libelle,(long)(a),(long)(b),(long)(c))
static BOOL readmem(const void *p,void *buf,SIZE_T n) {
    SIZE_T done=0;
    return ReadProcessMemory(GetCurrentProcess(),p,buf,n,&done) && done==n;
}
static DWORD rd32(const void *p) {DWORD v=0;readmem(p,&v,4);return v;}
static const BYTE dispatch_signature[17]={0x80,0x79,0x6a,0,0x74,0x0a,0x8b,1,0x8b,0x90,0x84,0,0,0,0xff,0xe2,0xc3};

static const WCHAR *stage_names[0x4b]={
    [0x01]=L"Scramble",[0x02]=L"Scramble",[0x03]=L"Fuel",[0x04]=L"Fuel",[0x06]=L"Lab",[0x08]=L"Flow",[0x09]=L"Primal",
    [0x0a]=L"Sanctuary",[0x0b]=L"Home",[0x0c]=L"Home",[0x0d]=L"Street",[0x0e]=L"Street",[0x0f]=L"The Show",
    [0x10]=L"The Tiger Show",[0x13]=L"Hot Zone",[0x14]=L"The Ends of the Earth",[0x16]=L"Depth",
    [0x18]=L"Temple of the Dragon",[0x19]=L"Sakura",[0x1a]=L"Fighting Entertainment",[0x1b]=L"Dojo",[0x1d]=L"Sweat",
    [0x1e]=L"Dead or Alive",[0x1f]=L"Arrival",[0x21]=L"Zack Island",[0x22]=L"Forest",[0x23]=L"Sky City Tokyo",
    [0x24]=L"Desert Wasteland",[0x25]=L"Lost World",[0x26]=L"Aircraft Carrier",[0x27]=L"Lorelei",[0x28]=L"Lorelei",
    [0x29]=L"Haunted Lorelei",[0x2a]=L"Haunted Lorelei",

    [0x07]=L"Lab (Destroyed)",[0x15]=L"Snowy Station",[0x2e]=L"Diner Street",[0x2f]=L"Diner Street (2)",[0x30]=L"Subway",
    [0x31]=L"Helena's Yacht (Office)",[0x33]=L"Oasis on the Sea",[0x34]=L"Helena's Yacht (Roof)",
    [0x36]=L"Helena's Yacht (Deck)",[0x37]=L"Helena's Yacht (Helipad)",[0x3a]=L"Oasis on the Sea (2)",
};
static void stage_name(BYTE idx,WCHAR *out,int n) {
    if(idx<0x4b && stage_names[idx]){lstrcpynW(out,stage_names[idx],n);return;}
    const WCHAR *t=((const WCHAR*(__cdecl*)(DWORD))RVA(0x7250F0))(idx);
    BOOL blank=TRUE;
    if(t && !IsBadReadPtr(t,2))for(const WCHAR *c=t;*c && c-t<64;c++)if(*c!=L' '){blank=FALSE;break;}
    if(!blank)lstrcpynW(out,t,n);else swprintf(out,n,L"Stage 0x%02x",idx);
}

static const WCHAR *chara_names[0x31]={
    [0x00]=L"Zack",[0x01]=L"Tina",[0x02]=L"Jann Lee",[0x03]=L"Ein",[0x04]=L"Hayabusa",[0x05]=L"Kasumi",[0x06]=L"Gen Fu",
    [0x07]=L"Helena",[0x08]=L"Leon",[0x09]=L"Bass",[0x0a]=L"Kokoro",[0x0b]=L"Hayate",[0x0c]=L"Leifang",[0x0d]=L"Ayane",
    [0x0e]=L"Eliot",[0x0f]=L"Lisa",[0x10]=L"Alpha-152",[0x13]=L"Brad Wong",[0x14]=L"Christie",[0x15]=L"Hitomi",
    [0x18]=L"Bayman",[0x1d]=L"Rig",[0x1e]=L"Mila",[0x1f]=L"Akira",[0x20]=L"Sarah",[0x21]=L"Pai",[0x27]=L"Momiji",
    [0x28]=L"Rachel",[0x29]=L"Jacky",[0x2a]=L"Marie",[0x2b]=L"Phase 4",[0x2c]=L"Nyotengu",[0x2d]=L"Honoka",
    [0x2e]=L"Raidou",[0x2f]=L"Naotora",[0x30]=L"Mai",
};
static void chara_name(BYTE c,WCHAR *out,int n) {
    if(c<0x31 && chara_names[c])lstrcpynW(out,chara_names[c],n);else swprintf(out,n,L"#%02x",c);
}

static WCHAR save_dir[MAX_PATH];
static BOOL find_save_dir(const char *name) {
    WCHAR docs[MAX_PATH],pat[MAX_PATH],path[MAX_PATH];
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_PERSONAL,NULL,0,docs)))return FALSE;
    swprintf(pat,MAX_PATH,L"%ls\\KoeiTecmo\\DOA5LR\\*",docs);
    WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(pat,&fd);
    if(h==INVALID_HANDLE_VALUE)return FALSE;
    BOOL found=FALSE;
    do {
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0]=='.')continue;
        swprintf(path,MAX_PATH,L"%ls\\KoeiTecmo\\DOA5LR\\%ls\\REPLY_SAVE\\%hs",docs,fd.cFileName,name);
        if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES) {
            swprintf(save_dir,MAX_PATH,L"%ls\\KoeiTecmo\\DOA5LR\\%ls\\REPLY_SAVE",docs,fd.cFileName);found=TRUE;
        }
    } while(!found && FindNextFileW(h,&fd));
    FindClose(h);
    return found;
}
static int lzss(const BYTE *src,int n,BYTE *out,int want) {
    static BYTE ring[4096];memset(ring,0,sizeof ring);
    int r=4078,i=0,o=0;unsigned flags=0;
    while(i<n && o<want) {
        flags>>=1;
        if(!(flags&0x100)){flags=src[i++]|0xff00;if(i>=n)break;}
        if(flags&1){BYTE c=src[i++];out[o++]=c;ring[r]=c;r=(r+1)&4095;}
        else {
            if(i+1>=n)break;
            int a=src[i],b=src[i+1];i+=2;
            int p=a|((b&0xf0)<<4),l=(b&0x0f)+3;
            for(int k=0;k<l && o<want;k++){BYTE c=ring[(p+k)&4095];out[o++]=c;ring[r]=c;r=(r+1)&4095;}
        }
    }
    return o;
}

#define FACE_W 93
#define FACE_H 54
typedef struct {BYTE tried,ok;DWORD px[FACE_W*FACE_H];} Face;
static Face faces[0x31];
static IDirect3DTexture9 *texture_at(DWORD obj,UINT w,UINT h,D3DFORMAT fmt) {
    MEMORY_BASIC_INFORMATION mi;DWORD vt=rd32((void*)obj);
    if(obj<0x10000 || !vt || !VirtualQuery((void*)vt,&mi,sizeof mi) || mi.Type!=MEM_IMAGE)return NULL;
    IDirect3DTexture9 *t=(IDirect3DTexture9*)obj;D3DSURFACE_DESC d;
    if(FAILED(IDirect3DTexture9_GetLevelDesc(t,0,&d)) || d.Width!=w || (h && d.Height!=h) || d.Format!=fmt)return NULL;
    return t;
}

static BOOL capture_picture(DWORD id,int w,int h,DWORD *out) {
    if(id==0xffffffff)return FALSE;
    DWORD rec=((DWORD(__cdecl*)(DWORD))RVA(0x7CF680))(id);
    if(!rec)return FALSE;
    DWORD texi=rd32((void*)(rec+0x14)),group=id>>16;
    if(group>=rd32(RVA(0x20825D4)))return FALSE;
    DWORD entry=rd32(RVA(0x20825D8))+group*0x28,blk=rd32((void*)(entry+0xc));
    DWORD tb=blk?rd32((void*)(blk+0x20)):0;if(tb)tb+=blk;
    DWORD ti=tb?rd32((void*)(tb+4)):0;if(ti)ti+=blk;
    char tag[8];
    if(!ti || !readmem((void*)ti,tag,8) || memcmp(tag,"texinfo",8) || texi>=rd32((void*)(ti+0x10)))return FALSE;
    DWORD e=ti+0x20+texi*12;float v=0;readmem((void*)(e+8),&v,4);
    IDirect3DTexture9 *img=texture_at(rd32((void*)rd32((void*)rd32((void*)e))),w,h,D3DFMT_L8);
    IDirect3DTexture9 *pal=texture_at(rd32((void*)rd32((void*)rd32((void*)(e+4)))),256,0,D3DFMT_A8R8G8B8);
    D3DSURFACE_DESC pd;int rows=8;
    if(pal && SUCCEEDED(IDirect3DTexture9_GetLevelDesc(pal,0,&pd)))rows=(int)pd.Height;
    int row=(int)(v*rows);
    if(!img || !pal || row<0 || row>=rows){JOURNAL("image_non_reconnue",id,0,0);return FALSE;}
    DWORD palette[256];D3DLOCKED_RECT lr;RECT rr={0,row,256,row+1};
    if(FAILED(IDirect3DTexture9_LockRect(pal,0,&lr,&rr,D3DLOCK_READONLY)))return FALSE;
    memcpy(palette,lr.pBits,sizeof palette);IDirect3DTexture9_UnlockRect(pal,0);
    if(FAILED(IDirect3DTexture9_LockRect(img,0,&lr,NULL,D3DLOCK_READONLY)))return FALSE;
    for(int y=0;y<h;y++){BYTE *src=(BYTE*)lr.pBits+y*lr.Pitch;for(int x=0;x<w;x++)out[y*w+x]=palette[src[x]];}
    IDirect3DTexture9_UnlockRect(img,0);
    return TRUE;
}
static void capture_face(BYTE c) {
    if(c>0x30 || faces[c].tried)return;
    faces[c].tried=1;
    faces[c].ok=capture_picture(((DWORD(__cdecl*)(DWORD))RVA(0x256B90))(c),FACE_W,FACE_H,faces[c].px);
}

#define THUMB_W 124
#define THUMB_H 72
typedef struct {BYTE tried,ok;DWORD px[THUMB_W*THUMB_H];} Thumb;
static Thumb thumbs[0x4b];
static BOOL thumbs_asked;
static void capture_thumbs(void);

static void *steam_friends(void) {
    HMODULE m=GetModuleHandleW(L"steam_api.dll");
    void *(*fn)(void)=m?(void*(*)(void))GetProcAddress(m,"SteamFriends"):NULL;
    return fn?fn():NULL;
}
static void persona(void *friends,unsigned long long id,WCHAR *out,int n) {
    out[0]=0;
    if(!friends || !id)return;
    void **vt=*(void***)friends;
    const char *(__attribute__((thiscall)) *get)(void*,unsigned long long)=vt[7];
    const char *s=get(friends,id);
    if(!s || !strcmp(s,"[unknown]")){lstrcpynW(out,L"?",n);return;}
    if(!MultiByteToWideChar(CP_UTF8,0,s,-1,out,n))lstrcpynW(out,L"?",n);
}

typedef struct {
    int number;
    BYTE ch1,ch2;
    BYTE stage_idx;
    WCHAR p1[64],p2[64],c1[32],c2[32],stage[48],mode[16],when[32],dur[16];
    unsigned long long s1,s2;
    SYSTEMTIME lt;
    BOOL ok,archived;
} Row;
static Row rows[100];
static WCHAR own_name[64];
static volatile LONG row_count,list_version;
static CRITICAL_SECTION lock;

#define TOUS 0xff

typedef struct {char file[0x41];WCHAR arch[MAX_PATH];} Copie;
static Copie copies[100];
static int copy_count;
static BYTE archive_view=TOUS;
static int archive_button=0x40,archive_key=VK_INSERT;
static int is_copy(const char *file) {
    for(int i=0;i<copy_count;i++)if(!strcmp(copies[i].file,file))return i;
    return -1;
}
static BYTE chara_filter=TOUS;
static WCHAR player_filter[64];
static LONG view[100];
static volatile LONG view_count;
static int chara_button=0x1000,player_button=0x2000;
static int chara_key=VK_F5,player_key=VK_F6;
static BOOL row_matches(const Row *r,BYTE chara,const WCHAR *player) {
    if(chara==TOUS && !player[0])return TRUE;
    if(!r->ok)return FALSE;
    if(chara!=TOUS && r->ch1!=chara && r->ch2!=chara)return FALSE;
    if(player[0] && lstrcmpW(r->p1,player) && lstrcmpW(r->p2,player))return FALSE;
    return TRUE;
}
static BOOL is_opponent_name(const WCHAR *n) {
    return n[0] && lstrcmpW(n,own_name) && lstrcmpW(n,L"?") && lstrcmpW(n,L"CPU") && lstrcmpW(n,L"P1") && lstrcmpW(n,L"P2");
}

static void rebuild_view(void) {
    LONG n=row_count,c=0;
    for(LONG k=0;k<n;k++)if(row_matches(&rows[k],chara_filter,player_filter))view[c++]=k;
    if(!c && n>0 && (chara_filter!=TOUS || player_filter[0]) && archive_view==TOUS) {
        chara_filter=TOUS;player_filter[0]=0;
        for(LONG k=0;k<n;k++)view[c++]=k;
    }
    InterlockedExchange(&view_count,c);
}
static LONG view_pos(LONG pos) {
    for(LONG v=0;v<view_count;v++)if(view[v]==pos)return v;
    return -1;
}
static void archive_counts(int *freq);
static void next_chara(void) {
    int freq[256]={0};LONG n=row_count;
    for(LONG k=0;k<n;k++)if(rows[k].ok && row_matches(&rows[k],TOUS,player_filter)) {
        freq[rows[k].ch1]++;if(rows[k].ch2!=rows[k].ch1)freq[rows[k].ch2]++;
    }
    if(own_name[0] && !lstrcmpW(player_filter,own_name))archive_counts(freq);
    int order[256],m=0;
    for(int c=0;c<256;c++)if(freq[c] && c!=TOUS)order[m++]=c;
    for(int i=1;i<m;i++)for(int j=i;j>0 && freq[order[j]]>freq[order[j-1]];j--){int t=order[j];order[j]=order[j-1];order[j-1]=t;}
    int at=-1;
    for(int i=0;i<m;i++)if(order[i]==chara_filter)at=i;
    chara_filter=at+1<m?(BYTE)order[at+1]:TOUS;
}
static void next_player(void) {
    const WCHAR *names[200];int freq[200],m=0;LONG n=row_count;
    if(own_name[0]){names[0]=own_name;freq[0]=1<<30;m=1;}
    for(LONG k=0;k<n;k++) {
        if(!rows[k].ok || !row_matches(&rows[k],chara_filter,L""))continue;
        const WCHAR *p[2]={rows[k].p1,rows[k].p2};
        for(int s=0;s<2;s++) {
            if(!is_opponent_name(p[s]) || (s==1 && !lstrcmpW(p[1],p[0])))continue;
            int i=0;while(i<m && lstrcmpW(names[i],p[s]))i++;
            if(i==m){if(m>=200)continue;names[m]=p[s];freq[m]=0;m++;}
            freq[i]++;
        }
    }
    for(int i=1;i<m;i++)for(int j=i;j>0 && freq[j]>freq[j-1];j--) {
        int t=freq[j];freq[j]=freq[j-1];freq[j-1]=t;const WCHAR *q=names[j];names[j]=names[j-1];names[j-1]=q;
    }
    int at=-1;
    for(int i=0;i<m;i++)if(player_filter[0] && !lstrcmpW(names[i],player_filter))at=i;
    if(at+1<m)lstrcpynW(player_filter,names[at+1],64);else player_filter[0]=0;
}

static void build_list(BYTE *menu) {
    int count=(int)rd32(RVA(0xF915D8));
    JOURNAL("liste",count,0,0);
    if(count<0 || count>100)return;
    if(count==0){InterlockedExchange(&row_count,0);InterlockedExchange(&view_count,0);InterlockedIncrement(&list_version);return;}
    char first[0x41]={0};memcpy(first,RVA(0xF915DC),0x40);
    if(!save_dir[0] && !find_save_dir(first)){JOURNAL("dossier_introuvable",0,0,0);return;}
    void *friends=steam_friends();
    if(friends && !own_name[0]) {
        const char *(__attribute__((thiscall)) *get)(void*)=(*(void***)friends)[0];
        const char *me=get(friends);
        if(me && !MultiByteToWideChar(CP_UTF8,0,me,-1,own_name,64))own_name[0]=0;
    }
    static Row tmp[100];
    static BYTE file[0x20000],dec[0x200];
    DWORD t0=GetTickCount();
    for(int pos=0;pos<count;pos++) {
        Row *r=&tmp[pos];memset(r,0,sizeof *r);r->number=pos+1;
        int idx=(int)rd32(menu+0x1888+pos*0x2c);
        if(idx<0 || idx>=count)continue;
        char name[0x41]={0};memcpy(name,RVA(0xF915DC)+idx*0x40,0x40);
        WCHAR path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\%hs",save_dir,name);
        HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
        DWORD got=0;
        if(h!=INVALID_HANDLE_VALUE){ReadFile(h,file,sizeof file,&got,NULL);CloseHandle(h);}
        int n=lzss(file,(int)got,dec,sizeof dec);
        if(n<0x1b8 || memcmp(dec,"IBPR",4)){JOURNAL("replay_illisible",pos+1,0,0);continue;}
        BYTE stage=dec[0x40];
        DWORD dur;memcpy(&dur,dec+0xc,4);
        memcpy(&r->s1,dec+0xd0,8);memcpy(&r->s2,dec+0xf8,8);
        SYSTEMTIME u,l;memcpy(&u,dec+0x1a8,sizeof u);
        if(!SystemTimeToTzSpecificLocalTime(NULL,&u,&l))l=u;
        int mode=(name[5]-'0')*10+(name[6]-'0');
        lstrcpynW(r->mode,mode==32?L"RANKED MATCH":mode>=32?L"LOBBY MATCH":mode>=4 && mode<8?L"VERSUS":L"REPLAY",16);
        stage_name(stage,r->stage,48);r->stage_idx=stage;
        chara_name(dec[0x4e],r->c1,32);chara_name(dec[0x6e],r->c2,32);
        r->ch1=dec[0x4e];r->ch2=dec[0x6e];capture_face(r->ch1);capture_face(r->ch2);
        if(r->s1)persona(friends,r->s1,r->p1,64);else lstrcpynW(r->p1,own_name[0]?own_name:L"P1",64);
        if(r->s2)persona(friends,r->s2,r->p2,64);else lstrcpynW(r->p2,mode>=32?L"P2":L"CPU",64);
        swprintf(r->when,32,L"%02u/%02u/%04u  %02u:%02u",l.wDay,l.wMonth,l.wYear,l.wHour,l.wMinute);
        swprintf(r->dur,16,L"%02lu:%02lu",dur/60/60,dur/60%60);
        r->lt=l;r->archived=is_copy(name)>=0;
        r->ok=TRUE;
    }
    EnterCriticalSection(&lock);
    memcpy(rows,tmp,sizeof(Row)*count);InterlockedExchange(&row_count,count);rebuild_view();
    InterlockedIncrement(&list_version);
    LeaveCriticalSection(&lock);
    JOURNAL("liste_prete",count,GetTickCount()-t0,0);
}

static void *detour(DWORD rva,const BYTE *prologue,int size,void *hook) {
    BYTE *target=RVA(rva),cur[8];
    if(!readmem(target,cur,size) || memcmp(cur,prologue,size))return NULL;
    BYTE *tramp=VirtualAlloc(NULL,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!tramp)return NULL;
    memcpy(tramp,prologue,size);
    tramp[size]=0xe9;*(DWORD*)(tramp+size+1)=(DWORD)(target+size)-(DWORD)(tramp+size+5);
    DWORD old,unused;
    if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(tramp,0,MEM_RELEASE);return NULL;}
    target[0]=0xe9;*(DWORD*)(target+1)=(DWORD)hook-(DWORD)(target+5);
    for(int i=5;i<size;i++)target[i]=0x90;
    VirtualProtect(target,size,old,&unused);
    FlushInstructionCache(GetCurrentProcess(),target,size);
    return tramp;
}

static void *detour_shared(DWORD rva,const BYTE *prologue,int size,void *hook) {
    void *t=detour(rva,prologue,size,hook);
    if(t)return t;
    BYTE *target=RVA(rva),cur[5];
    if(!readmem(target,cur,5) || cur[0]!=0xe9)return NULL;
    void *prev=target+5+*(LONG*)(cur+1);
    DWORD old,unused;
    if(!VirtualProtect(target,5,PAGE_EXECUTE_READWRITE,&old))return NULL;
    InterlockedExchange((LONG*)(target+1),(LONG)((DWORD)hook-(DWORD)(target+5)));
    VirtualProtect(target,5,old,&unused);
    FlushInstructionCache(GetCurrentProcess(),target,5);
    return prev;
}
static const BYTE prologue_sound[6]={0x55,0x8b,0xec,0x83,0xec,0x0c};
void *rm_sound_original;
volatile DWORD rm_mute_moves;
__attribute__((naked)) static void sound_hook(void) {
    __asm__ volatile("cmpl $0,_rm_mute_moves\n je 1f\n"
                     "cmpl $0x10001,4(%esp)\n je 2f\n cmpl $0x1001d,4(%esp)\n je 2f\n"
                     "1: jmp *_rm_sound_original\n"
                     "2: xor %eax,%eax\n ret\n");
}
static void play_sound(DWORD id) {
    ((DWORD(__cdecl*)(DWORD,DWORD))(rm_sound_original?rm_sound_original:(void*)RVA(0x557C40)))(id,0);
}

static void capture_thumbs(void) {
    BOOL missing=FALSE;LONG n=row_count;
    for(LONG k=0;k<n;k++){BYTE st=rows[k].stage_idx;if(st<0x4b && !thumbs[st].tried){missing=TRUE;break;}}
    if(!missing && !thumbs_asked)return;
    BOOL loaded=((char(__cdecl*)(DWORD))RVA(0x7D5A00))(0xca)!=0;
    if(!loaded) {
        if(!thumbs_asked){thumbs_asked=TRUE;((char(__cdecl*)(DWORD,DWORD,DWORD))RVA(0x7D59A0))(0xca,0,0);JOURNAL("miniatures_demandees",0,0,0);}
        return;
    }
    int got=0;
    for(LONG k=0;k<n;k++) {
        BYTE st=rows[k].stage_idx;
        if(st>=0x4b || thumbs[st].tried)continue;
        thumbs[st].tried=1;
        DWORD id=((DWORD(__cdecl*)(DWORD))RVA(0x724EE0))(st);
        thumbs[st].ok=(id>>16)==0xca && capture_picture(id,THUMB_W,THUMB_H,thumbs[st].px);
        if(thumbs[st].ok)got++;
    }
    if(thumbs_asked){thumbs_asked=FALSE;((void(__cdecl*)(DWORD))RVA(0x7D5A70))(0xca);}
    JOURNAL("miniatures_copiees",got,0,0);
    InterlockedIncrement(&list_version);
}

void *rm_text_original;
static volatile DWORD menu_tick;
static volatile LONG menu_shown;
__attribute__((used)) const WCHAR *__cdecl rm_text_c(DWORD lang,DWORD id,DWORD flags) {
    const WCHAR *t=((const WCHAR*(__cdecl*)(DWORD,DWORD,DWORD))rm_text_original)(lang,id,flags);

    if(id!=0x1a0013 || !t || IsBadReadPtr(t,2) || wcsstr(t,L"@[pad:Ru]"))return t;
    static WCHAR buf[2][300];static int k;WCHAR *b=buf[k^=1];
    swprintf(b,300,L"@[pad:Rl]Archive  @[pad:Ru]Delete  %ls",t);
    return b;
}
__attribute__((naked)) static void hook_text(void) {
    __asm__ volatile("push %ecx\n push %edx\n push 20(%esp)\n push 20(%esp)\n push 20(%esp)\n"
                     "call _rm_text_c\n add $12,%esp\n pop %edx\n pop %ecx\n ret\n");
}
static const BYTE prologue_text[5]={0x55,0x8b,0xec,0x6a,0xff};

static volatile LONG cursor_pos=-1;
static LONG sel=-1;
static DWORD *forced_at,forced_orig,forced_val;
static BYTE *home_tile;
static DWORD *cursor_data(BYTE *menu) {
    DWORD player=rd32(menu+0x186c);
    BYTE *e=((BYTE*(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD))RVA(0x7D6950))(menu,player,0x8d);
    BYTE *d=e?(BYTE*)rd32(e+0xc):NULL;
    return d?(DWORD*)(d+4):NULL;
}
static void release_tile(void) {
    if(forced_at && *forced_at==forced_val)*forced_at=forced_orig;
    forced_at=NULL;
}
static void force_tile(BYTE *menu) {
    DWORD *p=cursor_data(menu);
    if(p!=forced_at){release_tile();if(p){forced_at=p;forced_orig=*p;}}
    if(p && sel>=0){forced_val=(DWORD)sel;*p=forced_val;}
}
void *rm_update_original;
static BYTE *seen_menu;static int wait_frames;

#define PAD_UP 0x10001
#define PAD_DOWN 0x20002
#define PAD_LEFT 0x40004
#define PAD_RIGHT 0x80008
static DWORD prev_held,repeat_at,dirs_held;
static BOOL nav_repeat;
static int nav_step(void) {
    DWORD held=0;for(int d=0;d<5;d++)held|=rd32(RVA(0x2057928+d*0x2c));
    DWORD dirs=held&(PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT),pressed=dirs&~prev_held;
    dirs_held=dirs;
    prev_held=dirs;
    DWORD now=GetTickCount();
    DWORD use=0;
    nav_repeat=FALSE;
    if(pressed){use=pressed;repeat_at=now+380;}
    else if(dirs && (LONG)(now-repeat_at)>=0){use=dirs;repeat_at=now+70;nav_repeat=TRUE;}
    if(use&PAD_DOWN)return 1;
    if(use&PAD_UP)return -1;
    if(use&PAD_RIGHT)return 1000;
    if(use&PAD_LEFT)return -1000;
    return 0;
}
static int visible_rows=7;

static BOOL launched;static LONG prev_focus=-1;static int box_wait;static volatile LONG loading_pos=-1;
static LONG focus_list(BYTE *menu,DWORD player) {
    DWORD n=rd32(menu+0x54);BYTE *arr=(BYTE*)rd32(menu+0x5c);
    for(DWORD i=0;arr && i<n && i<16;i++) {
        BYTE *e=(BYTE*)rd32(arr+i*4);
        if(e && rd32(e)==player){BYTE *l=(BYTE*)rd32(e+4);return l?(LONG)rd32(l):-1;}
    }
    return -1;
}
static volatile LONG armed_pos=-1;static DWORD armed_tick,prev_delete;static BOOL prev_key=TRUE;
static void disarm(void){if(armed_pos>=0){InterlockedExchange(&armed_pos,-1);InterlockedIncrement(&list_version);}}
static int recycle;
static BOOL move_to_bin(const char *name) {
    WCHAR src[MAX_PATH],bin[MAX_PATH],dst[MAX_PATH];
    if(!save_dir[0])return FALSE;
    swprintf(src,MAX_PATH,L"%ls\\%hs",save_dir,name);
    if(!recycle) {
        if(!DeleteFileW(src)){JOURNAL("suppression_refusee",GetLastError(),0,0);return FALSE;}
        return TRUE;
    }
    lstrcpynW(bin,save_dir,MAX_PATH);
    WCHAR *slash=wcsrchr(bin,L'\\');if(!slash)return FALSE;
    wcscpy(slash,L"\\REPLY_CORBEILLE");
    CreateDirectoryW(bin,NULL);
    swprintf(dst,MAX_PATH,L"%ls\\%hs",bin,name);
    for(int n=2;GetFileAttributesW(dst)!=INVALID_FILE_ATTRIBUTES && n<100;n++)swprintf(dst,MAX_PATH,L"%ls\\%hs_%d",bin,name,n);
    if(!MoveFileExW(src,dst,0)){JOURNAL("deplacement_refuse",GetLastError(),0,0);return FALSE;}
    return TRUE;
}

static BOOL sibling_dir(const WCHAR *leaf,WCHAR *out) {
    if(!save_dir[0])return FALSE;
    lstrcpynW(out,save_dir,MAX_PATH);
    WCHAR *slash=wcsrchr(out,L'\\');if(!slash)return FALSE;
    swprintf(slash,MAX_PATH-(slash-out),L"\\%ls",leaf);
    return TRUE;
}

static void clean_name(const WCHAR *in,WCHAR *out,int n) {
    int o=0;
    for(;*in && o<n-1;in++)out[o++]=(*in<32 || wcschr(L"\\/:*?\"<>|",*in))?L'_':*in;
    while(o>0 && (out[o-1]==L'.' || out[o-1]==L' '))o--;
    out[o]=0;
    if(!o)lstrcpynW(out,L"Unknown",n);
}
static void chara_folder(BYTE c,WCHAR *out,int n){WCHAR t[32];chara_name(c,t,32);clean_name(t,out,n);}

static int my_side(const Row *r) {
    if(!own_name[0])return -1;
    if(!lstrcmpW(r->p1,own_name))return 0;
    if(!lstrcmpW(r->p2,own_name))return 1;
    return -1;
}

static BOOL original_name(const WCHAR *f,char *out) {
    const WCHAR *p=NULL,*q=f;
    while((q=wcsstr(q,L"__REPLY"))){p=q+2;q+=2;}
    if(!p || lstrlenW(p)>=0x40)return FALSE;
    int i=0;
    for(;p[i];i++){if(p[i]<32 || p[i]>126 || wcschr(L"\\/:*?\"<>|",p[i]))return FALSE;out[i]=(char)p[i];}
    out[i]=0;
    return TRUE;
}

static BOOL archive_file(const Row *r,const char *file,WCHAR *arch) {
    int side=my_side(r);
    if(side<0){JOURNAL("archive_cote_inconnu",0,0,0);return FALSE;}
    WCHAR root[MAX_PATH],dir[MAX_PATH],src[MAX_PATH],me[24],opp[24],c1[32],c2[32],stage[48];
    if(!sibling_dir(L"REPLY_ARCHIVE",root))return FALSE;
    chara_folder(side?r->ch2:r->ch1,c1,32);chara_folder(side?r->ch1:r->ch2,c2,32);
    swprintf(dir,MAX_PATH,L"%ls\\%ls",root,c1);
    CreateDirectoryW(root,NULL);CreateDirectoryW(dir,NULL);
    clean_name(side?r->p2:r->p1,me,24);clean_name(side?r->p1:r->p2,opp,24);clean_name(r->stage,stage,48);
    const SYSTEMTIME *t=&r->lt;
    if(swprintf(arch,MAX_PATH,L"%ls\\%ls_vs_%ls_%ls_vs_%ls_%ls_%04u-%02u-%02u_%02uh%02u__%hs",dir,me,opp,c1,c2,stage,
                t->wYear,t->wMonth,t->wDay,t->wHour,t->wMinute,file)<0)
        swprintf(arch,MAX_PATH,L"%ls\\__%hs",dir,file);
    swprintf(src,MAX_PATH,L"%ls\\%hs",save_dir,file);
    if(!MoveFileExW(src,arch,0)){JOURNAL("archivage_refuse",GetLastError(),0,0);return FALSE;}
    return TRUE;
}

static BOOL discard_archive(const WCHAR *arch) {
    WCHAR bin[MAX_PATH],dst[MAX_PATH];
    if(!recycle) {
        if(DeleteFileW(arch))return TRUE;
        JOURNAL("suppression_refusee",GetLastError(),0,0);return FALSE;
    }
    const WCHAR *f=wcsrchr(arch,L'\\');
    if(!f || !sibling_dir(L"REPLY_CORBEILLE",bin))return FALSE;
    CreateDirectoryW(bin,NULL);
    swprintf(dst,MAX_PATH,L"%ls%ls",bin,f);
    if(MoveFileExW(arch,dst,0))return TRUE;
    JOURNAL("deplacement_refuse",GetLastError(),0,0);return FALSE;
}
static void untrack(int i){copies[i]=copies[--copy_count];}

static void archive_counts(int *freq) {
    WCHAR root[MAX_PATH],pat[MAX_PATH];
    if(!sibling_dir(L"REPLY_ARCHIVE",root))return;
    for(int c=0;c<0x31;c++) {
        if(!chara_names[c])continue;
        WCHAR folder[32];chara_folder((BYTE)c,folder,32);
        swprintf(pat,MAX_PATH,L"%ls\\%ls\\*__REPLY*",root,folder);
        WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(pat,&fd);
        if(h==INVALID_HANDLE_VALUE)continue;
        do if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))freq[c]++;while(FindNextFileW(h,&fd));
        FindClose(h);
    }
}

static int copies_in(BYTE c) {
    WCHAR root[MAX_PATH],folder[32],pat[MAX_PATH],arch[MAX_PATH],dst[MAX_PATH];
    archive_view=c;
    if(!sibling_dir(L"REPLY_ARCHIVE",root))return 0;
    chara_folder(c,folder,32);
    swprintf(pat,MAX_PATH,L"%ls\\%ls\\*__REPLY*",root,folder);
    int files=(int)rd32(RVA(0xF915D8)),got=0,full=0;
    WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(pat,&fd);
    if(h==INVALID_HANDLE_VALUE){JOURNAL("archives_sorties",0,0,0);return 0;}
    do {
        char file[0x41];
        if((fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || !original_name(fd.cFileName,file) || copy_count>=100)continue;
        swprintf(arch,MAX_PATH,L"%ls\\%ls\\%ls",root,folder,fd.cFileName);
        swprintf(dst,MAX_PATH,L"%ls\\%hs",save_dir,file);
        BOOL there=GetFileAttributesW(dst)!=INVALID_FILE_ATTRIBUTES;
        if(!there && files+got>=100){full++;continue;}
        if(!there && !CopyFileW(arch,dst,TRUE))continue;
        if(!there)got++;
        lstrcpyA(copies[copy_count].file,file);lstrcpynW(copies[copy_count].arch,arch,MAX_PATH);copy_count++;
    } while(FindNextFileW(h,&fd));
    FindClose(h);
    JOURNAL("archives_sorties",got,copy_count,full);
    return got;
}

static int copies_out(void) {
    int n=0;
    for(int i=0;i<copy_count;i++) {
        WCHAR dst[MAX_PATH];swprintf(dst,MAX_PATH,L"%ls\\%hs",save_dir,copies[i].file);
        if(GetFileAttributesW(copies[i].arch)!=INVALID_FILE_ATTRIBUTES && DeleteFileW(dst))n++;
    }
    copy_count=0;archive_view=TOUS;
    if(n)JOURNAL("archives_rangees",n,0,0);
    return n;
}

static int stale_copies(void) {
    WCHAR root[MAX_PATH],pat[MAX_PATH],dst[MAX_PATH];
    if(!sibling_dir(L"REPLY_ARCHIVE",root))return 0;
    int n=0;
    for(int c=0;c<0x31;c++) {
        if(!chara_names[c])continue;
        WCHAR folder[32];chara_folder((BYTE)c,folder,32);
        swprintf(pat,MAX_PATH,L"%ls\\%ls\\*__REPLY*",root,folder);
        WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(pat,&fd);
        if(h==INVALID_HANDLE_VALUE)continue;
        do {
            char file[0x41];
            if((fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || !original_name(fd.cFileName,file) || is_copy(file)>=0)continue;
            swprintf(dst,MAX_PATH,L"%ls\\%hs",save_dir,file);
            if(GetFileAttributesW(dst)!=INVALID_FILE_ATTRIBUTES && DeleteFileW(dst))n++;
        } while(FindNextFileW(h,&fd));
        FindClose(h);
    }
    if(n)JOURNAL("copies_orphelines",n,0,0);
    return n;
}

static void ask_relist(BYTE *menu) {
    release_tile();home_tile=NULL;
    BYTE pad=((BYTE(__cdecl*)(void))RVA(0x67A780))();
    ((void(__cdecl*)(DWORD))RVA(0x133580))(pad);
    menu[0x17a4]=1;
}

static void archive_sync(BYTE *menu) {
    BYTE want=own_name[0] && chara_filter!=TOUS && !lstrcmpW(player_filter,own_name)?chara_filter:TOUS;
    if(want==archive_view)return;
    int n=copies_out();
    if(want!=TOUS)n+=copies_in(want);
    if(n)ask_relist(menu);
}

static DWORD prev_filter;
static BOOL prev_chara_key=TRUE,prev_player_key=TRUE;
static BOOL key_pressed(int vk,BOOL *prev) {
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    BOOL down=vk && pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000);
    BOOL r=down && !*prev;*prev=down;
    return r;
}
static void filter_input(BOOL use,BYTE *menu) {
    DWORD held=0;for(int d=0;d<5;d++)held|=rd32(RVA(0x2057928+d*0x2c));
    DWORD pressed=held&~prev_filter;prev_filter=held;
    BOOL kc=key_pressed(chara_key,&prev_chara_key),kp=key_pressed(player_key,&prev_player_key);
    if(!use)return;
    static int noted;
    DWORD buttons=pressed&~(PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT);
    for(int b=0;b<32 && buttons && noted<30;b++)if(buttons&(1u<<b)){noted++;JOURNAL("bouton",1u<<b,0,0);}
    BOOL c=kc || (chara_button && (pressed&chara_button)),p=kp || (player_button && (pressed&player_button));
    if(!c && !p)return;
    disarm();
    EnterCriticalSection(&lock);
    if(c)next_chara();
    if(p)next_player();
    LeaveCriticalSection(&lock);
    archive_sync(menu);
    EnterCriticalSection(&lock);
    rebuild_view();
    if(view_pos(sel)<0 && view_count>0)sel=view[0];
    LeaveCriticalSection(&lock);
    InterlockedIncrement(&list_version);
    play_sound(0x10001);
    JOURNAL("filtre",chara_filter==TOUS?-1:chara_filter,player_filter[0]!=0,view_count);
}

static void remove_row(LONG count) {
    LONG v=view_pos(sel);
    EnterCriticalSection(&lock);
    memmove(&rows[sel],&rows[sel+1],sizeof(Row)*(count-sel-1));
    for(LONG k=sel;k<count-1;k++)rows[k].number=k+1;
    InterlockedExchange(&row_count,count-1);rebuild_view();InterlockedIncrement(&list_version);
    LeaveCriticalSection(&lock);
    if(v>=view_count)v=view_count-1;
    if(v<0)v=0;
    sel=view_count>0?view[v]:0;
}

static int armed_kind;
static BOOL prev_arc_key=TRUE;
static void action_input(BYTE *menu,LONG count) {
    DWORD held=0;for(int d=0;d<5;d++)held|=rd32(RVA(0x2057928+d*0x2c));
    BOOL del=(held&0x80) && !(prev_delete&0x80),arc=archive_button && (held&archive_button) && !(prev_delete&archive_button);
    prev_delete=held;
    if(key_pressed(VK_DELETE,&prev_key))del=TRUE;
    if(key_pressed(archive_key,&prev_arc_key))arc=TRUE;
    if(armed_pos>=0 && (armed_pos!=sel || GetTickCount()-armed_tick>2500))disarm();
    int kind=del?1:arc?2:0;
    if(!kind || sel<0 || sel>=count)return;
    if(armed_pos!=sel || armed_kind!=kind) {
        armed_kind=kind;InterlockedExchange(&armed_pos,sel);armed_tick=GetTickCount();InterlockedIncrement(&list_version);
        play_sound(0x10001);
        return;
    }
    disarm();
    int idx=(int)rd32(menu+0x1888+sel*0x2c),files=(int)rd32(RVA(0xF915D8));
    char name[0x41]={0};
    if(idx>=0 && idx<files && files<=100)memcpy(name,RVA(0xF915DC)+idx*0x40,0x40);
    if(strncmp(name,"REPLY",5))return;
    Row *r=&rows[sel];
    int ci=is_copy(name);
    WCHAR path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\%hs",save_dir,name);
    if(kind==1) {
        JOURNAL("suppression",sel+1,idx,ci>=0);
        if(ci>=0) {
            if(!discard_archive(copies[ci].arch))return;
            DeleteFileW(path);untrack(ci);
        } else if(!move_to_bin(name))return;
        JOURNAL(recycle?"replay_deplace":"replay_supprime",0,0,0);
        play_sound(0x10000);
        remove_row(count);ask_relist(menu);
        return;
    }
    if(ci>=0) {
        if(!DeleteFileW(copies[ci].arch)){JOURNAL("desarchivage_refuse",GetLastError(),0,0);return;}
        untrack(ci);
        EnterCriticalSection(&lock);r->archived=FALSE;LeaveCriticalSection(&lock);
        InterlockedIncrement(&list_version);
        play_sound(0x1001a);JOURNAL("desarchive",sel+1,0,0);
        return;
    }
    WCHAR arch[MAX_PATH];
    if(!r->ok || !archive_file(r,name,arch)){play_sound(0x1001d);return;}
    play_sound(0x1001a);JOURNAL("archive",sel+1,0,0);
    BYTE mine=my_side(r)?r->ch2:r->ch1;
    if(archive_view!=TOUS && mine==archive_view && copy_count<100 && CopyFileW(arch,path,TRUE)) {

        lstrcpyA(copies[copy_count].file,name);lstrcpynW(copies[copy_count].arch,arch,MAX_PATH);copy_count++;
        EnterCriticalSection(&lock);r->archived=TRUE;LeaveCriticalSection(&lock);
        InterlockedIncrement(&list_version);
        return;
    }
    remove_row(count);ask_relist(menu);
}
static BOOL clean_copies;
void __cdecl rm_update_before(BYTE *menu) {
    BOOL fresh=menu!=seen_menu || GetTickCount()-menu_tick>500;
    if(fresh){seen_menu=menu;wait_frames=1;sel=-1;forced_at=NULL;home_tile=NULL;launched=FALSE;prev_focus=-1;box_wait=0;loading_pos=-1;armed_pos=-1;InterlockedExchange(&row_count,0);InterlockedExchange(&view_count,0);

        if(own_name[0] && !lstrcmpW(player_filter,own_name))player_filter[0]=0;
        copy_count=0;archive_view=TOUS;clean_copies=TRUE;}
    menu_tick=GetTickCount();
    if(wait_frames>0 && !menu[0x17a4] && ++wait_frames>3) {
        wait_frames=0;
        if(sel<0){DWORD *p=cursor_data(menu);sel=p?(LONG)*p:0;}
        build_list(menu);
        if(sel>=row_count)sel=row_count-1;
        if(sel<0)sel=0;
        if(view_pos(sel)<0 && view_count>0)sel=view[0];
        if(clean_copies && save_dir[0]){clean_copies=FALSE;if(stale_copies())ask_relist(menu);}
    }
    if(menu[0x17a4] && !wait_frames){wait_frames=1;release_tile();home_tile=NULL;}
    LONG count=row_count;
    BOOL active=!wait_frames && count>0;
    if(active)capture_thumbs();
    if(active && rd32(menu+0xb84)==0 && loading_pos<0) {
        int step=nav_step();
        LONG vcount=view_count;
        if(step && vcount>0) {
            LONG at=view_pos(sel),to=at<0?0:at;
            if(step==1000||step==-1000){to+=step>0?10:-10;}else to+=step;

            if((step==1 || step==-1) && !nav_repeat){if(to<0)to=vcount-1;else if(to>=vcount)to=0;}
            if(to<0)to=0;
            if(to>=vcount)to=vcount-1;
            if(view[to]!=sel){sel=view[to];play_sound(0x10001);}
        }
        filter_input(TRUE,menu);
        action_input(menu,count);
    } else {nav_step();filter_input(FALSE,menu);disarm();}
    if(active)force_tile(menu);

    if(active && (dirs_held || (prev_filter&(chara_button|player_button))))rm_mute_moves=1;
    InterlockedExchange(&cursor_pos,sel);

    InterlockedExchange(&menu_shown,rd32(menu+0xb84)==0 && count>0);
}
static BYTE *cursor_tile(BYTE *menu) {
    DWORD player=rd32(menu+0x186c);
    return ((BYTE*(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD))RVA(0x7D6950))(menu,player,0x8d);
}
static void put_cursor(BYTE *menu,BYTE *tile) {
    DWORD key=rd32(tile+4);
    DWORD players=rd32(((BYTE*(__cdecl*)(void))RVA(0xD9070))()+0x5c);
    for(DWORD p=0;p<players && p<8;p++)((void(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD))RVA(0x7D6790))(menu,p,key);
}

void __cdecl rm_update_after(BYTE *menu) {
    rm_mute_moves=0;
    if(!wait_frames && row_count>0 && !menu[0x17a4]) {

        DWORD player=rd32(menu+0x186c);
        LONG focus=focus_list(menu,player);
        if(focus!=prev_focus){static int n;if(n++<20)JOURNAL("liste_active",prev_focus,focus,0);}
        if(focus==0x8e && prev_focus==0x8d){box_wait=1;InterlockedExchange(&loading_pos,sel);InterlockedIncrement(&list_version);
            JOURNAL("fenetre_replay",sel+1,0,0);}
        if(focus!=0x8e)box_wait=0;
        prev_focus=focus;

        if(box_wait && !launched) {
            if(menu[0x17a5] || rd32(menu+0x17a0)!=0xffffffff)box_wait=1;
            else if(++box_wait>3) {
                BYTE *e=((BYTE*(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD))RVA(0x7D6950))(menu,player,0x8e);
                BYTE *d=e?(BYTE*)rd32(e+0xc):NULL;
                DWORD ev=d?rd32(d+0x18):0;
                JOURNAL("evenement_fenetre",ev,0,0);
                if(ev>=0x99 && ev<=0x9d){launched=TRUE;*(DWORD*)(menu+0x17a0)=ev;}
                box_wait=0;
            }
        }
    }
    if(wait_frames || row_count<=0 || menu[0x17a4]){home_tile=NULL;return;}
    BYTE *t=cursor_tile(menu);
    if(!home_tile && t && rd32(t+0xc))home_tile=t;
    if(home_tile && t!=home_tile) {
        release_tile();put_cursor(menu,home_tile);
        static int n;if(n<20 && cursor_tile(menu)!=home_tile){n++;JOURNAL("retour_curseur_refuse",0,0,0);}
    }
    force_tile(menu);
}
__attribute__((naked)) static void update_hook(void) {
    __asm__ volatile("pushfl\n pushal\n push %ecx\n call _rm_update_before\n add $4,%esp\n popal\n popfl\n"
                     "push %ecx\n call *_rm_update_original\n pop %ecx\n"
                     "pushal\n push %ecx\n call _rm_update_after\n add $4,%esp\n popal\n ret\n");
}

static int start_on_viewer=1;
void *rm_watch_original;
static BYTE *watch_seen;static DWORD watch_tick;static int watch_frames;
void __cdecl rm_watch_before(BYTE *menu) {
    DWORD now=GetTickCount();
    if(menu!=watch_seen || now-watch_tick>500){watch_seen=menu;watch_frames=0;}
    watch_tick=now;
    if(++watch_frames>10 || !start_on_viewer)return;
    BYTE *e=((BYTE*(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD))RVA(0x7D78C0))(menu,0x89,1);
    if(!e || (rd32(e+0x10)&1))return;
    DWORD key=rd32(e+4);
    DWORD players=rd32(((BYTE*(__cdecl*)(void))RVA(0xD9070))()+0x5c);
    void (__attribute__((thiscall)) *moved)(BYTE*,DWORD)=(*(void***)menu)[8];
    for(DWORD p=0;p<players && p<8;p++) {
        ((void(__attribute__((thiscall))*)(BYTE*,DWORD,DWORD))RVA(0x7D6790))(menu,p,key);
        moved(menu,p);
    }
    JOURNAL("spectator_fight_viewer",watch_frames,0,0);
}
__attribute__((naked)) static void watch_hook(void) {
    __asm__ volatile("pushfl\n pushal\n push %ecx\n call _rm_watch_before\n add $4,%esp\n popal\n popfl\n"
                     "jmp *_rm_watch_original\n");
}

static int panel_x=100,panel_y=150,panel_w=1720,panel_h=700;
static int top_row;
static IDirect3DTexture9 *panel_tex;static int tex_w,tex_h;
static LONG drawn_version=-1,drawn_cursor=-2,drawn_top=-1;
static HDC dib_dc;static HBITMAP dib;static DWORD *dib_bits;static int dib_w,dib_h;
static HFONT font_big,font_name,font_small,font_bar;

static int bar_height(int h){return h/11;}
static int row_height(int h){int H=h-bar_height(h);return (H-(H/140)*(visible_rows+1))/visible_rows;}
static void make_fonts(int h,int bh) {
    if(font_big)DeleteObject(font_big);
    if(font_name)DeleteObject(font_name);
    if(font_small)DeleteObject(font_small);
    if(font_bar)DeleteObject(font_bar);
    font_bar=CreateFontW(-bh*45/100,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
    font_big=CreateFontW(-h*42/100,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
    font_name=CreateFontW(-h*30/100,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
    font_small=CreateFontW(-h*24/100,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
}
static void text(HFONT f,COLORREF c,int x,int y,int w,int h,const WCHAR *s,UINT fmt) {
    RECT r={x,y,x+w,y+h};SelectObject(dib_dc,f);SetTextColor(dib_dc,c);
    DrawTextW(dib_dc,s,-1,&r,fmt|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
}
static void fill(int x,int y,int w,int h,COLORREF c) {
    RECT r={x,y,x+w,y+h};HBRUSH b=CreateSolidBrush(c);FillRect(dib_dc,&r,b);DeleteObject(b);
}

static void draw_pic(const DWORD *px,int PW,int PH,int x,int y,int w,int h) {
    if(w<2 || h<2)return;
    GdiFlush();
    for(int j=0;j<h;j++) {
        int dy=y+j;if(dy<0 || dy>=dib_h)continue;
        float sy=(j+0.5f)*PH/h-0.5f;if(sy<0)sy=0;
        int y0=(int)sy,y1=y0+1<PH?y0+1:y0;float fy=sy-y0;
        for(int i=0;i<w;i++) {
            int dx=x+i;if(dx<0 || dx>=dib_w)continue;
            float sx=(i+0.5f)*PW/w-0.5f;if(sx<0)sx=0;
            int x0=(int)sx,x1=x0+1<PW?x0+1:x0;float fx=sx-x0;
            DWORD q[4]={px[y0*PW+x0],px[y0*PW+x1],px[y1*PW+x0],px[y1*PW+x1]};
            float wq[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy},ch[4]={0,0,0,0};
            for(int k=0;k<4;k++)for(int b=0;b<4;b++)ch[b]+=wq[k]*((q[k]>>(b*8))&0xff);
            float a=ch[3]/255;DWORD *d=dib_bits+dy*dib_w+dx,o=*d,n=0;
            for(int b=0;b<3;b++){float v=ch[b]*a+((o>>(b*8))&0xff)*(1-a);n|=(DWORD)(v+0.5f)<<(b*8);}
            *d=n;
        }
    }
}

static const WCHAR *control_name(int mask,int vk,WCHAR *out,int n) {
    const WCHAR *b=mask==0x1000?L"L1":mask==0x2000?L"R1":mask==0x4000?L"L2":mask==0x8000?L"R2":L"";
    WCHAR k[8]=L"";
    if(vk>=VK_F1 && vk<=VK_F24)swprintf(k,8,L"F%d",vk-VK_F1+1);
    swprintf(out,n,L"%ls%ls%ls",b,b[0] && k[0]?L" / ":L"",k);
    return out;
}
static int text_width(HFONT f,const WCHAR *s) {
    SIZE z={0,0};SelectObject(dib_dc,f);GetTextExtentPoint32W(dib_dc,s,lstrlenW(s),&z);return z.cx;
}

static void paint_bar(int w,int bh) {
    int pad=bh/4,x=pad,fh=bh*80/100,fw=fh*FACE_W/FACE_H;
    COLORREF lab=RGB(170,165,165),on=RGB(255,200,60),off=RGB(240,240,240);
    WCHAR t[96],c[24];
    fill(0,0,w,bh,RGB(34,28,30));
    if(chara_button || chara_key) {
        swprintf(t,96,L"%ls  Character:",control_name(chara_button,chara_key,c,24));
        text(font_bar,lab,x,0,w/3,bh,t,DT_LEFT);x+=text_width(font_bar,t)+pad;
        if(chara_filter==TOUS){text(font_bar,off,x,0,w/6,bh,L"All",DT_LEFT);x+=text_width(font_bar,L"All");}
        else if(chara_filter<=0x30 && faces[chara_filter].ok){draw_pic(faces[chara_filter].px,FACE_W,FACE_H,x,(bh-fh)/2,fw,fh);x+=fw;}
        else {swprintf(t,96,L"#%02x",chara_filter);text(font_bar,on,x,0,w/6,bh,t,DT_LEFT);x+=text_width(font_bar,t);}
        x+=pad*4;
    }
    if(player_button || player_key) {
        swprintf(t,96,L"%ls  Player:",control_name(player_button,player_key,c,24));
        text(font_bar,lab,x,0,w/3,bh,t,DT_LEFT);x+=text_width(font_bar,t)+pad;
        text(font_bar,player_filter[0]?on:off,x,0,w*35/100,bh,player_filter[0]?player_filter:L"All",DT_LEFT);
    }
    swprintf(t,96,L"%ls%ld / %ld",archive_view!=TOUS?L"Archives shown   ":L"",view_count,row_count);
    text(font_bar,archive_view!=TOUS?RGB(120,170,255):lab,w-w*3/10-pad,0,w*3/10,bh,t,DT_RIGHT);
}
static void paint_panel(int w,int h,int pos) {
    LONG count=view_count,cur=view_pos(pos);
    int bh=bar_height(h),H=h-bh;
    fill(0,0,w,h,RGB(16,10,12));
    SetBkMode(dib_dc,TRANSPARENT);
    paint_bar(w,bh);
    int gap=H/140,rh=row_height(h);
    if(cur>=0) {
        if(cur<top_row)top_row=cur;
        if(cur>=top_row+visible_rows)top_row=cur-visible_rows+1;
    }
    if(top_row>count-visible_rows)top_row=count-visible_rows;
    if(top_row<0)top_row=0;
    for(int i=0;i<visible_rows;i++) {
        int v=top_row+i;if(v>=count)break;
        int k=view[v];
        Row *r=&rows[k];
        int y=bh+gap+i*(rh+gap),x=gap;int rw=w-2*gap;
        BOOL sel=k==pos,armed=k==armed_pos;
        if(armed) {
            BOOL arc=armed_kind==2;
            fill(x,y,rw,rh,arc?RGB(28,70,170):RGB(170,28,28));fill(x,y,rw,2,arc?RGB(120,170,255):RGB(255,120,120));
            WCHAR num[8],msg[160],folder[32];swprintf(num,8,L"%03d",r->number);
            text(font_big,RGB(255,255,255),x+rh/5,y,rh*12/10,rh,num,DT_LEFT);
            int side=my_side(r);
            chara_folder(side==1?r->ch2:r->ch1,folder,32);
            if(arc && r->archived)lstrcpynW(msg,L"Unarchive this replay?   Press Square / X again   (back to the normal list)",160);
            else if(arc && side<0)lstrcpynW(msg,L"Cannot archive: your name is not in this replay",160);
            else if(arc)swprintf(msg,160,L"Archive this replay?   Press Square / X again   (REPLY_ARCHIVE\\%ls)",folder);
            else if(r->archived)lstrcpynW(msg,recycle?L"Delete this archived replay?   Press Triangle / Y again   (moved to REPLY_CORBEILLE)"
                                                    :L"Delete this archived replay for good?   Press Triangle / Y again",160);
            else lstrcpynW(msg,recycle?L"Delete this replay?   Press Triangle / Y again   (moved to REPLY_CORBEILLE)"
                                      :L"Delete this replay for good?   Press Triangle / Y again",160);
            text(font_name,RGB(255,255,255),x+rh*14/10,y,rw-rh*16/10,rh,msg,DT_CENTER);
            continue;
        }
        fill(x,y,rw,rh,sel?RGB(200,150,30):RGB(48,44,46));
        fill(x,y,rw,2,sel?RGB(255,220,120):RGB(80,74,76));
        COLORREF main=sel?RGB(20,12,0):RGB(240,240,240),dim=sel?RGB(60,40,0):RGB(170,165,165);
        WCHAR num[8];swprintf(num,8,L"%03d",r->number);
        text(font_big,main,x+rh/5,y,rh*12/10,rh,num,DT_LEFT);
        int cx=x+rh*14/10,cw=rw-(cx-x)-rh/5;
        if(!r->ok){text(font_name,dim,cx,y,cw,rh,L"(replay illisible)",DT_LEFT);continue;}

        int infow=cw*38/100,mid=cw-infow;
        int fh=rh*80/100,fw=fh*FACE_W/FACE_H,vsw=rh*7/10,namew=(mid-2*fw-vsw-rh/2)/2;
        int fy=y+(rh-fh)/2,px1=cx+namew+rh/8,vx=px1+fw,px2=vx+vsw;
        BOOL f1=r->ch1<=0x30 && faces[r->ch1].ok,f2=r->ch2<=0x30 && faces[r->ch2].ok;
        text(font_name,main,cx,y,namew,rh,r->p1,DT_RIGHT);
        if(f1){fill(px1-2,fy-2,fw+4,fh+4,sel?RGB(255,230,160):RGB(110,104,106));draw_pic(faces[r->ch1].px,FACE_W,FACE_H,px1,fy,fw,fh);}
        else text(font_small,dim,px1,y,fw,rh,r->c1,DT_CENTER);
        text(font_big,sel?RGB(120,0,0):RGB(230,60,60),vx,y,vsw,rh,L"VS",DT_CENTER);
        if(f2){fill(px2-2,fy-2,fw+4,fh+4,sel?RGB(255,230,160):RGB(110,104,106));draw_pic(faces[r->ch2].px,FACE_W,FACE_H,px2,fy,fw,fh);}
        else text(font_small,dim,px2,y,fw,rh,r->c2,DT_CENTER);
        text(font_name,main,px2+fw+rh/8,y,namew,rh,r->p2,DT_LEFT);
        int ix=cx+mid;
        fill(ix,y+rh/8,2,rh*3/4,sel?RGB(150,105,20):RGB(80,74,76));
        if(k==loading_pos){text(font_name,main,ix,y,infow,rh,L"LOADING...",DT_CENTER);continue;}
        int tx=ix+rh/6;
        if(r->stage_idx<0x4b && thumbs[r->stage_idx].ok) {
            int th=rh*80/100,tw=th*THUMB_W/THUMB_H;
            draw_pic(thumbs[r->stage_idx].px,THUMB_W,THUMB_H,tx,y+(rh-th)/2,tw,th);
            tx+=tw+rh/6;
        }
        text(font_name,main,tx,y+rh/12,ix+infow-tx,rh/2-rh/12,r->stage,DT_LEFT);
        WCHAR info[112];swprintf(info,112,L"%ls%ls  %ls  %ls",r->archived?L"ARCHIVE  ":L"",r->mode,r->when,r->dur);
        text(font_small,dim,tx,y+rh/2,ix+infow-tx,rh/2-rh/12,info,DT_LEFT);
    }
    if(count>visible_rows) {
        int sh=H*visible_rows/count,sy=bh+(H-sh)*top_row/(count-visible_rows);
        fill(w-gap/2-3,sy,3,sh,RGB(200,150,30));
    }
}
static BOOL update_texture(IDirect3DDevice9 *dev,int w,int h,int cur) {
    if(w!=dib_w || h!=dib_h) {
        if(dib){DeleteObject(dib);dib=NULL;}
        if(!dib_dc)dib_dc=CreateCompatibleDC(NULL);
        BITMAPINFO bi={{sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB}};
        dib=CreateDIBSection(dib_dc,&bi,DIB_RGB_COLORS,(void**)&dib_bits,NULL,0);
        if(!dib)return FALSE;
        SelectObject(dib_dc,dib);dib_w=w;dib_h=h;
        make_fonts(row_height(h),bar_height(h));
    }
    if(!panel_tex || tex_w!=w || tex_h!=h) {
        if(panel_tex){IDirect3DTexture9_Release(panel_tex);panel_tex=NULL;}
        if(FAILED(IDirect3DDevice9_CreateTexture(dev,w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&panel_tex,NULL))){JOURNAL("texture_impossible",0,0,0);return FALSE;}
        tex_w=w;tex_h=h;
    }
    EnterCriticalSection(&lock);paint_panel(w,h,cur);LeaveCriticalSection(&lock);
    GdiFlush();
    D3DLOCKED_RECT lr;
    if(FAILED(IDirect3DTexture9_LockRect(panel_tex,0,&lr,NULL,0)))return FALSE;
    for(int y=0;y<h;y++) {
        DWORD *src=dib_bits+y*w,*dst=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(int x=0;x<w;x++)dst[x]=(src[x]&0xffffff)|0xff000000;
    }
    IDirect3DTexture9_UnlockRect(panel_tex,0);
    return TRUE;
}
static void draw_overlay(IDirect3DDevice9 *dev) {
    LONG count=row_count;if(count<=0)return;
    IDirect3DSurface9 *bb=NULL;
    if(FAILED(IDirect3DDevice9_GetBackBuffer(dev,0,0,D3DBACKBUFFER_TYPE_MONO,&bb)) || !bb)return;
    D3DSURFACE_DESC sd;IDirect3DSurface9_GetDesc(bb,&sd);
    float gh=(float)sd.Height,gw=gh*16/9,gx=0,gy=0;
    if(gw>sd.Width){gw=(float)sd.Width;gh=gw*9/16;gy=(sd.Height-gh)/2;}else gx=(sd.Width-gw)/2;
    float s=gh/1080;
    int w=(int)(panel_w*s),h=(int)(panel_h*s);
    LONG cur=cursor_pos;
    if(w<16 || h<16){IDirect3DSurface9_Release(bb);return;}
    if(drawn_version!=list_version || drawn_cursor!=cur || !panel_tex || tex_w!=w || tex_h!=h) {
        if(!update_texture(dev,w,h,cur)){IDirect3DSurface9_Release(bb);return;}
        drawn_version=list_version;drawn_cursor=cur;drawn_top=top_row;
    }
    IDirect3DStateBlock9 *sb=NULL;
    if(FAILED(IDirect3DDevice9_CreateStateBlock(dev,D3DSBT_ALL,&sb))){IDirect3DSurface9_Release(bb);return;}
    IDirect3DSurface9 *rt=NULL;IDirect3DDevice9_GetRenderTarget(dev,0,&rt);
    IDirect3DDevice9_SetRenderTarget(dev,0,bb);
    D3DVIEWPORT9 vp={0,0,sd.Width,sd.Height,0,1};IDirect3DDevice9_SetViewport(dev,&vp);
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_SetVertexShader(dev,NULL);IDirect3DDevice9_SetPixelShader(dev,NULL);
    IDirect3DDevice9_SetFVF(dev,D3DFVF_XYZRHW|D3DFVF_TEX1);
    IDirect3DDevice9_SetTexture(dev,0,(IDirect3DBaseTexture9*)panel_tex);
    for(int i=1;i<8;i++)IDirect3DDevice9_SetTexture(dev,i,NULL);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_TEXCOORDINDEX,0);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev,1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev,1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_SRGBTEXTURE,0);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_SRGBWRITEENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ALPHABLENDENABLE,TRUE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_BLENDOP,D3DBLENDOP_ADD);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ALPHATESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ZENABLE,D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ZWRITEENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_STENCILENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_SCISSORTESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_CULLMODE,D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_FOGENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_LIGHTING,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_COLORWRITEENABLE,0xf);
    float x0=(float)(int)(gx+panel_x*s)-0.5f,y0=(float)(int)(gy+panel_y*s)-0.5f,x1=x0+w,y1=y0+h;
    struct {float x,y,z,w,u,v;} q[4]={{x0,y0,0,1,0,0},{x1,y0,0,1,1,0},{x0,y1,0,1,0,1},{x1,y1,0,1,1,1}};
    IDirect3DDevice9_DrawPrimitiveUP(dev,D3DPT_TRIANGLESTRIP,2,q,sizeof(q[0]));
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_SetRenderTarget(dev,0,rt);if(rt)IDirect3DSurface9_Release(rt);
    IDirect3DStateBlock9_Apply(sb);IDirect3DStateBlock9_Release(sb);
    IDirect3DSurface9_Release(bb);
    static BOOL logged;
    if(!logged){logged=TRUE;JOURNAL("panneau_dessine",0,0,0);}
}

typedef HRESULT(WINAPI *Present_t)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
typedef HRESULT(WINAPI *CreateDevice_t)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
typedef struct {void **vt;Present_t present;} DevHook;
static DevHook dev_hooks[4];static volatile LONG dev_hook_count;
static CreateDevice_t createdevice_original;
static HRESULT WINAPI present_hook(IDirect3DDevice9 *d,const RECT *a,const RECT *b,HWND w,const RGNDATA *r) {
    void **vt=*(void***)d;Present_t orig=NULL;
    for(LONG i=0;i<dev_hook_count;i++)if(dev_hooks[i].vt==vt)orig=dev_hooks[i].present;
    if(menu_shown && GetTickCount()-menu_tick<200)draw_overlay(d);
    return orig?orig(d,a,b,w,r):D3D_OK;
}
static void hook_device(IDirect3DDevice9 *dev) {
    void **vt=*(void***)dev;DWORD old;
    for(LONG i=0;i<dev_hook_count;i++)if(dev_hooks[i].vt==vt)return;
    if(dev_hook_count>=4 || vt[17]==(void*)present_hook)return;
    if(VirtualProtect(&vt[17],4,PAGE_EXECUTE_READWRITE,&old)) {
        dev_hooks[dev_hook_count].vt=vt;dev_hooks[dev_hook_count].present=(Present_t)vt[17];
        InterlockedIncrement(&dev_hook_count);
        vt[17]=(void*)present_hook;VirtualProtect(&vt[17],4,old,&old);
        JOURNAL("affichage_accroche",dev_hook_count,0,0);
    }
}
static HRESULT WINAPI createdevice_hook(IDirect3D9 *d3d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS *pp,IDirect3DDevice9 **out) {
    HRESULT hr=createdevice_original(d3d,a,t,w,f,pp,out);
    if(SUCCEEDED(hr) && out && *out && t==D3DDEVTYPE_HAL)hook_device(*out);
    return hr;
}
static void install_d3d_hooks(void) {
    IDirect3D9 *d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){JOURNAL("d3d9_indisponible",0,0,0);return;}
    void **vt=*(void***)d3d;DWORD old;
    if(VirtualProtect(&vt[16],4,PAGE_EXECUTE_READWRITE,&old)) {
        createdevice_original=(CreateDevice_t)vt[16];vt[16]=(void*)createdevice_hook;
        VirtualProtect(&vt[16],4,old,&old);
    }
    IDirect3D9_Release(d3d);
}

static DWORD WINAPI worker(void *u) {
    (void)u;
    install_d3d_hooks();
    BYTE probe[sizeof dispatch_signature];BOOL ready=FALSE;
    for(int n=0;n<600 && !ready;n++) {
        ready=readmem(RVA(0x690100),probe,sizeof probe) && !memcmp(probe,dispatch_signature,sizeof probe);
        if(!ready)Sleep(100);
    }
    void **slot=(void**)RVA(0x99C7A4+33*4);
    if(!ready){JOURNAL("refus_version_game_exe",0,0,0);return 0;}
    DWORD old;
    if(!VirtualProtect(slot,4,PAGE_READWRITE,&old)){JOURNAL("accroche_impossible",0,0,0);return 0;}
    rm_update_original=*slot;*slot=(void*)update_hook;
    VirtualProtect(slot,4,old,&old);
    rm_sound_original=detour(0x557C40,prologue_sound,6,(void*)sound_hook);
    void **wslot=(void**)RVA(0x99CE9C+33*4);
    if(*wslot==(void*)RVA(0x24C5C0) && VirtualProtect(wslot,4,PAGE_READWRITE,&old)) {
        rm_watch_original=*wslot;*wslot=(void*)watch_hook;VirtualProtect(wslot,4,old,&old);
    }
    rm_text_original=detour_shared(0x745BA0,prologue_text,5,(void*)hook_text);
    JOURNAL("pret 1.2",rm_sound_original!=NULL,rm_text_original!=NULL,rm_watch_original!=NULL);
    return 0;
}

void ReplayMenu_Demarrer(void) {
    base=(BYTE*)GetModuleHandleW(NULL);
    InitializeCriticalSection(&lock);
    panel_x=Reglage("ReplayMenu","X",panel_x);panel_y=Reglage("ReplayMenu","Y",panel_y);
    panel_w=Reglage("ReplayMenu","Width",panel_w);panel_h=Reglage("ReplayMenu","Height",panel_h);
    visible_rows=Reglage("ReplayMenu","Rows",visible_rows);if(visible_rows<1)visible_rows=1;if(visible_rows>20)visible_rows=20;
    recycle=Reglage("ReplayMenu","Recycle",1);
    start_on_viewer=Reglage("ReplayMenu","StartOnFightViewer",1);
    chara_button=Reglage("ReplayMenu","FilterCharacterButton",chara_button);
    player_button=Reglage("ReplayMenu","FilterPlayerButton",player_button);
    chara_key=Reglage("ReplayMenu","FilterCharacterKey",chara_key);
    player_key=Reglage("ReplayMenu","FilterPlayerKey",player_key);
    archive_button=Reglage("ReplayMenu","ArchiveButton",archive_button);
    archive_key=Reglage("ReplayMenu","ArchiveKey",archive_key);
    HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);
    if(t)CloseHandle(t);
}

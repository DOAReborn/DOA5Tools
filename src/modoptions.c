#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "accroche.h"
#include "commun.h"
static BYTE *base;
#define VA(v) (base+((v)-0x400000))

static void log_line(const char *fmt,...){
    char l[256];va_list ap;va_start(ap,fmt);vsnprintf(l,sizeof l,fmt,ap);va_end(ap);
    Journal("ModOptions",l,0,0,0);
}
static const char *fname(const char *path){const char *b=strrchr(path,'\\');return b?b+1:path;}
static BOOL readmem(const void *p,void *b,SIZE_T n){SIZE_T d=0;return ReadProcessMemory(GetCurrentProcess(),p,b,n,&d)&&d==n;}
static DWORD rd(DWORD a){DWORD v=0;readmem((void*)a,&v,4);return v;}
static BOOL online(void){return rd((DWORD)VA(0x138f208))||rd((DWORD)VA(0x138189c));}

#define VID 0x2607f0
#define TID 0x0c0ff0
#define CLE 0x60
#define LISTE 0xf0
#define NW 9
#define MODELE 0x260008
static DWORD target=0x42;
static DWORD open_screen;
static DWORD position;
static volatile LONG want72;
static volatile LONG rfmods;static volatile int vpage,vprev;
static volatile LONG rf_return;static DWORD rf_return_t;
static void rf_frame(DWORD obj);
static volatile LONG rfmode;
DWORD mo_frame=0x40;
static char words[NW][64]={"EH-NAH WATAH"};static char words_off[NW][64];
static WCHAR helps[NW][256]={L"Mod options."};static WCHAR helps_off[NW][256];static int nwords=1;
#define NOPT 80
static struct{char file[MAX_PATH],section[64],keys[2][32];int nkeys,row,state,page,slot,def;WCHAR wname[64];
    int nval,val;char vals[8][12],module[64],func[64];WCHAR disp[96],help[160];BOOL nofn;}opts[NOPT];static int nopt;
static int nvirt=1,title_k=0;static char titles[3][64]={"DOA5TOOLS","ROLLBACK","ADDITIONAL CONTENT"};
static int opt_of_row(int k){for(int i=0;i<nopt;i++)if(opts[i].row==k)return i;return -1;}

static BYTE *copy;static DWORD copy_off;static volatile BOOL tenrows;static DWORD position=6;
__attribute__((used)) DWORD __cdecl mo_build_note(DWORD menu,DWORD poff){
    DWORD lay=rd(menu+0x60),off=poff?rd(poff):0;
    if(!lay||!off||online())return poff;
    DWORD h=lay+off,nl=rd(h),ol=rd(h+4),nr=rd(h+8),orr=rd(h+12),ne=rd(h+16),oe=rd(h+20);
    if(nl>64||nr>256||ne>256)return poff;
    if(!copy)copy=VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!copy)return poff;
    DWORD n=nwords-1;DWORD link=n?LISTE:0xffffffff;
    BYTE *H=copy,*L=copy+0x40,*R=L+(nl+1)*0x18,*E=R+(nr+n+1)*0x18;
    memcpy(L,(void*)(lay+ol),nl*0x18);memcpy(R,(void*)(lay+orr),nr*0x18);memcpy(E,(void*)(lay+oe),ne*0x10);
    BOOL add=target==0;DWORD rows=0;
    if(add){
        for(DWORD i=0;i<nl;i++){DWORD *d=(DWORD*)(L+i*0x18);if(d[0]==0xf)rows=d[4];}
        if(rows!=9){log_line("Menu principal : %lu rangs (9 attendus), rien change.",rows);return poff;}
        DWORD pos=position>9?9:position;
        static BYTE R2[0x18*300],E2[0x10*300];DWORD k=0,j=0;BOOL put=FALSE;
        for(DWORD i=0;i<nr;i++){DWORD *r=(DWORD*)(R+i*0x18);
            if(r[0]==0xf&&!put&&r[3]>=pos){DWORD *o=(DWORD*)(R2+k++*0x18);o[0]=0xf;o[1]=CLE;o[2]=0;o[3]=pos;o[4]=0;o[5]=0;put=TRUE;}
            DWORD *o=(DWORD*)(R2+k++*0x18);memcpy(o,r,0x18);if(r[0]==0xf&&r[3]>=pos)o[3]++;}
        if(!put){DWORD *o=(DWORD*)(R2+k++*0x18);o[0]=0xf;o[1]=CLE;o[2]=0;o[3]=pos;o[4]=0;o[5]=0;}
        put=FALSE;
        for(DWORD i=0;i<ne;i++){DWORD *e=(DWORD*)(E+i*0x10);
            if(e[0]==0xf&&!put&&e[1]>=pos){DWORD *o=(DWORD*)(E2+j++*0x10);o[0]=0xf;o[1]=pos;o[2]=link;o[3]=0xffffffff;put=TRUE;}
            DWORD *o=(DWORD*)(E2+j++*0x10);memcpy(o,e,0x10);if(e[0]==0xf&&e[1]>=pos)o[1]++;}
        if(!put){DWORD *o=(DWORD*)(E2+j++*0x10);o[0]=0xf;o[1]=pos;o[2]=link;o[3]=0xffffffff;}
        nr=k;ne=j;E=R+(nr+n+1)*0x18;memcpy(R,R2,nr*0x18);memcpy(E,E2,ne*0x10);
        for(DWORD i=0;i<nl;i++){DWORD *d=(DWORD*)(L+i*0x18);if(d[0]==0xf)d[4]=10;}
    }else{
        DWORD idx=0xffffffff;
        for(DWORD i=0;i<nr;i++){DWORD *r=(DWORD*)(R+i*0x18);if(r[0]==0xf&&r[1]==target){r[1]=CLE;idx=r[3];}}
        if(idx==0xffffffff){log_line("Menu principal : ligne 0x%lx introuvable, rien change.",target);return poff;}
        for(DWORD i=0;i<ne;i++){DWORD *e=(DWORD*)(E+i*0x10);if(e[0]==0xf&&e[1]==idx)e[2]=link;}
    }
    DWORD *src=NULL;for(DWORD i=0;i<nl;i++){DWORD *d=(DWORD*)(L+i*0x18);if(d[0]==0x11)src=d;}
    if(!src){log_line("Liste 0x11 introuvable, rien change.");return poff;}
    DWORD *d=(DWORD*)(L+nl*0x18);memcpy(d,src,0x18);d[0]=LISTE;d[4]=n;
    if(!n)nl--;
    for(DWORD i=0;i<n;i++){DWORD *r=(DWORD*)(R+(nr+i)*0x18);r[0]=LISTE;r[1]=CLE+1+i;r[2]=0;r[3]=i;r[4]=0;r[5]=0;
        DWORD *e=(DWORD*)(E+(ne+i)*0x10);e[0]=LISTE;e[1]=i;e[2]=0xffffffff;e[3]=0xffffffff;}
    DWORD *hd=(DWORD*)H;hd[0]=nl+1;hd[1]=(DWORD)L-lay;hd[2]=nr+n;hd[3]=(DWORD)R-lay;hd[4]=ne+n;hd[5]=(DWORD)E-lay;
    copy_off=(DWORD)H-lay;
    if(add)log_line("Menu principal : 10e ligne (cle 0x%x) au rang %lu, qui ouvre notre sous-menu de %lu ligne(s) ; pas des lignes 37 -> 33.",CLE,position,n);
    else log_line("Menu principal : la ligne 0x%lx (cle 0x%x) ouvre notre sous-menu de %lu ligne(s).",target,CLE,n);
    tenrows=add;
    return (DWORD)&copy_off;
}
void *volatile mo_build_suite;
__attribute__((naked)) static void build_hook(void){
    __asm__ volatile("pushal\n pushfl\n push 40(%esp)\n push %ecx\n call _mo_build_note\n add $8,%esp\n mov %eax,40(%esp)\n popfl\n popal\n jmp *_mo_build_suite\n");
}

void *volatile mo_label_suite,*volatile mo_help_suite;
static BOOL ours(DWORD k){return k>=CLE&&k<CLE+(DWORD)nwords;}
__attribute__((used)) DWORD __cdecl mo_label_c(DWORD k){
    if(ours(k)&&!online())return VID+(k-CLE);
    return ((DWORD(__cdecl*)(DWORD))mo_label_suite)(k);
}
__attribute__((used)) DWORD __cdecl mo_help_c(DWORD k){
    if(ours(k)&&!online())return TID+(k-CLE);
    return ((DWORD(__cdecl*)(DWORD))mo_help_suite)(k);
}
#define PRESERVING_ENTRY(entry,target) \
    __attribute__((naked)) static void entry(void){ \
        __asm__ volatile("push %ecx\n push %edx\n push 12(%esp)\n call _" #target "\n add $4,%esp\n pop %edx\n pop %ecx\n ret\n");}
PRESERVING_ENTRY(label_hook,mo_label_c)
PRESERVING_ENTRY(help_hook,mo_help_c)

static const DWORD help_sites[]={0xaea5d8,0xaea5f2,0xaea821,0xaea83b,0xaea881,0xaea89b,0xaea9c5,0xaea9df,0xaeb3ea,0xaeb404,
    0xaeb494,0xaeb4b2,0xaeb537,0xaeb555,0xaeb821,0xaeb83b,0xaebd83,0xaebd9d,0xaec1cc,0xaec1e6,0xaed02b,0xaed045,0xaed0d5,
    0xaed0f3,0xaed17c,0xaed19a,0xaed8b3,0xaed8cd};
void *volatile mo_text_suite;
__attribute__((used)) const WCHAR *__cdecl mo_text_c(DWORD lang,DWORD id,DWORD flags){
    if(id>=TID&&id<TID+(DWORD)nwords){int k=id-TID,o=opt_of_row(k);return o>=0&&!opts[o].state?helps_off[k]:helps[k];}
    return ((const WCHAR*(__cdecl*)(DWORD,DWORD,DWORD))VA(0xb45ba0))(lang,id,flags);
}
__attribute__((naked)) static void text_hook(void){
    __asm__ volatile("push %ecx\n push %edx\n push 20(%esp)\n push 20(%esp)\n push 20(%esp)\n call _mo_text_c\n add $12,%esp\n pop %edx\n pop %ecx\n ret\n");
}

static struct{BYTE record[0x30];DWORD texinfo[3];DWORD desc[4];BYTE object[0x40];DWORD pdesc[4];BYTE pobject[0x40];DWORD tex,pal;}vs[2*NW];
static void show_state(int k,int on){
    *(volatile DWORD*)(vs[k].object+8)=on?vs[k].tex:vs[k+NW].tex;*(volatile DWORD*)(vs[k].pobject+8)=on?vs[k].pal:vs[k+NW].pal;}
static volatile BOOL ready;static BOOL failed;
void *volatile mo_record_suite,*volatile mo_texinfo_suite;
__attribute__((used)) DWORD __cdecl mo_record_c(DWORD id){
    if(id>=VID&&id<VID+(DWORD)nvirt&&ready)return (DWORD)vs[id-VID].record;
    return ((DWORD(__cdecl*)(DWORD))mo_record_suite)(id);
}
__attribute__((used)) DWORD __cdecl mo_texinfo_c(DWORD id){
    if(id>=VID&&id<VID+(DWORD)nvirt&&ready)return (DWORD)vs[id-VID].texinfo;
    return ((DWORD(__cdecl*)(DWORD))mo_texinfo_suite)(id);
}
PRESERVING_ENTRY(record_hook,mo_record_c)
PRESERVING_ENTRY(texinfo_hook,mo_texinfo_c)

static BYTE *d3lo,*d3hi;
static IDirect3DTexture9 *as_texture(DWORD p){
    if(p<0x10000||(p&3))return NULL;DWORD vt=rd(p);if(!vt||(BYTE*)vt<d3lo||(BYTE*)vt>=d3hi)return NULL;
    static const GUID iid={0x85c31227,0x3de5,0x4f00,{0x9b,0x3a,0xf1,0x1a,0xc3,0x8c,0x18,0xb5}};
    IUnknown *u=(IUnknown*)p;void *q=NULL;if(FAILED(u->lpVtbl->QueryInterface(u,&iid,&q))||!q)return NULL;
    ((IUnknown*)q)->lpVtbl->Release((IUnknown*)q);return (IDirect3DTexture9*)p;
}

static DWORD texinfo_entry(DWORD id,DWORD *rec_out){
    DWORD rec=((DWORD(__cdecl*)(DWORD))VA(0xbcf680))(id);if(!rec)return 0;
    DWORD texi=rd(rec+0x14),g=id>>16;if(g>=rd((DWORD)VA(0x24825d4)))return 0;
    DWORD entry=rd((DWORD)VA(0x24825d8))+g*0x28,blk=rd(entry+0xc),tb=blk?rd(blk+0x20):0;if(tb)tb+=blk;
    DWORD ti=tb?rd(tb+4):0;if(ti)ti+=blk;char tag[8]={0};
    if(!ti||!readmem((void*)ti,tag,8)||memcmp(tag,"texinfo",7)||texi>=rd(ti+0x10))return 0;
    if(rec_out)*rec_out=rec;
    return ti+0x20+texi*12;
}

static const struct{char c;DWORD id;int x0,x1;}letters[]={
    {'E',0x260008,63,80},{'X',0x260008,81,102},{'I',0x260008,103,110},{'T',0x260008,111,129},
    {'F',0x260000,30,46},{'H',0x260000,74,93},
    {'S',0x260018,5,24},{'O',0x260018,25,44},{'U',0x260018,45,64},{'N',0x260018,65,85},{'D',0x260018,86,106},
    {'W',0x260033,25,48},{'A',0x260033,66,84},{'R',0x260033,220,239},
    {'C',0x26001b,5,23},
    {'P',0x260003,30,47},
    {'K',0x26000b,173,192},
    {'B',0x260035,6,25},
    {'L',0x260002,60,77},
};
#define LH 26
#define K 0.27f
#define SW 420
static int dec(int y){return (int)lroundf(K*(y-LH/2.0f))+(int)(K*LH/2)+1;}

#define NSRC 12
static struct{DWORD id;DWORD px[LH*SW];int ok;}srcs[NSRC];static int nsrc;
static DWORD *label_pixels(DWORD id){
    for(int i=0;i<nsrc;i++)if(srcs[i].id==id)return srcs[i].ok?srcs[i].px:NULL;
    if(nsrc>=NSRC)return NULL;
    int k=nsrc++;srcs[k].id=id;srcs[k].ok=0;memset(srcs[k].px,0,sizeof srcs[k].px);
    DWORD e=texinfo_entry(id,NULL);if(!e)return NULL;
    IDirect3DTexture9 *img=as_texture(rd(rd(rd(e)))),*pal=as_texture(rd(rd(rd(e+4))));if(!img||!pal)return NULL;
    float fv=0;readmem((void*)(e+8),&fv,4);
    D3DSURFACE_DESC di,dp;if(FAILED(IDirect3DTexture9_GetLevelDesc(img,0,&di))||FAILED(IDirect3DTexture9_GetLevelDesc(pal,0,&dp)))return NULL;
    if(di.Format!=D3DFMT_L8||dp.Format!=D3DFMT_A8R8G8B8||di.Height!=LH||di.Width>SW-12||dp.Width!=256)return NULL;
    int row=(int)(fv*8);if(row<0)row=0;if(row>=(int)dp.Height)row=dp.Height-1;
    DWORD lut[256];D3DLOCKED_RECT lr;RECT rr={0,row,256,row+1};
    if(FAILED(IDirect3DTexture9_LockRect(pal,0,&lr,&rr,D3DLOCK_READONLY)))return NULL;
    memcpy(lut,lr.pBits,1024);IDirect3DTexture9_UnlockRect(pal,0);
    if(FAILED(IDirect3DTexture9_LockRect(img,0,&lr,NULL,D3DLOCK_READONLY)))return NULL;
    for(int y=0;y<LH;y++)for(UINT x=0;x<di.Width;x++)srcs[k].px[y*SW+x+dec(y)]=lut[((BYTE*)lr.pBits)[y*lr.Pitch+x]];
    IDirect3DTexture9_UnlockRect(img,0);
    srcs[k].ok=1;return srcs[k].px;
}
static IDirect3DTexture9 *new_texture(IDirect3DDevice9 *dev,UINT w,UINT h,D3DFORMAT fmt,const BYTE *rows,UINT rb){
    IDirect3DTexture9 *t=NULL;D3DLOCKED_RECT lr;
    if(FAILED(IDirect3DDevice9_CreateTexture(dev,w,h,1,0,fmt,D3DPOOL_MANAGED,&t,NULL)))return NULL;
    if(FAILED(IDirect3DTexture9_LockRect(t,0,&lr,NULL,0))){IDirect3DTexture9_Release(t);return NULL;}
    for(UINT y=0;y<h;y++)memcpy((BYTE*)lr.pBits+y*lr.Pitch,rows+y*rb,rb);
    IDirect3DTexture9_UnlockRect(t,0);return t;
}
static BOOL compose(int k,const char *word,DWORD rec,DWORD e,IDirect3DDevice9 *dev){

    static DWORD I[LH*SW],O[LH*SW];memset(I,0,sizeof I);memset(O,0,sizeof O);int x=0;
    for(const char *c=word;*c;c++){
        if(*c==' '){x+=9;continue;}
        if(*c=='5'){
            int ge=-1,gs=-1;for(int i=0;i<(int)(sizeof letters/sizeof letters[0]);i++){if(letters[i].c=='E')ge=i;if(letters[i].c=='S')gs=i;}
            DWORD *PE=ge>=0?label_pixels(letters[ge].id):NULL,*PS=gs>=0?label_pixels(letters[gs].id):NULL;
            if(!PE||!PS){log_line("Lettres du 5 introuvables.");return FALSE;}
            int w=letters[ge].x1-letters[ge].x0;
            for(int y=0;y<LH;y++)for(int i=0;i<w&&x+i<SW;i++){DWORD pe=PE[y*SW+letters[ge].x0+i],ps=PS[y*SW+letters[gs].x0+i];
                DWORD p=y<13?pe:y>15?ps:((pe>>24)>(ps>>24)?pe:ps);
                if((p>>24)>(I[y*SW+x+i]>>24))I[y*SW+x+i]=p;}
            x+=w+1;continue;}
        char want=*c=='-'?'T':*c;int g=-1;
        for(int i=0;i<(int)(sizeof letters/sizeof letters[0]);i++)if(letters[i].c==want)g=i;
        DWORD *P=g>=0?label_pixels(letters[g].id):NULL;
        if(!P){log_line("Lettre '%c' introuvable (ou etiquette pas chargee).",*c);return FALSE;}
        int x0=letters[g].x0,x1=letters[g].x1;
        if(*c=='-'){int m=(x0+x1)/2-2;
            for(int y=4;y<10;y++)for(int i=m-5;i<m+5;i++)if(x+i-2-(m-7)<SW)I[(y+6)*SW+x+i-2-(m-7)]=P[y*SW+i];
            x+=11;continue;}
        for(int y=0;y<LH;y++)for(int i=x0;i<x1&&x+i-x0<SW;i++){DWORD p=P[y*SW+i];if((p>>24)>(I[y*SW+x+i-x0]>>24))I[y*SW+x+i-x0]=p;}
        x+=x1-x0+1;}
    int lw=x-1,mw=274;if(lw>mw)mw=lw+2;if(mw>SW-12){log_line("Mot \"%s\" trop long (%d colonnes).",word,lw);return FALSE;}
    int off=(mw-lw)/2;
    for(int y=0;y<LH;y++)for(int xi=0;xi<SW;xi++){int xd=xi-dec(y)+off+(int)(K*LH/2);
        if(xd>=0&&xd<mw&&(I[y*SW+xi]>>24)>(O[y*mw+xd]>>24))O[y*mw+xd]=I[y*SW+xi];}

    static DWORD pal[256];static BYTE L8[LH*SW];int np;DWORD mask=0xffffffff;
    for(;;){np=1;pal[0]=0;BOOL over=FALSE;
        for(int i=0;i<LH*mw&&!over;i++){DWORD p=O[i]&mask;if(!(p>>24)){L8[i]=0;continue;}
            int j=1;while(j<np&&pal[j]!=p)j++;
            if(j==np){if(np==256){over=TRUE;break;}pal[np++]=p;}
            L8[i]=(BYTE)j;}
        if(!over)break;mask=mask==0xffffffff?0xf8f8f8f8:mask==0xf8f8f8f8?0xf0f0f0f0:0xe0e0e0e0;}
    static DWORD pal8[8*256];for(int r=0;r<8;r++)memcpy(pal8+r*256,pal,1024);
    DWORD img_obj=rd(rd(e))-8,pal_obj=rd(rd(e+4))-8;
    IDirect3DTexture9 *ti=new_texture(dev,mw,LH,D3DFMT_L8,L8,mw),*tp=new_texture(dev,256,8,D3DFMT_A8R8G8B8,(BYTE*)pal8,1024);
    if(!ti||!tp){log_line("Creation des textures refusee.");return FALSE;}
#define v vs[k]
    readmem((void*)rec,v.record,0x30);*(DWORD*)(v.record+0x14)=(VID+k)&0xffff;
    readmem((void*)img_obj,v.object,0x40);*(DWORD*)(v.object+4)=0x10000;*(DWORD*)(v.object+8)=(DWORD)ti;
    readmem((void*)pal_obj,v.pobject,0x40);*(DWORD*)(v.pobject+4)=0x10000;*(DWORD*)(v.pobject+8)=(DWORD)tp;
    v.tex=(DWORD)ti;v.pal=(DWORD)tp;
    v.desc[0]=(DWORD)(v.object+8);v.desc[1]=1;v.desc[2]=0x3000+2*k;v.desc[3]=(DWORD)v.object;
    v.pdesc[0]=(DWORD)(v.pobject+8);v.pdesc[1]=1;v.pdesc[2]=0x3001+2*k;v.pdesc[3]=(DWORD)v.pobject;
    float row=0.5f/8;v.texinfo[0]=(DWORD)v.desc;v.texinfo[1]=(DWORD)v.pdesc;memcpy(&v.texinfo[2],&row,4);
#undef v
    log_line("Etiquette \"%s\" composee avec les lettres du jeu (%d colonnes sur %d, %d couleurs).",word,lw,mw,np-1);
    return TRUE;
}
static void make_labels(void){
    if(ready||failed||online())return;
    if(!d3lo){HMODULE m=GetModuleHandleA("d3d9.dll");if(!m)return;
        IMAGE_NT_HEADERS *nt=(IMAGE_NT_HEADERS*)((BYTE*)m+((IMAGE_DOS_HEADER*)m)->e_lfanew);d3lo=(BYTE*)m;d3hi=d3lo+nt->OptionalHeader.SizeOfImage;}
    DWORD rec=0,e=texinfo_entry(MODELE,&rec);if(!e)return;
    failed=TRUE;
    IDirect3DTexture9 *mimg=as_texture(rd(rd(rd(e))));if(!mimg){log_line("Modele : texture inattendue.");return;}
    IDirect3DDevice9 *dev=NULL;if(FAILED(IDirect3DTexture9_GetDevice(mimg,&dev))||!dev){log_line("Pas de peripherique.");return;}
    BOOL ok=TRUE;for(int k=0;k<nwords&&ok;k++){ok=compose(k,words[k],rec,e,dev);if(ok&&opt_of_row(k)>=0)ok=compose(k+NW,words_off[k],rec,e,dev);}
    nvirt=nwords;
    if(ok&&open_screen==72&&nwords+3<=NW){title_k=nwords;
        for(int t=0;t<3&&ok;t++)ok=compose(title_k+t,titles[t],rec,e,dev);
        if(ok)nvirt=title_k+3;}
    if(ok)for(int i=0;i<nopt;i++)show_state(opts[i].row,opts[i].state);
    IDirect3DDevice9_Release(dev);
    if(ok){ready=TRUE;failed=FALSE;}
}

void *volatile mo_frame_suite;volatile DWORD mo_frame_key;
__attribute__((naked)) static void frame_hook(void){
    __asm__ volatile("mov _mo_frame_key,%eax\n cmp %eax,8(%esp)\n jne 1f\n mov _mo_frame,%eax\n mov %eax,8(%esp)\n 1:\n jmp *_mo_frame_suite\n");
}

void *volatile mo_decide_suite,*volatile mo_errsnd_suite;static volatile LONG skip_error;
__attribute__((used)) int __cdecl mo_decide_c(DWORD menu,DWORD player){
    if(online()||!ready)return 0;
    DWORD row=((DWORD(__attribute__((thiscall))*)(DWORD,DWORD))VA(0xbd7010))(menu,player);if(!row)return 0;
    DWORD k=rd(row+4);
    if(k==CLE&&open_screen){
        DWORD m=(DWORD)VA(0x138a770),cnt=rd(m+4),arr=rd(m+0xc),obj=0;
        for(DWORD i=0;i<cnt&&i<512;i++){DWORD o=rd(arr+i*4);if(o&&rd(o+4)==open_screen){obj=o;break;}}
        log_line("Ecran %lu demande dans la scene 9 : objet %s (%lu objets d ecran)%s.",open_screen,obj?"present":"absent",cnt,obj?"":" : chargement demande");
        rfmode=1;
        if(!obj){((void(__attribute__((thiscall))*)(DWORD,DWORD,DWORD))VA(0xa7c200))((DWORD)VA(0x1383d00),open_screen,1);InterlockedExchange(&want72,1);}
        ((void(__attribute__((thiscall))*)(DWORD,DWORD))VA(0xa7ed60))((DWORD)VA(0x1383d00),open_screen);
        ((void(__cdecl*)(DWORD,DWORD))VA(0x957c40))(0x1001a,0);
        InterlockedExchange(&skip_error,1);return 1;}
    if(k<=CLE||k>=CLE+(DWORD)nwords)return 0;
    int o=opt_of_row(k-CLE);
    if(o<0){log_line("Ligne \"%s\" choisie (rien a faire).",words[k-CLE]);return 0;}
    opts[o].state=!opts[o].state;show_state(opts[o].row,opts[o].state);
    for(int i=0;i<opts[o].nkeys;i++)WritePrivateProfileStringA(opts[o].section,opts[o].keys[i],opts[o].state?"1":"0",opts[o].file);
    ((void(__cdecl*)(DWORD,DWORD))VA(0x957c40))(0x1001a,0);
    InterlockedExchange(&skip_error,1);
    log_line("Option \"%s\" : %s (%s).",words_off[opts[o].row],opts[o].state?"ON":"OFF",fname(opts[o].file));
    return 1;
}
__attribute__((naked)) static void decide_hook(void){
    __asm__ volatile("pushal\n pushfl\n push 40(%esp)\n push %ecx\n call _mo_decide_c\n add $8,%esp\n test %eax,%eax\n jnz 1f\n"
                     "popfl\n popal\n jmp *_mo_decide_suite\n"
                     "1:\n popfl\n popal\n mov $-1,%eax\n ret $4\n");
}
__attribute__((used)) int __cdecl mo_errsnd_c(void){return InterlockedExchange(&skip_error,0);}
__attribute__((naked)) static void errsnd_hook(void){
    __asm__ volatile("pushal\n pushfl\n call _mo_errsnd_c\n test %eax,%eax\n jnz 1f\n popfl\n popal\n jmp *_mo_errsnd_suite\n"
                     "1:\n popfl\n popal\n ret\n");
}

void *volatile mo_known_suite;
__attribute__((used)) int __cdecl mo_known_c(DWORD list){
    if(list==LISTE)return 1;
    return (BYTE)((int(__cdecl*)(DWORD))mo_known_suite)(list);
}
PRESERVING_ENTRY(known_hook,mo_known_c)

#define PAS 33
static struct{DWORD obj;float mine;}moved[48];
static const float NX[10]={281,281,211,281,281,281,213,211,211,281},SX[10]={166,166,96,166,166,166,98,96,96,166};
static int orig_slot(int i){DWORD pos=position>9?9:position;return i<(int)pos?i:i==(int)pos?-1:i-1;}
static DWORD main_helper(void){
    DWORD m=(DWORD)VA(0x138a770),cnt=rd(m+4),arr=rd(m+0xc);
    for(DWORD i=0;i<cnt&&i<512;i++){DWORD o=rd(arr+i*4);if(o&&rd(o+4)==9&&rd(o)==(DWORD)VA(0x100f22c))return rd(o+0xa0);}
    return 0;
}
static float remap_y(float y){int r=(int)((y-230.0f)/37.0f);if(r<0||r>9)return y;return y-(float)(r*(37-PAS));}
__attribute__((used)) void __cdecl mo_anim_note(DWORD obj){
    if(!tenrows||online())return;
    static DWORD helper,when;DWORD now=GetTickCount();
    if(now-when>250){helper=main_helper();when=now;}
    if(!helper||obj<helper+0x618||obj>helper+0x1a2c+10*0xf0)return;
    DWORD off=obj-helper;int slot=-1,row=-1;
    if(off==0x618)slot=0;
    else if(off>=0x71c&&off<0x71c+10*0xf8&&(off-0x71c)%0xf8==0){row=(off-0x71c)/0xf8;slot=1+row;}
    else if(off>=0x10cc&&off<0x10cc+10*0xf0&&(off-0x10cc)%0xf0==0)slot=11+(off-0x10cc)/0xf0;
    else if(off>=0x1a2c&&off<0x1a2c+10*0xf0&&(off-0x1a2c)%0xf0==0)slot=21+(off-0x1a2c)/0xf0;
    if(slot<0)return;
    if(row>=0){
        int o=orig_slot(row);float *px=(float*)(obj+0x34),x=*px;
        float nx=o<0?211:NX[o],sx=o<0?96:SX[o];
        if(x==NX[row]&&x!=nx)*px=nx;else if(x==SX[row]&&x!=sx)*px=sx;}
    float *py=(float*)(obj+0x38),y=*py;
    if(moved[slot].obj==obj&&y==moved[slot].mine)return;
    float ny;
    if(slot==0){
        int i=(int)rd(helper+0x1c);if(i<0||i>9){moved[slot].obj=0;return;}
        float code=(float)((i>7?211:248)+37*i);
        if(y==code)ny=(float)(248+PAS*i);
        else ny=remap_y(y);
    }else ny=remap_y(y);
    *py=ny;moved[slot].obj=obj;moved[slot].mine=ny;
}
void *volatile mo_anim_suite;
__attribute__((naked)) static void anim_hook(void){
    __asm__ volatile("pushal\n pushfl\n push %ecx\n call _mo_anim_note\n add $4,%esp\n popfl\n popal\n jmp *_mo_anim_suite\n");
}

void *volatile mo_play_suite;
static struct{DWORD off,id;}entry[32];static int nentry;static volatile LONG entry_done;
static BOOL entry_anim(DWORD id){return id==0x250003||id==0x250038||id==0x250005;}
static DWORD __attribute__((thiscall)) play_my(DWORD a,DWORD id){
    DWORD r=((DWORD(__attribute__((thiscall))*)(DWORD,DWORD))mo_play_suite)(a,id);
    if(!rf_return&&entry_anim(id)&&!online()){DWORD h=main_helper();
        if(h&&a>=h+0x71c&&a<h+0x1a2c+10*0xf0){DWORD off=a-h;int k=0;
            while(k<nentry&&entry[k].off!=off)k++;
            if(k<32){entry[k].off=off;entry[k].id=id;if(k==nentry)nentry++;}}}
    if(rf_return&&!online()){DWORD h=main_helper();
        if(h&&a>=h&&a<h+0x2b08){
            if(id==0x250022)rf_return=0;
            else if(rd(a+0xc4)&&!*(BYTE*)(a+0xe5))((void(__attribute__((thiscall))*)(DWORD,DWORD))VA(0xbc8fb0))(a,0x7fffffff);}}
    return r;
}

__attribute__((used)) void mo_tick(void){
    if(online())return;
    long scr=(long)rd((DWORD)VA(0x138a7ac));
    if(scr==9)make_labels();
    if(rfmods&&scr==72&&title_k&&nvirt>=title_k+3){
        DWORD m=(DWORD)VA(0x138a770),cnt=rd(m+4),arr=rd(m+0xc),obj=0;
        for(DWORD i=0;i<cnt&&i<512;i++){DWORD o=rd(arr+i*4);if(o&&rd(o+4)==72){obj=o;break;}}
        if(obj&&rd(obj)==(DWORD)VA(0xd9c0cc)){*(DWORD*)(obj+0x530)=VID+title_k+vpage;*(DWORD*)(obj+0x628)=VID+title_k+vprev;rf_frame(obj);}}
    {static WCHAR kept[2];
        if(rfmods&&helps[0][0]){kept[0]=helps[0][0];kept[1]=helps_off[0][0];helps[0][0]=0;helps_off[0][0]=0;}
        else if(!rfmods&&kept[0]){helps[0][0]=kept[0];helps_off[0][0]=kept[1];kept[0]=0;}}
    if(rf_return&&scr==9){DWORD now=GetTickCount();
        if(!rf_return_t)rf_return_t=now;
        if(!entry_done){DWORD h=main_helper();entry_done=1;
            for(int k=0;h&&k<nentry;k++){DWORD a=h+entry[k].off;
                if(rd(a)!=(DWORD)VA(0x1021f58))continue;
                ((DWORD(__attribute__((thiscall))*)(DWORD,DWORD))mo_play_suite)(a,entry[k].id);
                ((void(__attribute__((thiscall))*)(DWORD,DWORD))VA(0xbc8fb0))(a,0x7fffffff);}
            log_line("Retour au menu principal : %d ligne(s) remises en place aussitot.",h?nentry:0);}
        {volatile int *c=(volatile int*)VA(0x247ef5c);int v=*c;
            if(v<0x25)*c=0x25;else if(v>0x26&&v<0x2c)*c=0x2c;else if(v>0x2d&&v<0x3b)*c=0x3b;}
        if(now-rf_return_t>2500)rf_return=0;}
    if(scr==9&&rfmode==2){rfmode=0;log_line("Retour au menu principal.");}
    DWORD now=GetTickCount();
    if(want72){static DWORD t0;if(!t0)t0=now;
        DWORD m=(DWORD)VA(0x138a770),cnt=rd(m+4),arr=rd(m+0xc),obj=0;
        for(DWORD i=0;i<cnt&&i<512;i++){DWORD o=rd(arr+i*4);if(o&&rd(o+4)==72){obj=o;break;}}
        if(obj||scr==72){if(scr!=72){((void(__attribute__((thiscall))*)(DWORD,DWORD))VA(0xa7ed60))((DWORD)VA(0x1383d00),72);}
            if(scr==72){want72=0;t0=0;log_line("Ecran 72 ouvert (%lu ms).",now-t0);}}
        else if(now-t0>8000){want72=0;t0=0;rfmode=0;log_line("Ecran 72 toujours absent apres 8 s : abandon.");}}
}
void *volatile mo_pads_suite;
__attribute__((naked)) static void pads_hook(void){
    __asm__ volatile("call *_mo_pads_suite\n pushal\n pushfl\n call _mo_tick\n popfl\n popal\n ret\n");
}

static const DWORD rfids[2][40]={
    {0x20063,0x20064,0x20065,0x20066,0x20067,0x20076,0x20068,0x20069,0x2006a,0x2006b,0x2006c,0x2006d,0x2006e,0x2006f,0x2008d,
     0x20070,0x20071,0x20072,0x20073,0x20074,0x20075,0x2008b,0x2008c,0x2008e,0x10101,0x10102,0x10103,0x10104,0x10105,0x10126,
     0x10127,0x10128,0x10129,0x1012a,0x10130,0x10131},
    {0x2014c,0x2014d,0x2014e,0x2014f,0x20150,0x20151,0x20152,0x20153,0x20154,0x20155,0x20156,0x20157,0x20158,0x20159,0x2015a,
     0x2015b,0x2015c,0x2015d,0x2015e,0x2015f,0x20160,0x20161,0x10106,0x10107,0x10108,0x10109,0x1010a,0x10117,0x10116,0x1012b,0x1012c}};
static WCHAR rf_help[256]=L"Check an option to turn it on, uncheck it to turn it off. Back: save and return.";
static const WCHAR *orig_text(DWORD lang,DWORD id,DWORD flags){return ((const WCHAR*(__cdecl*)(DWORD,DWORD,DWORD))VA(0xb45ba0))(lang,id,flags);}
__attribute__((used)) const WCHAR *__cdecl mo_rfname_c(int page,DWORD lang,DWORD id,DWORD flags){
    if(!rfmods)return orig_text(lang,id,flags);

    int slot=-1;if(id!=0xffffffff)for(int i=0;i<40;i++)if(rfids[page&1][i]==id){slot=i;break;}
    if(slot>=0)for(int i=0;i<nopt;i++)if(opts[i].page==vpage&&opts[i].slot==slot)return opts[i].nval?opts[i].disp:opts[i].wname;
    return L"";
}
#define RFNAME_HOOK(page) \
    __attribute__((naked)) static void rfname_hook##page(void){ \
        __asm__ volatile("push %ecx\n push %edx\n push 20(%esp)\n push 20(%esp)\n push 20(%esp)\n push $" #page "\n call _mo_rfname_c\n add $16,%esp\n pop %edx\n pop %ecx\n ret\n");}
RFNAME_HOOK(0) RFNAME_HOOK(1)
void *volatile mo_rfname_suite[10],*volatile mo_rfhelp_suite;
__attribute__((used)) const WCHAR *__cdecl mo_rfhelp_c(DWORD lang,DWORD id,DWORD flags){
    if(rfmods&&id==0x2002a)return rf_help;
    return orig_text(lang,id,flags);
}
__attribute__((naked)) static void rfhelp_hook(void){
    __asm__ volatile("push %ecx\n push %edx\n push 20(%esp)\n push 20(%esp)\n push 20(%esp)\n call _mo_rfhelp_c\n add $12,%esp\n pop %edx\n pop %ecx\n ret\n");
}

void *volatile mo_start_suite,*volatile mo_start2_suite;
void *volatile mo_curs_suite[4];
#define CURS_HOOK(k) __attribute__((naked)) static void curs_hook##k(void){ \
    __asm__ volatile("cmpl $0,_rf_return\n je 1f\n movl $0x60,8(%esp)\n 1:\n jmp *_mo_curs_suite+" #k "*4\n");}
CURS_HOOK(0) CURS_HOOK(1) CURS_HOOK(2) CURS_HOOK(3)
__attribute__((naked)) static void start2_hook(void){
    __asm__ volatile("cmpl $0,_rf_return\n je 1f\n movl $0x60,8(%esp)\n 1:\n jmp *_mo_start2_suite\n");
}
__attribute__((naked)) static void start_hook(void){
    __asm__ volatile("cmpl $0,_rf_return\n je 1f\n movl $0x60,8(%esp)\n 1:\n jmp *_mo_start_suite\n");
}

static DWORD rf_key(int i){return i<30?0x533+i:0x547+i;}
void *volatile mo_rfhide_suite[2];

static void opt_disp(int i){
    WCHAR *d=opts[i].disp;int n=0;
    for(const WCHAR *w=opts[i].wname;*w&&n<80;)d[n++]=*w++;
    d[n++]=L':';d[n++]=L' ';
    for(const char *c=opts[i].vals[opts[i].val];*c&&n<94;)d[n++]=(WCHAR)(BYTE)*c++;
    d[n]=0;
}
static void opt_apply(int i){
    if(!opts[i].func[0]||opts[i].nofn)return;
    HMODULE m=GetModuleHandleA(opts[i].module);FARPROC f=m?GetProcAddress(m,opts[i].func):NULL;
    if(!f){opts[i].nofn=TRUE;log_line("%s absent (ou sans %s) : valeur ecrite dans l ini seulement, prise au prochain lancement.",opts[i].module,opts[i].func);return;}
    char *e;const char *v=opts[i].vals[opts[i].val];long n=strtol(v,&e,10);if(e==v)n=-1;
    ((void(__cdecl*)(int))f)((int)n);
    log_line("%s(%ld) : \"%s\".",opts[i].func,n,v);
}
static void opt_anim(DWORD obj,int i,int on){
    DWORD a=obj+0x2e80+opts[i].slot*0xf0;if(rd(a)!=(DWORD)VA(0x1021f58))return;
    *(BYTE*)(a+0xe7)=0;
    ((DWORD(__attribute__((thiscall))*)(DWORD,DWORD))rd(rd(a)+8))(a,on?0x1c30006:0x1c30007);
    if(!on)((void(__attribute__((thiscall))*)(DWORD,DWORD))VA(0xbc8fb0))(a,0x7fffffff);
}
static void rf_frame(DWORD obj){
    int phys=(int)rd(obj+0x20f0c)&1;BYTE *st=(BYTE*)(obj+0x20f11+phys*0x28);
    for(int i=0;i<nopt;i++){if(!opts[i].nval||opts[i].page!=vpage)continue;
        int want=opts[i].val!=0,b=st[opts[i].slot]!=0;if(b==want)continue;
        opts[i].val=(opts[i].val+1)%opts[i].nval;int on=opts[i].val!=0;opts[i].state=on;
        st[opts[i].slot]=(BYTE)on;if(b!=on)opt_anim(obj,i,on);
        opt_disp(i);opt_apply(i);}
    int cur=(int)rd(obj+0x20f00);const WCHAR *h=rf_help;
    for(int i=0;i<nopt;i++)if(opts[i].page==vpage&&opts[i].slot==cur&&opts[i].help[0]){h=opts[i].help;break;}
    if(rd(obj+0x1f7f4)!=(DWORD)h){*(DWORD*)(obj+0x1f7f4)=(DWORD)h;*(DWORD*)(obj+0x1f7cc)&=0xffff0fff;}
}
static void rf_fill(DWORD obj,int phys,int vp){
    BYTE *st=(BYTE*)(obj+0x20f11+phys*0x28),*ab=(BYTE*)(obj+0x20f61+phys*0x28);
    memset(st,0,40);memset(ab,1,40);
    for(int k=0;k<nopt;k++)if(opts[k].page==vp){ab[opts[k].slot]=0;st[opts[k].slot]=(BYTE)(opts[k].state!=0);}
}
static void rf_read(DWORD obj,int phys,int vp){
    for(int k=0;k<nopt;k++)if(opts[k].page==vp&&!opts[k].nval)opts[k].state=*(BYTE*)(obj+0x20f11+phys*0x28+opts[k].slot)!=0;
}
__attribute__((used)) int __cdecl mo_rfhide_c(int which,DWORD obj,DWORD chars){
    if(!(rfmods||rfmode==1)||online())return 0;
    int phys=(chars&0xff)?0:1;
    if(which==1&&rfmods){
        rf_read(obj,1-phys,vpage);

        int dir=0;
        if(rd(obj+0x350+0xc8)==0x1c30015&&rd(obj+0x350+0xcc)<2)dir=1;
        else if(rd(obj+0x260+0xc8)==0x1c30013&&rd(obj+0x260+0xcc)<2)dir=-1;
        if(!dir){dir=1;for(int d=0;d<5;d++){WORD h=*(WORD*)(VA(0x2457928)+d*0x2c);if(h&0x1000)dir=-1;if(h&0x2000)dir=1;}
            log_line("Sens de la page devine par la manette.");}
        vprev=vpage;vpage=(vpage+dir+3)%3;
        rf_fill(obj,phys,vpage);
        log_line("Page %d -> page %d.",vprev+1,vpage+1);}
    BYTE *ab=(BYTE*)(obj+0x20f61+phys*0x28);
    for(int i=0;i<40;i++)((void(__attribute__((thiscall))*)(DWORD,DWORD,DWORD))VA(0xbd6660))(obj,rf_key(i),1);
    for(int i=0;i<40;i++){BOOL used=FALSE;for(int k=0;k<nopt;k++)if(opts[k].page==vpage&&opts[k].slot==i)used=TRUE;
        ab[i]=used?0:1;
        if(!used)((void(__attribute__((thiscall))*)(DWORD,DWORD,DWORD))VA(0xbd6630))(obj,rf_key(i),1);}
    return 1;
}
#define RFHIDE_HOOK(k) \
    __attribute__((naked)) static void rfhide_hook##k(void){ \
        __asm__ volatile("pushal\n pushfl\n push 40(%esp)\n push %ecx\n push $" #k "\n call _mo_rfhide_c\n add $12,%esp\n test %eax,%eax\n jnz 1f\n popfl\n popal\n jmp *_mo_rfhide_suite+" #k "*4\n" \
                         "1:\n popfl\n popal\n ret $4\n");}
RFHIDE_HOOK(0) RFHIDE_HOOK(1)

void *volatile mo_rflayout_suite;
__attribute__((used)) void __cdecl mo_rflayout_c(DWORD obj){
    if(rfmode!=1||online())return;
    rfmode=2;rfmods=1;vpage=0;vprev=2;
    rf_fill(obj,0,0);rf_fill(obj,1,1);
    log_line("Random Filter en mode mods : %d option(s), 3 pages.",nopt);
}
__attribute__((naked)) static void rflayout_hook(void){
    __asm__ volatile("pushal\n pushfl\n push %ecx\n call _mo_rflayout_c\n add $4,%esp\n popfl\n popal\n jmp *_mo_rflayout_suite\n");
}

void *volatile mo_rflast_suite[2];
#define RFLAST_HOOK(k) \
    __attribute__((naked)) static void rflast_hook##k(void){ \
        __asm__ volatile("cmpl $0,_rfmods\n je 1f\n xor %eax,%eax\n ret $4\n 1:\n jmp *_mo_rflast_suite+" #k "*4\n");}
RFLAST_HOOK(0) RFLAST_HOOK(1)

void *volatile mo_rfsave_suite;
__attribute__((used)) int __cdecl mo_rfsave_c(DWORD obj){
    if(!rfmods)return 0;
    rf_read(obj,(int)rd(obj+0x20f0c)&1,vpage);
    for(int i=0;i<nopt;i++){
        if(opts[i].nval){char cur[32];GetPrivateProfileStringA(opts[i].section,opts[i].keys[0],"",cur,sizeof cur,opts[i].file);
            if(_stricmp(cur,opts[i].vals[opts[i].val])){
                for(int k=0;k<opts[i].nkeys;k++)WritePrivateProfileStringA(opts[i].section,opts[i].keys[k],opts[i].vals[opts[i].val],opts[i].file);
                log_line("Option page %d case %d : %s (%s).",opts[i].page+1,opts[i].slot+1,opts[i].vals[opts[i].val],fname(opts[i].file));}
            continue;}
        int st=opts[i].state;
        if(st!=GetPrivateProfileIntA(opts[i].section,opts[i].keys[0],-1,opts[i].file)){
            for(int k=0;k<opts[i].nkeys;k++)WritePrivateProfileStringA(opts[i].section,opts[i].keys[k],st?"1":"0",opts[i].file);
            log_line("Option page %d case %d : %s (%s).",opts[i].page+1,opts[i].slot+1,st?"ON":"OFF",fname(opts[i].file));}}
    rfmods=0;rfmode=0;rf_return=1;rf_return_t=0;entry_done=0;log_line("Random Filter (mode mods) ferme : reglages du random non touches.");
    return 1;
}
__attribute__((naked)) static void rfsave_hook(void){
    __asm__ volatile("pushal\n pushfl\n push %ecx\n call _mo_rfsave_c\n add $4,%esp\n test %eax,%eax\n jnz 1f\n popfl\n popal\n jmp *_mo_rfsave_suite\n"
                     "1:\n popfl\n popal\n ret $4\n");
}

void *volatile mo_rfdef_suite;
__attribute__((used)) int __cdecl mo_rfdef_c(DWORD obj){
    if(!rfmods)return 0;
    int phys=(int)rd(obj+0x20f0c)&1,n=0;
    for(int k=0;k<nopt;k++)if(opts[k].page==vpage){n++;
        if(opts[k].nval){int ch=opts[k].val!=opts[k].def;opts[k].val=opts[k].def;opts[k].state=opts[k].def!=0;opt_disp(k);if(ch)opt_apply(k);}
        else opts[k].state=opts[k].def;}
    rf_fill(obj,phys,vpage);
    for(int k=0;k<nopt;k++)if(opts[k].page==vpage)opt_anim(obj,k,opts[k].state);
    log_line("Defaut : page %d, %d option(s) remise(s) a leur valeur par defaut.",vpage+1,n);
    return 1;
}
__attribute__((naked)) static void rfdef_hook(void){
    __asm__ volatile("pushal\n pushfl\n push %ecx\n call _mo_rfdef_c\n add $4,%esp\n test %eax,%eax\n jnz 1f\n popfl\n popal\n jmp *_mo_rfdef_suite\n"
                     "1:\n popfl\n popal\n ret\n");
}

static DWORD WINAPI worker(void *u){
    (void)u;
    static const BYTE pm[6]={0x55,0x8b,0xec,0x83,0xec,0x6c};
    for(int n=0;n<600&&memcmp(VA(0x83b7f0),pm,6)&&VA(0x83b7f0)[0]!=0xe9;n++)Sleep(100);
    Sleep(1000);
    if(GetModuleHandleA("DOA5LR-ModOptions.asi")){log_line("module_separe_present inactif");return 0;}
    static const BYTE pk[6]={0x55,0x8b,0xec,0x8b,0x45,0x08};
    static const BYTE pl[7]={0x55,0x8b,0xec,0x56,0x8b,0x75,0x08};
    if(!ac_detour(VA(0xbd5b90),pl,7,(void*)record_hook,&mo_record_suite)||!ac_detour(VA(0xbd5b20),pl,7,(void*)texinfo_hook,&mo_texinfo_suite)){
        log_line("Recherche d images (0xbd5b90 / 0xbd5b20) : debut inattendu, rien fait.");return 0;}
    if(!ac_detour(VA(0xae8cd0),pk,6,(void*)label_hook,&mo_label_suite)){log_line("0xae8cd0 : debut inattendu, rien fait.");return 0;}
    if(!ac_detour(VA(0xae8a00),pk,6,(void*)help_hook,&mo_help_suite))log_line("0xae8a00 : debut inattendu, pas d aides.");
    int ns=(int)(sizeof help_sites/sizeof help_sites[0]),nt=0;
    for(int i=0;i<ns;i++)if(ac_call(VA(help_sites[i]),VA(0xb45ba0),(void*)text_hook,&mo_text_suite))nt++;
    if(nt<ns)log_line("Aides : %d appel(s) accroche(s) sur %d.",nt,ns);
    mo_frame_key=CLE;
    if(!ac_detour(VA(0xae8f90),pk,6,(void*)known_hook,&mo_known_suite))log_line("0xae8f90 : debut inattendu, le retour ne fermera pas la fenetre.");
    if(nopt||open_screen){
        if(!ac_call(VA(0xaf25ea),VA(0xbd6b10),(void*)decide_hook,&mo_decide_suite))log_line("0xaf25ea : appel inattendu, options inactives.");
        else if(!ac_call(VA(0xaf269c),VA(0x957c40),(void*)errsnd_hook,&mo_errsnd_suite))log_line("0xaf269c : appel inattendu (son d erreur garde).");}
    if(!ac_call(VA(0xaf0a77),VA(0xaeb8c0),(void*)frame_hook,&mo_frame_suite))log_line("0xaf0a77 : appel inattendu, pas de cadre.");
    if(target==0){void **vt=(void**)VA(0x1021f58);
        if(vt[5]!=(void*)VA(0xbcac10)&&!ac_foreign(vt[5]))log_line("Animator emplacement 5 inattendu : pas de resserrement des lignes.");
        else if(!ac_slot(&vt[5],(void*)anim_hook,&mo_anim_suite))log_line("Animator emplacement 5 : rien fait.");}
    if(open_screen==72){void **vt=(void**)VA(0x1021f58);
        if(vt[2]!=(void*)VA(0xbca000)&&!ac_foreign(vt[2]))log_line("Animator emplacement 2 inattendu : entree rejouee au retour.");
        else if(!ac_slot(&vt[2],(void*)play_my,&mo_play_suite))log_line("Animator emplacement 2 : rien fait.");}
    if(open_screen==72&&!ac_call(VA(0xaecf33),VA(0xbd77c0),(void*)start_hook,&mo_start_suite))log_line("0xaecf33 : appel inattendu (curseur au retour).");
    if(open_screen==72&&!ac_call(VA(0xaf2f84),VA(0xbd77c0),(void*)start2_hook,&mo_start2_suite))log_line("0xaf2f84 : appel inattendu (curseur au retour).");
    if(open_screen==72){static const DWORD cs_[4]={0xaf2f01,0xaf2f0b,0xaf2f15,0xaf2f1f};void *hk[4]={(void*)curs_hook0,(void*)curs_hook1,(void*)curs_hook2,(void*)curs_hook3};
        for(int i=0;i<4;i++)if(!ac_call(VA(cs_[i]),VA(0xbd6790),hk[i],&mo_curs_suite[i]))log_line("0x%lx : appel inattendu (curseur de la liste au retour).",cs_[i]);}
    if(open_screen==72){
        static const DWORD ns[10]={0x6325bd,0x6325db,0x633131,0x633154,0x633671,0x63368c,0x632f33,0x632f56,0x6334f0,0x63350b};
        for(int i=0;i<10;i++)if(!ac_call(VA(ns[i]),VA(0xb45ba0),i<6?(void*)rfname_hook0:(void*)rfname_hook1,&mo_rfname_suite[i]))log_line("0x%lx : appel inattendu (noms).",ns[i]);
        if(!ac_call(VA(0x63265b),VA(0xb45ba0),(void*)rfhelp_hook,&mo_rfhelp_suite))log_line("0x63265b : appel inattendu (aide).");

        if(!ac_call(VA(0x632a9f),VA(0x631990),(void*)rfsave_hook,&mo_rfsave_suite))log_line("0x632a9f : appel inattendu : PAS de mode mods (les reglages du random seraient ecrases).");
        else{
            if(!ac_call(VA(0x632b32),VA(0x630ef0),(void*)rflast_hook0,&mo_rflast_suite[0])||!ac_call(VA(0x632bbe),VA(0x630ef0),(void*)rflast_hook1,&mo_rflast_suite[1]))log_line("Derniere coche : appel inattendu.");
            if(!ac_call(VA(0x632152),VA(0x631d30),(void*)rfhide_hook0,&mo_rfhide_suite[0])||!ac_call(VA(0x632e3b),VA(0x631d30),(void*)rfhide_hook1,&mo_rfhide_suite[1]))log_line("Cases cachees : appel inattendu.");
            if(!ac_call(VA(0x632cb4),VA(0x632320),(void*)rflayout_hook,&mo_rflayout_suite))log_line("0x632cb4 : appel inattendu, pas de mode mods.");
            if(!ac_call(VA(0x633cd7),VA(0x6326f0),(void*)rfdef_hook,&mo_rfdef_suite))log_line("0x633cd7 : appel inattendu : Y (Defaut) ecrirait les reglages du random.");}}
    if(!ac_detour(VA(0x83b7f0),pm,6,(void*)pads_hook,&mo_pads_suite)){log_line("0x83b7f0 : debut inattendu, rien fait.");return 0;}
    if(!ac_call(VA(0xaf0fae),VA(0xa900c0),(void*)build_hook,&mo_build_suite)){log_line("0xaf0fae : appel inattendu, rien fait.");return 0;}
    if(target)log_line("Pret : \"%s\" a la place de la ligne 0x%lx du menu principal, %d option(s), hors ligne.",words[0],target,nopt);
    else log_line("Pret : \"%s\" en 10e ligne du menu principal, %d option(s), hors ligne.",words[0],nopt);
    return 0;
}
static void utf8_to_w(const char *s,WCHAR *w,int n){if(!MultiByteToWideChar(CP_UTF8,0,s,-1,w,n))w[0]=0;}
static void upper(char *s){for(;*s;s++)*s=(char)toupper((unsigned char)*s);}

static const char *const OPTIONS_DEFAUT[][24]={
 {"ModOptions","Nom","EXTRA OPTIONS","Aide","Mod options.","Remplacer","0","Position","6","Ecran","72","Cadre","64",NULL},
 {"ModOptionsPages","Titre1","DOA5TOOLS","Titre2","ROLLBACK","Titre3","EXTRA CONTENT",NULL},
#define T(n,nom,cle,def,aide) {"ModOption" #n,"Nom",nom,"Page","1","Fichier","DOA5Tools.ini","Section","DOA5Tools","Cles",cle,"Defaut",def,"Aide",aide " Next launch.",NULL}
 T(1,"Lobby","Lobby","1","Lobby menu in Online."),
 T(2,"Join fix","JoinFix","1","Steam invitations and F7 room link."),
 T(3,"Lobby chat","LobbyChat","1","Text chat in lobbies."),
 T(4,"Lobby name","LobbyName","1","Lobby names in the lobby list."),
 T(5,"Connection filter and kick","ConnFilter","1","CONNECTION setting and working Kick button (host)."),
 T(6,"Rematch","Rematch","1","Rematch menu with 2 players in the queue."),
 T(7,"Wi-Fi / wired","WiFiWired","1","Shows your connection type to others."),
 T(8,"Stage slots","StageSlots","1","Extra stage slots and PS4 stages."),
 T(9,"AoT fix","AoTFix","1","Attack on Titan Power Blow fixes."),
 T(10,"Console restart points","ConsoleRestart","1","PS4 restart points on console stages."),
 T(11,"Arcade console stages","ArcadeConsole","1","Console stages in Arcade, Time Attack, Survival."),
 T(12,"Replay menu","ReplayMenu","1","Replay list with filters and archives."),
 T(13,"Game intro skip","Skip","1","Skips the startup videos."),
 T(14,"Borderless","Borderless","1","Borderless fullscreen (F11)."),
 T(15,"Ultrawide","Ultrawide","1","Desktop resolution and wide screens."),
 T(16,"High FPS","HighFps","0","120/144 fps in fights (F3). @[color:red]Online: rollback only.@[color:resume]"),
#undef T
 {"ModOption20","Nom","Rollback","Page","2","Fichier","scripts\\DOA5Rollback.ini","Section","Rollback","Cles","Enabled","Defaut","1",
  "Aide","Rollback netcode in lobbies. @[color:red]Next launch.@[color:resume]",NULL},
 {"ModOption21","Nom","Rollback delay","Page","2","Fichier","scripts\\DOA5Rollback.ini","Section","Rollback","Cles","Delay",
  "Valeurs","AUTO, 0, 1, 2, 3, 4, 5","Defaut","AUTO","Module","DOA5Rollback.asi","Fonction","DOA5RB_SetDelay",
  "Aide","Input delay in frames (AUTO = from the ping). Next online match.",NULL},
#define E(n,nom,cle,def,aide) {"ModOption" #n,"Nom",nom,"Page","3","Fichier","scripts\\DOA5Extra.ini","Section","DOA5Extra","Cles",cle,"Defaut",def,"Aide",aide " Next launch.",NULL}
 E(30,"Hitboxes","Hitbox","1","Hitboxes in Training (F6)."),
 E(31,"Critical Burst","OpenBurst","1","PS4 Critical Burst effect."),
 E(32,"PS4 costumes","StatefulCostumes","1","Transformations, tearing and effects."),
 E(33,"Breast press","BustPress","1","Breast press on contact."),
 E(34,"PS4 breast spring","PS4Pressure","1","PS4 spring after a contact."),
 E(35,"Console music","ConsoleMusic","1","Music of the console stages."),
 E(36,"PS4 LR breast motion","SoftEngineLR","1","LR in BREAST MOTION (needs the PS4 data)."),
 E(37,"PS4 skin","SkinSSS","1","PS4 skin and sweat."),
 E(38,"SoftBody","SoftBody","0","Experimental breast physics."),
#undef E
};
static const char *g_ini;
static BOOL g_defauts;
static const char *Defaut(const char *sec,const char *cle){
    for(size_t i=0;i<sizeof OPTIONS_DEFAUT/sizeof *OPTIONS_DEFAUT;i++){
        const char *const *e=OPTIONS_DEFAUT[i];if(!e[0]||_stricmp(e[0],sec))continue;
        for(int k=1;e[k]&&e[k+1];k+=2)if(!_stricmp(e[k],cle))return e[k+1];
        return NULL;}
    return NULL;
}

static DWORD Lire(const char *sec,const char *cle,const char *def,char *b,DWORD n){
    const BOOL option=!_strnicmp(sec,"ModOption",9)&&_stricmp(sec,"ModOptions")&&_stricmp(sec,"ModOptionsPages");
    if(!option||!g_defauts){DWORD r=GetPrivateProfileStringA(sec,cle,"\x01",b,n,g_ini);if(!(r==1&&b[0]==1))return r;}
    const char *v=Defaut(sec,cle);if(!v)v=def;
    lstrcpynA(b,v,(int)n);return (DWORD)strlen(b);
}
static int LireInt(const char *sec,const char *cle,int def){char b[32];if(!Lire(sec,cle,"",b,sizeof b)||!b[0])return def;return atoi(b);}

void ModOptions_Demarrer(void){
    base=(BYTE*)GetModuleHandleW(NULL);g_ini=CheminIni();
    char p[MAX_PATH];lstrcpynA(p,g_ini,MAX_PATH);
    {char t[8];g_defauts=!GetPrivateProfileStringA("ModOption1","Nom","",t,sizeof t,p);for(int i=2;g_defauts&&i<=NOPT;i++){char sec[16];sprintf(sec,"ModOption%d",i);if(GetPrivateProfileStringA(sec,"Nom","",t,sizeof t,p))g_defauts=FALSE;}}
    log_line("ModOptions 1.0 (hors ligne)");
    target=LireInt("ModOptions","Remplacer",66);
    mo_frame=LireInt("ModOptions","Cadre",64);
    position=LireInt("ModOptions","Position",6);
    open_screen=LireInt("ModOptions","Ecran",0);
    for(int t=0;t<3;t++){char k[16],v[64];sprintf(k,"Titre%d",t+1);if(Lire("ModOptionsPages",k,"",v,sizeof v)&&v[0]){strncpy(titles[t],v,63);upper(titles[t]);}}
    char b[512],v2[512];
    if(Lire("ModOptions","Nom","",b,sizeof b)&&b[0]){strncpy(words[0],b,63);upper(words[0]);}
    if(Lire("ModOptions","Aide","",b,sizeof b))utf8_to_w(b,helps[0],256);
    char *dir=strrchr(p,'\\');
    for(int i=1;i<=NOPT&&(open_screen==72||nwords<NW);i++){char sec[16];sprintf(sec,"ModOption%d",i);
        if(!Lire(sec,"Nom","",b,sizeof b)||!b[0])continue;
        int n=nopt;
        Lire(sec,"Fichier","",v2,sizeof v2);if(!v2[0])continue;
        if(dir){memcpy(opts[n].file,p,dir-p+1);opts[n].file[dir-p+1]=0;}strncat(opts[n].file,v2,MAX_PATH-strlen(opts[n].file)-1);
        Lire(sec,"Section","",opts[n].section,64);
        Lire(sec,"Cles","",v2,sizeof v2);
        for(char *t=strtok(v2," ,");t&&opts[n].nkeys<2;t=strtok(NULL," ,"))strncpy(opts[n].keys[opts[n].nkeys++],t,31);
        if(!opts[n].section[0]||!opts[n].nkeys){memset(&opts[n],0,sizeof opts[n]);continue;}
        opts[n].def=LireInt(sec,"Defaut",1)!=0;
        opts[n].state=GetPrivateProfileIntA(opts[n].section,opts[n].keys[0],opts[n].def,opts[n].file)!=0;
        if(Lire(sec,"Valeurs","",v2,sizeof v2)&&v2[0]){
            for(char *t=strtok(v2,", ");t&&opts[n].nval<8;t=strtok(NULL,", "))strncpy(opts[n].vals[opts[n].nval++],t,11);
            char d[32],c[32];Lire(sec,"Defaut","",d,sizeof d);
            GetPrivateProfileStringA(opts[n].section,opts[n].keys[0],"",c,sizeof c,opts[n].file);
            opts[n].def=0;opts[n].val=-1;
            for(int j=0;j<opts[n].nval;j++){if(!_stricmp(d,opts[n].vals[j]))opts[n].def=j;if(!_stricmp(c,opts[n].vals[j]))opts[n].val=j;}
            if(opts[n].val<0)opts[n].val=opts[n].def;
            opts[n].state=opts[n].val!=0;
            Lire(sec,"Module","",opts[n].module,64);Lire(sec,"Fonction","",opts[n].func,64);}
        if(Lire(sec,"Aide","",v2,sizeof v2)&&v2[0])utf8_to_w(v2,opts[n].help,160);
        if(open_screen==72){
            utf8_to_w(b,opts[n].wname,64);{int pg=LireInt(sec,"Page",1);opts[n].page=pg<1?0:pg>3?2:pg-1;}
            int k=0;for(int j=0;j<n;j++)if(opts[j].page==opts[n].page)k++;
            if(k>=31){log_line("Page %d pleine (31 options au plus) : \"%s\" ignoree.",opts[n].page+1,b);memset(&opts[n],0,sizeof opts[n]);continue;}
            opts[n].slot=k;opts[n].row=-1;if(opts[n].nval)opt_disp(n);nopt++;
            log_line("Option \"%s\" : %s, page %d case %d (%s).",b,opts[n].nval?opts[n].vals[opts[n].val]:opts[n].state?"ON":"OFF",opts[n].page+1,k+1,fname(opts[n].file));continue;}
        upper(b);snprintf(words[nwords],64,"%s ON",b);snprintf(words_off[nwords],64,"%s OFF",b);
        if(Lire(sec,"AideOn","",v2,sizeof v2))utf8_to_w(v2,helps[nwords],256);
        if(Lire(sec,"AideOff","",v2,sizeof v2))utf8_to_w(v2,helps_off[nwords],256);
        opts[n].row=nwords;nopt++;nwords++;
        log_line("Option \"%s\" : %s (%s).",b,opts[n].state?"ON":"OFF",fname(opts[n].file));}
    for(int i=1;i<NW&&nwords<NW;i++){char k[16];sprintf(k,"Ligne%d",i);
        if(!Lire("ModOptionsLignes",k,"",b,sizeof b)||!b[0])continue;
        strncpy(words[nwords],b,63);upper(words[nwords]);
        sprintf(k,"Aide%d",i);if(Lire("ModOptionsLignes",k,"",b,sizeof b))utf8_to_w(b,helps[nwords],256);
        nwords++;}
    if(nwords<2&&!(open_screen==72&&nopt)){log_line("Aucune option ni ligne : rien fait.");return;}
    HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);if(t)CloseHandle(t);
}

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include "commun.h"
#include "accroche.h"

static BYTE *base;
#define RVA(rva) (base+(rva))
#define JOURNAL(libelle,a) Journal("LobbyName",libelle,(long)(a),0,0)
static BOOL readmem(const void *p,void *buf,SIZE_T n){SIZE_T d=0;return ReadProcessMemory(GetCurrentProcess(),p,buf,n,&d)&&d==n;}
static DWORD rd32(const void *p){DWORD v=0;readmem(p,&v,4);return v;}

#define RVA_DECODE_NOM    0x6FCE40
#define RVA_COUCHE_STEAM  0xF83C60
#define RVA_IMENGINE      0xCBF190
#define RVA_IME_IMPL_VT   0x972D04
#define RVA_PADS          0x2057928

typedef unsigned long long U64;
static void *matchmaking(void){HMODULE m=GetModuleHandleA("steam_api.dll");void*(*f)(void)=m?(void*(*)(void))GetProcAddress(m,"SteamMatchmaking"):NULL;return f?f():NULL;}
static const char *lobby_data(void *mm,U64 id,const char *k){typedef const char*(__attribute__((thiscall))*F)(void*,U64,const char*);return ((F)(*(void***)mm)[19])(mm,id,k);}
static U64 lobby_by_index(void *mm,int i){typedef U64*(__attribute__((thiscall))*F)(void*,U64*,int);U64 id=0;((F)(*(void***)mm)[12])(mm,&id,i);return id;}
static long numeric(void *mm,U64 lobby,int n){
    char key[32];sprintf(key,"NumericalProperty%d",n);const char *v=lobby_data(mm,lobby,key);
    return v&&v[0]?atol(v):0;
}

typedef struct {int n;long v;} Prop;
static int props_match(void *mm,U64 lobby,const Prop *p,int np){
    for(int i=0;i<np;i++)if(numeric(mm,lobby,p[i].n)!=p[i].v)return 0;
    return 1;
}

typedef struct {DWORD key;DWORD tick;WCHAR name[24];} C;
static C cache[16];
static BOOL find_name(DWORD props,WCHAR *outname){
    DWORD now=GetTickCount();
    for(int i=0;i<16;i++)if(cache[i].key==props && now-cache[i].tick<3000){wcscpy(outname,cache[i].name);return outname[0]!=0;}
    DWORD obj=rd32(RVA(RVA_COUCHE_STEAM)),ctx=obj?rd32((void*)(obj+0x3c)):0;
    DWORD arr=ctx?rd32((void*)ctx):0,cnt=ctx?rd32((void*)(ctx+8)):0;
    if(!arr || cnt>200)return FALSE;
    int row=-1;DWORD nprops=0;
    for(DWORD i=0;i<cnt;i++)if(rd32((void*)(arr+i*0xa8+8))==props){row=(int)i;nprops=rd32((void*)(arr+i*0xa8+0xc));break;}
    if(row<0)return FALSE;
    Prop p[32];int np=0;
    for(DWORD i=0;i<nprops && i<32;i++){
        BYTE e[0x18];if(!readmem((void*)(props+i*0x18),e,sizeof e))break;
        if(e[8]==1){p[np].n=*(int*)e;p[np].v=*(long*)(e+0x10);np++;}
    }
    void *mm=matchmaking();if(!mm)return FALSE;
    U64 lobby=lobby_by_index(mm,row);
    int how=1;
    if(!lobby || !props_match(mm,lobby,p,np)){
        lobby=0;how=0;
        for(int i=0;i<200;i++){U64 id=lobby_by_index(mm,i);if(!id)break;
            if(props_match(mm,id,p,np)){if(lobby){lobby=0;break;}lobby=id;how=2;}}
        if(!lobby)how=0;
    }
    WCHAR nm[24];int k=0;
    if(lobby)for(int n=14;n<=23;n++){
        char key[32];sprintf(key,"NumericalProperty%d",n);const char *v=lobby_data(mm,lobby,key);
        unsigned long x=v?strtoul(v,NULL,10):0;
        nm[k++]=(WCHAR)(x>>16);nm[k++]=(WCHAR)(x&0xffff);}
    nm[k]=0;
    C *c=&cache[0];
    for(int i=0;i<16;i++)if(cache[i].key==props || cache[i].tick<c->tick){c=&cache[i];if(cache[i].key==props)break;}
    c->key=props;c->tick=now;wcsncpy(c->name,nm,23);c->name[23]=0;
    static int told;if(told<20){told++;Journal("LobbyName","ligne (rang, trouve 1=rang 2=recherche 0=non, nb proprietes)",row,how,np);}
    wcscpy(outname,c->name);return outname[0]!=0;
}
typedef BOOL(__attribute__((thiscall))*Decode_t)(void*,WCHAR*,DWORD*);
static Decode_t decode_orig;
static BOOL __attribute__((thiscall)) decode_wrap(void *self,WCHAR *buf,DWORD *list){
    BOOL r=decode_orig(self,buf,list);
    if(buf && !buf[0] && list && list[0]){WCHAR nm[24];if(find_name(list[0],nm))wcscpy(buf,nm);}
    return r;
}
static void install_list(void){
    static const BYTE pro[6]={0x55,0x8b,0xec,0x8b,0x45,0x08};BYTE *t=RVA(RVA_DECODE_NOM);
    for(int n=0;n<600 && memcmp(t,pro,6);n++)Sleep(100);
    JOURNAL("liste (1=ok)",ac_detour(t,pro,6,(void*)decode_wrap,(void*volatile*)&decode_orig)!=NULL);
}

#define MAX_CHARS 20
static CRITICAL_SECTION lock;
static WCHAR typed[MAX_CHARS+1];static int typed_len;static WCHAR before[MAX_CHARS+1];
static volatile LONG typing,typed_version;
static volatile DWORD opened_at,last_enter;
static BYTE *ime_impl;
static const int box_x=405,box_y=722,box_w=955,box_h=56;
static const WCHAR *hint=L"Enter: OK    Esc: cancel";
static void deposit_text(const WCHAR *w){
    WCHAR t[MAX_CHARS+1];wcsncpy(t,w,MAX_CHARS);t[MAX_CHARS]=0;
    wcscpy((WCHAR*)(ime_impl+4+8),t);ime_impl[4]=1;
}
static void open_box(void){
    WCHAR w[MAX_CHARS+1]=L"Fighting Online!";char v[64];
    if(GetPrivateProfileStringA("LobbyName","Name","",v,sizeof v,CheminIni()) && v[0])MultiByteToWideChar(CP_UTF8,0,v,-1,w,MAX_CHARS+1);
    w[MAX_CHARS]=0;
    EnterCriticalSection(&lock);wcscpy(before,w);wcscpy(typed,w);typed_len=(int)wcslen(typed);LeaveCriticalSection(&lock);
    opened_at=GetTickCount();InterlockedIncrement(&typed_version);InterlockedExchange(&typing,1);
    JOURNAL("saisie_ouverte",1);
}
static void close_box(int how){
    if(!InterlockedExchange(&typing,0))return;
    WCHAR t[MAX_CHARS+1];EnterCriticalSection(&lock);wcscpy(t,how==2 && typed_len?typed:before);LeaveCriticalSection(&lock);
    deposit_text(t);
    if(how==2 && t[0]){char u[64];WideCharToMultiByte(CP_UTF8,0,t,-1,u,sizeof u,NULL,NULL);WritePrivateProfileStringA("LobbyName","Name",u,CheminIni());}
    JOURNAL("saisie_fermee (1=annulee,2=validee)",how);
}
static void type_char(WCHAR c){
    EnterCriticalSection(&lock);
    if(c==8){if(typed_len)typed[--typed_len]=0;}
    else if(c>=0x20 && c!=0x7f && typed_len<MAX_CHARS){typed[typed_len++]=c;typed[typed_len]=0;}
    LeaveCriticalSection(&lock);InterlockedIncrement(&typed_version);
}

static HHOOK msg_hook;
static LRESULT CALLBACK on_message(int code,WPARAM remove,LPARAM lp){
    MSG *m=(MSG*)lp;
    if(code==HC_ACTION && remove==PM_REMOVE && typing) switch(m->message){
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if(m->wParam==VK_RETURN){last_enter=GetTickCount()|1;if(!(m->lParam&0x40000000) && GetTickCount()-opened_at>300)close_box(2);}
        else if(m->wParam==VK_ESCAPE)close_box(1);
        else if(m->wParam==VK_BACK)type_char(8);
        else if(m->wParam!=VK_F4 || m->message!=WM_SYSKEYDOWN)TranslateMessage(m);
        else break;
        m->message=WM_NULL;break;
    case WM_KEYUP: case WM_SYSKEYUP:
        if(m->wParam!=VK_F4){TranslateMessage(m);m->message=WM_NULL;}
        break;
    case WM_CHAR: case WM_SYSCHAR: {
        WCHAR c=(WCHAR)m->wParam;
        if(!IsWindowUnicode(m->hwnd)){char a=(char)m->wParam;MultiByteToWideChar(CP_ACP,0,&a,1,&c,1);}
        if(c>=0x20)type_char(c);
        m->message=WM_NULL;break;}
    case WM_DEADCHAR: case WM_SYSDEADCHAR:
        m->message=WM_NULL;break;
    }
    return CallNextHookEx(msg_hook,code,remove,lp);
}
static void hook_messages(HWND w){
    if(msg_hook || !w)return;
    msg_hook=SetWindowsHookExW(WH_GETMESSAGE,on_message,NULL,GetWindowThreadProcessId(w,NULL));
    JOURNAL("clavier_fenetre",msg_hook!=NULL);
}

static DWORD prev_start,pending_start;
static void Tic(void){
    DWORD now=GetTickCount(),held=0;BYTE *p=RVA(RVA_PADS);
    for(int d=0;d<5;d++)held|=*(DWORD*)(p+d*0x2c);
    if(held&0x100&~prev_start)pending_start=now|1;
    prev_start=held&0x100;
    if(pending_start && now-pending_start>=150){
        DWORD e=last_enter;BOOL start=!(e && (e>pending_start?e-pending_start:pending_start-e)<600);pending_start=0;
        if(start && typing && now-opened_at>500)close_box(1);
    }
}

static IDirect3DTexture9 *box_tex;static int tex_w,tex_h;static LONG drawn_version=-1;
static HDC dib_dc;static HBITMAP dib;static DWORD *dib_bits;static int dib_w,dib_h;
static HFONT font,font_small;
static void paint_box(int w,int h){
    RECT r={0,0,w,h};HBRUSH b=CreateSolidBrush(RGB(20,12,14));FillRect(dib_dc,&r,b);DeleteObject(b);
    b=CreateSolidBrush(RGB(170,40,48));RECT e={0,h-2>0?h-2:0,w,h};FillRect(dib_dc,&e,b);RECT l={0,0,4,h};FillRect(dib_dc,&l,b);DeleteObject(b);
    SetBkMode(dib_dc,TRANSPARENT);
    int pad=h/3,hint_w=w*34/100;
    SelectObject(dib_dc,font_small);SetTextColor(dib_dc,RGB(170,170,170));
    RECT hr={w-hint_w,0,w-pad,h};
    DrawTextW(dib_dc,hint,-1,&hr,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    WCHAR s[MAX_CHARS+4];EnterCriticalSection(&lock);swprintf(s,MAX_CHARS+4,L"%ls|",typed);LeaveCriticalSection(&lock);
    SelectObject(dib_dc,font);SetTextColor(dib_dc,RGB(255,255,255));
    int room=w-hint_w-2*pad;const WCHAR *q=s;SIZE z;
    while(*q && GetTextExtentPoint32W(dib_dc,q,lstrlenW(q),&z) && z.cx>room)q++;
    RECT tr={pad,0,pad+room,h};
    DrawTextW(dib_dc,q,-1,&tr,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
}
static BOOL update_texture(IDirect3DDevice9 *dev,int w,int h){
    if(w!=dib_w || h!=dib_h) {
        if(dib){DeleteObject(dib);dib=NULL;}
        if(!dib_dc)dib_dc=CreateCompatibleDC(NULL);
        BITMAPINFO bi={{sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB}};
        dib=CreateDIBSection(dib_dc,&bi,DIB_RGB_COLORS,(void**)&dib_bits,NULL,0);
        if(!dib)return FALSE;
        SelectObject(dib_dc,dib);dib_w=w;dib_h=h;
        if(font)DeleteObject(font);
        if(font_small)DeleteObject(font_small);
        font=CreateFontW(-h*52/100,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
        font_small=CreateFontW(-h*38/100,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
    }
    if(!box_tex || tex_w!=w || tex_h!=h) {
        if(box_tex){IDirect3DTexture9_Release(box_tex);box_tex=NULL;}
        if(FAILED(IDirect3DDevice9_CreateTexture(dev,w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&box_tex,NULL)))return FALSE;
        tex_w=w;tex_h=h;
    }
    paint_box(w,h);GdiFlush();
    D3DLOCKED_RECT lr;
    if(FAILED(IDirect3DTexture9_LockRect(box_tex,0,&lr,NULL,0)))return FALSE;
    for(int y=0;y<h;y++){DWORD *src=dib_bits+y*w,*dst=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(int x=0;x<w;x++)dst[x]=(src[x]&0xffffff)|0xff000000;}
    IDirect3DTexture9_UnlockRect(box_tex,0);
    return TRUE;
}
static void draw_box(IDirect3DDevice9 *dev){
    IDirect3DSurface9 *bb=NULL;
    if(FAILED(IDirect3DDevice9_GetBackBuffer(dev,0,0,D3DBACKBUFFER_TYPE_MONO,&bb)) || !bb)return;
    D3DSURFACE_DESC sd;IDirect3DSurface9_GetDesc(bb,&sd);
    float gh=(float)sd.Height,gw=gh*16/9,gx=0,gy=0;
    if(gw>sd.Width){gw=(float)sd.Width;gh=gw*9/16;gy=(sd.Height-gh)/2;}else gx=(sd.Width-gw)/2;
    float s=gh/1080;int w=(int)(box_w*s),h=(int)(box_h*s);
    if(w<16 || h<8){IDirect3DSurface9_Release(bb);return;}
    LONG v=typed_version;
    if(drawn_version!=v || !box_tex || tex_w!=w || tex_h!=h) {
        if(!update_texture(dev,w,h)){IDirect3DSurface9_Release(bb);return;}
        drawn_version=v;
    }
    IDirect3DStateBlock9 *sb=NULL;
    if(FAILED(IDirect3DDevice9_CreateStateBlock(dev,D3DSBT_ALL,&sb))){IDirect3DSurface9_Release(bb);return;}
    IDirect3DSurface9 *rt=NULL;IDirect3DDevice9_GetRenderTarget(dev,0,&rt);
    IDirect3DDevice9_SetRenderTarget(dev,0,bb);
    D3DVIEWPORT9 vp={0,0,sd.Width,sd.Height,0,1};IDirect3DDevice9_SetViewport(dev,&vp);
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_SetVertexShader(dev,NULL);IDirect3DDevice9_SetPixelShader(dev,NULL);
    IDirect3DDevice9_SetFVF(dev,D3DFVF_XYZRHW|D3DFVF_TEX1);
    IDirect3DDevice9_SetTexture(dev,0,(IDirect3DBaseTexture9*)box_tex);
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
    float x0=(float)(int)(gx+box_x*s)-0.5f,y0=(float)(int)(gy+box_y*s)-0.5f,x1=x0+w,y1=y0+h;
    struct {float x,y,z,w,u,v;} q[4]={{x0,y0,0,1,0,0},{x1,y0,0,1,1,0},{x0,y1,0,1,0,1},{x1,y1,0,1,1,1}};
    IDirect3DDevice9_DrawPrimitiveUP(dev,D3DPT_TRIANGLESTRIP,2,q,sizeof(q[0]));
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_SetRenderTarget(dev,0,rt);if(rt)IDirect3DSurface9_Release(rt);
    IDirect3DStateBlock9_Apply(sb);IDirect3DStateBlock9_Release(sb);
    IDirect3DSurface9_Release(bb);
}
typedef HRESULT(WINAPI *Present_t)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
typedef HRESULT(WINAPI *CreateDevice_t)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
typedef struct {void **vt;Present_t present;} DevHook;
static DevHook dev_hooks[4];static volatile LONG dev_hook_count;
static CreateDevice_t createdevice_original;
static HRESULT WINAPI present_hook(IDirect3DDevice9 *d,const RECT *a,const RECT *b,HWND w,const RGNDATA *r){
    void **vt=*(void***)d;Present_t orig=NULL;
    for(LONG i=0;i<dev_hook_count;i++)if(dev_hooks[i].vt==vt)orig=dev_hooks[i].present;
    if(typing)draw_box(d);
    return orig?orig(d,a,b,w,r):D3D_OK;
}
static void hook_device(IDirect3DDevice9 *dev){
    void **vt=*(void***)dev;
    for(LONG i=0;i<dev_hook_count;i++)if(dev_hooks[i].vt==vt)return;
    if(dev_hook_count>=4 || vt[17]==(void*)present_hook)return;
    LONG n=dev_hook_count;dev_hooks[n].vt=vt;dev_hooks[n].present=(Present_t)vt[17];InterlockedIncrement(&dev_hook_count);
    JOURNAL("affichage",ac_slot(&vt[17],(void*)present_hook,(void*volatile*)&dev_hooks[n].present)!=NULL);
}
static HRESULT WINAPI createdevice_hook(IDirect3D9 *d3d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS *pp,IDirect3DDevice9 **out){
    HRESULT hr=createdevice_original(d3d,a,t,w,f,pp,out);
    if(SUCCEEDED(hr) && out && *out && t==D3DDEVTYPE_HAL){hook_device(*out);hook_messages(pp && pp->hDeviceWindow?pp->hDeviceWindow:w);}
    return hr;
}
static void install_d3d_hooks(void){
    void **vt=D3D9_Methodes();
    if(!vt){JOURNAL("affichage",0);return;}
    ac_slot(&vt[16],(void*)createdevice_hook,(void*volatile*)&createdevice_original);
}

static void *ime_vt[16],*ime_orig[16];
static volatile LONG pending;
typedef BOOL(__attribute__((thiscall))*F1_t)(void*,DWORD);
typedef DWORD(__attribute__((thiscall))*F0_t)(void*);

static DWORD __attribute__((thiscall)) s7(void *t){DWORD r=((F0_t)ime_orig[7])(t);if(InterlockedExchange(&pending,0))open_box();return r;}

static BOOL __attribute__((thiscall)) s11(void *t,DWORD a){BOOL r=((F1_t)ime_orig[11])(t,a);InterlockedExchange(&pending,a==1);return r;}
static void install_ime(void){
    DWORD eng=0;for(int n=0;n<600 && !eng;n++){eng=rd32(RVA(RVA_IMENGINE));if(!eng)Sleep(100);}
    DWORD impl=eng?rd32((void*)(eng+8)):0;
    void **vt=impl?*(void***)impl:NULL;
    if(!vt || vt!=(void**)RVA(RVA_IME_IMPL_VT)){JOURNAL("champ_room (0=moteur introuvable ou inattendu)",0);return;}
    memcpy(ime_orig,vt,sizeof ime_orig);memcpy(ime_vt,vt,sizeof ime_vt);
    ime_vt[7]=(void*)s7;ime_vt[11]=(void*)s11;
    ime_impl=(BYTE*)impl;
    JOURNAL("champ_room (1=ok)",InterlockedCompareExchangePointer((void**)impl,ime_vt,vt)==vt);
}

static DWORD WINAPI worker(void *u){
    (void)u;
    install_d3d_hooks();
    install_list();
    install_ime();
    return 0;
}
void LobbyName_Demarrer(void){
    base=(BYTE*)GetModuleHandleW(NULL);
    InitializeCriticalSection(&lock);
    JOURNAL("debut 1.0",0);
    AjouterTic(Tic);
    HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);if(t)CloseHandle(t);
}

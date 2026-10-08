#include <windows.h>
#include <d3d9.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "commun.h"

static BYTE *base;
#define RVA(rva) (base+(rva))
#define JOURNAL(libelle,a) Journal("LobbyChat",libelle,(long)(a),0,0)
static BOOL readmem(const void *p,void *buf,SIZE_T n){SIZE_T d=0;return ReadProcessMemory(GetCurrentProcess(),p,buf,n,&d)&&d==n;}
static DWORD rd32(const void *p){DWORD v=0;readmem(p,&v,4);return v;}
static BOOL patch_ptr(void **slot,void *want_old,void *nw,void **old_out){
    DWORD o;if(!VirtualProtect(slot,4,PAGE_READWRITE,&o))return FALSE;
    BOOL ok=!want_old || *slot==want_old;
    if(ok){if(old_out)*old_out=*slot;*slot=nw;}
    VirtualProtect(slot,4,o,&o);return ok;
}

#define RVA_CHAT_VT        0xC0F5BC
#define RVA_CHAT_UPDATE    0x6F68F0
#define RVA_LOBBY_VT       0xC0F96C
#define RVA_LOBBY_UPDATE   0x706F80
#define RVA_CHAT_ADD       0x5571A0
#define RVA_CHAT_OBJ       0xF81AD8
#define RVA_LINE_FORMAT    0x6F6D64
#define RVA_LINE_FORMAT_S  0xC0F554
#define RVA_NET_PTR        0x20924BC
#define RVA_PADS           0x2057928
#define RVA_MENU_LOCK      0xF8A840

typedef uint64_t SteamID;
static void *steam(const char *fn){HMODULE m=GetModuleHandleA("steam_api.dll");void*(*f)(void)=m?(void*(*)(void))GetProcAddress(m,fn):NULL;return f?f():NULL;}
static SteamID lobby_id(void){DWORD net=rd32(RVA(RVA_NET_PTR));SteamID id=0;if(net)readmem((void*)(net+0xb0),&id,8);return id;}
static const char TAG[]="DOA5CHAT3";
#define MAX_CHARS 120
static BOOL steam_send(const WCHAR *text){
    SteamID lobby=lobby_id();void *mm=steam("SteamMatchmaking");
    if(!mm || !lobby)return FALSE;
    char msg[600];int k=sprintf(msg,"%s",TAG);
    if(!WideCharToMultiByte(CP_UTF8,0,text,-1,msg+k,(int)sizeof msg-k,NULL,NULL))return FALSE;
    typedef BOOL(__attribute__((thiscall))*F)(void*,SteamID,const void*,int);
    return ((F)(*(void***)mm)[26])(mm,lobby,msg,(int)strlen(msg)+1)&0xff;
}
typedef struct {SteamID lobby,user;uint8_t type;uint8_t pad[3];uint32_t chat;} LobbyChatMsg;
static void on_chat(const LobbyChatMsg *m){
    void *mm=steam("SteamMatchmaking"),*fr=steam("SteamFriends");
    if(!mm || !fr || m->lobby!=lobby_id())return;
    char buf[600];SteamID user=0;int type=0;
    typedef int(__attribute__((thiscall))*G)(void*,SteamID,int,SteamID*,void*,int,int*);
    int n=((G)(*(void***)mm)[27])(mm,m->lobby,(int)m->chat,&user,buf,(int)sizeof buf-1,&type);
    if(n<=0)return;
    buf[n]=0;
    if(strncmp(buf,TAG,sizeof TAG-1))return;
    const char *body=buf+sizeof TAG-1;
    char name[80]="?";
    typedef const char*(__attribute__((thiscall))*P)(void*,SteamID);
    const char *s=((P)(*(void***)fr)[7])(fr,user);
    if(s)lstrcpynA(name,s,sizeof name);
    user=0;
    char line[700];snprintf(line,sizeof line,"%s: %s",name,body);
    WCHAR w[256];if(!MultiByteToWideChar(CP_UTF8,0,line,-1,w,256))return;w[255]=0;
    for(WCHAR *p=w;*p;p++)if(*p<0x20)*p=L' ';
    BYTE sender[0x24];memset(sender,0,sizeof sender);
    typedef char(__attribute__((thiscall))*R)(void*,const void*,const WCHAR*);
    char ok=((R)RVA(RVA_CHAT_ADD))(RVA(RVA_CHAT_OBJ),sender,w);
    JOURNAL("recu ajoute",ok!=0);
}
typedef struct CB{void **vt;uint8_t flags;uint8_t pad[3];int id;int size;}CB;
static void __attribute__((thiscall)) cb_run_result(CB *s,void *p,int io,uint64_t call){(void)s;(void)io;(void)call;on_chat(p);}
static void __attribute__((thiscall)) cb_run(CB *s,void *p){(void)s;on_chat(p);}
static int __attribute__((thiscall)) cb_size(CB *s){return s->size;}
static void *cb_vt[3]={(void*)cb_run_result,(void*)cb_run,(void*)cb_size};
static CB cb={cb_vt,0,{0},507,(int)sizeof(LobbyChatMsg)};

static const WCHAR line_format[]=L"%ls%ls";
static BOOL patch_format(void){
    BYTE *p=RVA(RVA_LINE_FORMAT);DWORD old;
    if(p[0]!=0x68 || rd32(p+1)!=(DWORD)RVA(RVA_LINE_FORMAT_S))return FALSE;
    if(!VirtualProtect(p+1,4,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *(DWORD*)(p+1)=(DWORD)line_format;VirtualProtect(p+1,4,old,&old);FlushInstructionCache(GetCurrentProcess(),p,5);
    return TRUE;
}

static CRITICAL_SECTION lock;
static WCHAR typed[MAX_CHARS+1];static int typed_len;
static volatile LONG typing,typed_version;
static volatile LONG want_close;
static WCHAR outgoing[MAX_CHARS+1];
static volatile DWORD chat_tick,lobby_tick,opened_at,last_close,last_enter;
static BYTE *volatile lobby_obj;
static const int box_x=100,box_y=890,box_w=1720,box_h=56;
static const WCHAR *hint=L"Enter: send    Esc: cancel";
static BOOL blocked(void){return typing || GetTickCount()-last_close<400;}

static BOOL lobby_free(void){
    BYTE *l=lobby_obj;DWORD now=GetTickCount();
    if(!l || now-lobby_tick>300 || now-chat_tick>300 || now-last_close<500)return FALSE;
    BYTE d=1;readmem(l+0x90,&d,1);
    return !rd32(l+0xbf4) && !rd32(l+0x2360) && !rd32(l+0x3a1c) && !d;
}
static BYTE lock_saved[8];static BOOL lock_on;
static void apply_lock(void){
    BYTE *l=RVA(RVA_MENU_LOCK);
    if(blocked()){if(!lock_on){memcpy(lock_saved,l,8);lock_on=TRUE;}memset(l,1,8);}
    else if(lock_on){memcpy(l,lock_saved,8);lock_on=FALSE;}
}
static void open_box(int how){
    EnterCriticalSection(&lock);typed_len=0;typed[0]=0;LeaveCriticalSection(&lock);
    opened_at=GetTickCount();InterlockedIncrement(&typed_version);InterlockedExchange(&typing,1);
    apply_lock();
    JOURNAL("saisie_ouverte (1=entree,2=start)",how);
}
static void close_box(int how){
    if(!InterlockedExchange(&typing,0))return;
    last_close=GetTickCount()|1;
    if(how==2){EnterCriticalSection(&lock);wcscpy(outgoing,typed);LeaveCriticalSection(&lock);}
    InterlockedExchange(&want_close,how==2?2:1);
    if(how!=2)JOURNAL("saisie_fermee (1=annulee,3=fenetre_partie)",how);
}
static void type_char(WCHAR c){
    EnterCriticalSection(&lock);
    if(c==8){if(typed_len)typed[--typed_len]=0;}
    else if(c>=0x20 && c!=0x7f && typed_len<MAX_CHARS){typed[typed_len++]=c;typed[typed_len]=0;}
    LeaveCriticalSection(&lock);InterlockedIncrement(&typed_version);
}
static void send_typed(void){
    EnterCriticalSection(&lock);int n=typed_len;LeaveCriticalSection(&lock);
    close_box(n?2:1);
}

static HHOOK msg_hook;
static LRESULT CALLBACK on_message(int code,WPARAM remove,LPARAM lp){
    MSG *m=(MSG*)lp;
    if(code==HC_ACTION && remove==PM_REMOVE){
        if(m->message==WM_KEYDOWN && m->wParam==VK_RETURN && !(m->lParam&0x40000000)){
            last_enter=GetTickCount()|1;
            if(!typing && lobby_free()){open_box(1);m->message=WM_NULL;return CallNextHookEx(msg_hook,code,remove,lp);}
        }
        if(typing) switch(m->message){
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if(m->wParam==VK_RETURN){if(!(m->lParam&0x40000000) && GetTickCount()-opened_at>300)send_typed();}
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
    }
    return CallNextHookEx(msg_hook,code,remove,lp);
}
static void hook_messages(HWND w){
    if(msg_hook || !w)return;
    msg_hook=SetWindowsHookExW(WH_GETMESSAGE,on_message,NULL,GetWindowThreadProcessId(w,NULL));
    JOURNAL("clavier_fenetre",msg_hook!=NULL);
}

static const GUID LC_IID_DI8A={0xBF798030,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const GUID LC_IID_DI8W={0xBF798031,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const GUID LC_SYSKEYBOARD={0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
typedef HRESULT(WINAPI *GetState_t)(void*,DWORD,void*);
typedef HRESULT(WINAPI *GetData_t)(void*,DWORD,void*,DWORD*,DWORD);
typedef HRESULT(WINAPI *GetCaps_t)(void*,DIDEVCAPS*);
static GetState_t di_state[2];static GetData_t di_data[2];static GetCaps_t di_caps[2];static void **di_vt[2];
static int di_which(void *self){return *(void***)self==di_vt[1]?1:0;}
static BOOL is_keyboard(void *self){
    static void *known[16];static BYTE kind[16];static LONG n;
    for(LONG i=0;i<n;i++)if(known[i]==self)return kind[i];
    DIDEVCAPS c;memset(&c,0,sizeof c);c.dwSize=sizeof c;
    BOOL k=SUCCEEDED(di_caps[di_which(self)](self,&c)) && GET_DIDEVICE_TYPE(c.dwDevType)==DI8DEVTYPE_KEYBOARD;
    if(n<16){known[n]=self;kind[n]=(BYTE)k;n++;}
    return k;
}
static HRESULT WINAPI di_state_hook(void *self,DWORD cb,void *data){
    HRESULT r=di_state[di_which(self)](self,cb,data);
    if(SUCCEEDED(r) && data && blocked() && is_keyboard(self))memset(data,0,cb);
    return r;
}
static HRESULT WINAPI di_data_hook(void *self,DWORD cbo,void *rg,DWORD *inout,DWORD flags){
    HRESULT r=di_data[di_which(self)](self,cbo,rg,inout,flags);
    if(SUCCEEDED(r) && inout && blocked() && is_keyboard(self))*inout=0;
    return r;
}
static void hook_dinput(void){
    HMODULE m=GetModuleHandleA("dinput8.dll");
    typedef HRESULT(WINAPI *Create_t)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    Create_t create=m?(Create_t)GetProcAddress(m,"DirectInput8Create"):NULL;
    int done=0;
    const GUID *iids[2]={&LC_IID_DI8A,&LC_IID_DI8W};
    for(int w=0;create && w<2;w++){
        void *di=NULL;
        if(FAILED(create(GetModuleHandleW(NULL),DIRECTINPUT_VERSION,iids[w],&di,NULL))||!di)continue;
        void *dev=NULL;
        typedef HRESULT(WINAPI *CD_t)(void*,REFGUID,void**,LPUNKNOWN);
        if(SUCCEEDED(((CD_t)(*(void***)di)[3])(di,&LC_SYSKEYBOARD,&dev,NULL)) && dev){
            void **vt=*(void***)dev;DWORD old;
            if(vt[9]!=(void*)di_state_hook && VirtualProtect(&vt[9],8,PAGE_EXECUTE_READWRITE,&old)){
                di_vt[w]=vt;di_caps[w]=(GetCaps_t)vt[3];di_state[w]=(GetState_t)vt[9];di_data[w]=(GetData_t)vt[10];
                vt[9]=(void*)di_state_hook;vt[10]=(void*)di_data_hook;VirtualProtect(&vt[9],8,old,&old);done++;
            }
            ((ULONG(WINAPI*)(void*))(*(void***)dev)[2])(dev);
        }
        ((ULONG(WINAPI*)(void*))(*(void***)di)[2])(di);
    }
    JOURNAL("clavier_directinput (vtables)",done);
}

static DWORD prev_start,pending_start;
static void Tic(void){
    DWORD now=GetTickCount();
    apply_lock();
    if(InterlockedExchange(&want_close,0)==2){
        WCHAR t[MAX_CHARS+1];EnterCriticalSection(&lock);wcscpy(t,outgoing);LeaveCriticalSection(&lock);
        JOURNAL("envoi (1=ok)",steam_send(t));
    }

    BYTE *p=RVA(RVA_PADS);DWORD held=0;
    for(int d=0;d<5;d++)held|=*(DWORD*)(p+d*0x2c);
    if(held&0x100&~prev_start)pending_start=now|1;
    prev_start=held&0x100;
    BOOL start=FALSE;
    if(pending_start && now-pending_start>=150){
        DWORD e=last_enter;start=!(e && (e>pending_start?e-pending_start:pending_start-e)<600);pending_start=0;
    }
    if(!typing){if(start && lobby_free())open_box(2);return;}
    if(now-chat_tick>500){close_box(3);return;}
    if(start && now-opened_at>300){EnterCriticalSection(&lock);int n=typed_len;LeaveCriticalSection(&lock);if(!n)close_box(1);}
}

static IDirect3DTexture9 *box_tex;static int tex_w,tex_h;static LONG drawn_version=-1;
static HDC dib_dc;static HBITMAP dib;static DWORD *dib_bits;static int dib_w,dib_h;
static HFONT font,font_small;
static void paint_box(int w,int h){
    RECT r={0,0,w,h};HBRUSH b=CreateSolidBrush(RGB(20,12,14));FillRect(dib_dc,&r,b);DeleteObject(b);
    b=CreateSolidBrush(RGB(170,40,48));RECT e={0,h-2>0?h-2:0,w,h};FillRect(dib_dc,&e,b);RECT l={0,0,4,h};FillRect(dib_dc,&l,b);DeleteObject(b);
    SetBkMode(dib_dc,TRANSPARENT);
    int pad=h/3,hint_w=w*28/100;
    SelectObject(dib_dc,font_small);SetTextColor(dib_dc,RGB(170,170,170));
    RECT hr={w-hint_w,0,w-pad,h};
    DrawTextW(dib_dc,hint,-1,&hr,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    WCHAR s[MAX_CHARS+4];EnterCriticalSection(&lock);swprintf(s,MAX_CHARS+4,L"%ls|",typed);LeaveCriticalSection(&lock);
    SelectObject(dib_dc,font);SetTextColor(dib_dc,RGB(255,255,255));
    int room=w-hint_w-2*pad;const WCHAR *p=s;SIZE z;
    while(*p && GetTextExtentPoint32W(dib_dc,p,lstrlenW(p),&z) && z.cx>room)p++;
    RECT tr={pad,0,pad+room,h};
    DrawTextW(dib_dc,p,-1,&tr,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
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
    void **vt=*(void***)dev;DWORD old;
    for(LONG i=0;i<dev_hook_count;i++)if(dev_hooks[i].vt==vt)return;
    if(dev_hook_count>=4 || vt[17]==(void*)present_hook)return;
    if(VirtualProtect(&vt[17],4,PAGE_EXECUTE_READWRITE,&old)) {
        dev_hooks[dev_hook_count].vt=vt;dev_hooks[dev_hook_count].present=(Present_t)vt[17];
        InterlockedIncrement(&dev_hook_count);
        vt[17]=(void*)present_hook;VirtualProtect(&vt[17],4,old,&old);
        JOURNAL("affichage",1);
    }
}
static HRESULT WINAPI createdevice_hook(IDirect3D9 *d3d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS *pp,IDirect3DDevice9 **out){
    HRESULT hr=createdevice_original(d3d,a,t,w,f,pp,out);
    if(SUCCEEDED(hr) && out && *out && t==D3DDEVTYPE_HAL){hook_device(*out);hook_messages(pp && pp->hDeviceWindow?pp->hDeviceWindow:w);}
    return hr;
}
static void install_d3d_hooks(void){
    IDirect3D9 *d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){JOURNAL("affichage",0);return;}
    void **vt=*(void***)d3d;DWORD old;
    if(VirtualProtect(&vt[16],4,PAGE_EXECUTE_READWRITE,&old)){
        createdevice_original=(CreateDevice_t)vt[16];vt[16]=(void*)createdevice_hook;VirtualProtect(&vt[16],4,old,&old);
    }
    IDirect3D9_Release(d3d);
}

void *lc_chat_original,*lc_lobby_original;
__attribute__((used)) void __cdecl lc_chat_seen(void){chat_tick=GetTickCount();}
__attribute__((used)) void __cdecl lc_lobby_seen(BYTE *l){lobby_obj=l;lobby_tick=GetTickCount();}
__attribute__((naked)) static void chat_update_hook(void){
    __asm__ volatile("pushal\n pushfl\n call _lc_chat_seen\n popfl\n popal\n jmp *_lc_chat_original\n");
}
__attribute__((naked)) static void lobby_update_hook(void){
    __asm__ volatile("pushal\n pushfl\n push %ecx\n call _lc_lobby_seen\n add $4,%esp\n popfl\n popal\n jmp *_lc_lobby_original\n");
}

static DWORD WINAPI worker(void *u){
    (void)u;
    install_d3d_hooks();
    void **cs=(void**)RVA(RVA_CHAT_VT+33*4),**ls=(void**)RVA(RVA_LOBBY_VT+33*4);
    for(int n=0;n<600 && *cs!=(void*)RVA(RVA_CHAT_UPDATE);n++)Sleep(100);

    BOOL ok=patch_ptr(cs,RVA(RVA_CHAT_UPDATE),(void*)chat_update_hook,&lc_chat_original);
    if(ok && !patch_ptr(ls,RVA(RVA_LOBBY_UPDATE),(void*)lobby_update_hook,&lc_lobby_original)){
        patch_ptr(cs,(void*)chat_update_hook,lc_chat_original,NULL);ok=FALSE;}
    JOURNAL("menus (1=ok, 0=deja pris ou game.exe inattendu)",ok);
    if(!ok)return 0;
    JOURNAL("format_lignes",patch_format());
    HMODULE sa=NULL;for(int t=0;t<1200 && !sa;t++){sa=GetModuleHandleA("steam_api.dll");if(!sa)Sleep(100);}
    void(*reg)(CB*,int)=sa?(void(*)(CB*,int))GetProcAddress(sa,"SteamAPI_RegisterCallback"):NULL;
    if(reg)reg(&cb,507);
    JOURNAL("reception_steam",reg!=NULL);
    hook_dinput();
    return 0;
}
void LobbyChat_Demarrer(void){
    base=(BYTE*)GetModuleHandleW(NULL);
    InitializeCriticalSection(&lock);
    JOURNAL("debut 1.1",0);
    AjouterTic(Tic);
    HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);if(t)CloseHandle(t);
}

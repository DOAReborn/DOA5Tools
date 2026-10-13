#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <d3d9.h>
#include <math.h>
static BYTE *base;
#define VA(v) (base+((v)-0x400000))
#include "commun.h"
#define VERSION_MODULE "1.0"

#define NOTE(libelle,a,b,c) Journal("IntroOutroSkip",libelle,(long)(a),(long)(b),(long)(c))
static BOOL online(void){return *(DWORD*)VA(0x138f208)||*(DWORD*)VA(0x138189c);}
static int opt_intro=1,opt_outro=1,opt_marks=1;

static int skipped_intro=-2,intro=-1;
static DWORD last_script=0xfffffffe,last_phase=0xffffffff;static BOOL was_online=2;
static void __cdecl frame_hook(void){
    BOOL on=online();if(on!=was_online){NOTE("en_ligne",on,0,0);was_online=on;}
    if(!opt_intro){((void(*)(void))VA(0x7f4690))();return;}
    DWORD phase=*(DWORD*)VA(0x10d6d80),script=*(DWORD*)VA(0x113356c);
    if(phase!=last_phase){if(phase==1){intro=-1;skipped_intro=-2;}last_phase=phase;}
    BOOL in_script=script!=0xffffffff && script!=0xfffffffe;
    if(script!=last_script){if(phase==1 && in_script){intro++;}last_script=script;}
    if(phase==1 && in_script && intro>=0 && skipped_intro!=intro){
        DWORD seq=*(DWORD*)VA(0x13ce348),info=seq>0x10000?*(DWORD*)(seq+0x1c):0,who=info>0x10000?*(DWORD*)(info+4):0xffffffff;
        BYTE place=who<4?*(BYTE*)VA(0x14877c0+who):0xff;
        if(place>=4 && place!=0xff && who<2)place=(BYTE)who;
        if(place==0xff && who<4 && !on)
            for(BYTE p=0;p<4;p++)if(*(DWORD*)VA(0x1487900+p*0x2c)&0x10){place=p;break;}
        if(place<4 && (*(DWORD*)VA(0x1487900+place*0x2c)&0x10)){
            *(BYTE*)VA(0x148864b)=1;skipped_intro=intro;
            NOTE("intro_passee",intro+1,(long)who+1,place);
        }
    }
    ((void(*)(void))VA(0x7f4690))();
}

static BOOL install_intro(void){
    BYTE *site=VA(0x7f6382);
    for(int n=0;n<600 && !(site[0]==0xe8 && site+5+*(int*)(site+1)==VA(0x7f4690));n++)Sleep(100);
    if(!(site[0]==0xe8 && site+5+*(int*)(site+1)==VA(0x7f4690))){NOTE("intro_refus_version",0,0,0);return FALSE;}
    DWORD old;VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old);
    *(int*)(site+1)=(int)((BYTE*)frame_hook-(site+5));
    VirtualProtect(site,5,old,&old);FlushInstructionCache(GetCurrentProcess(),site,5);
    return TRUE;
}

static volatile LONG menu_on;
void *playwin_update;
static int done;static DWORD prev[2];static BYTE *volatile suppress_obj;

static int ko_delay=1500;static DWORD after_t0,after_last,after_prev[2];static volatile LONG pending;
void *after_update[2];
__attribute__((used)) void __cdecl after_note(void){
    DWORD now=GetTickCount();
    if(!after_last||now-after_last>1000){after_t0=now;pending=0;after_prev[0]=after_prev[1]=0xffffffff;}
    after_last=now;
    if(ko_delay<0||now-after_t0<(DWORD)ko_delay)return;
    for(int p=0;p<2;p++){DWORD v=*(DWORD*)VA(0x1487900+p*0x2c)&0x110;
        if(v&&after_prev[p]!=0xffffffff&&!pending){pending=1;NOTE("ko_appui_garde",p+1,(v&0x100)!=0,0);}
        after_prev[p]=v;}
}
__attribute__((naked)) static void after_hook0(void){
    __asm__ volatile("pushal\n pushfl\n call _after_note\n popfl\n popal\n jmp *_after_update\n");
}
__attribute__((naked)) static void after_hook1(void){
    __asm__ volatile("pushal\n pushfl\n call _after_note\n popfl\n popal\n jmp *_after_update+4\n");
}
__attribute__((used)) void __cdecl playwin_note(BYTE *self){
    for(int p=0;p<2;p++){
        DWORD v=*(DWORD*)VA(0x1487900+p*0x2c)&0x110;
        if(p==0 && pending && prev[0]!=0xffffffff && !done && GetTickCount()-after_last<1000){pending=0;v=0x10;
            NOTE("ko_appui_applique",0,0,0);}
        if(v && prev[p]!=0xffffffff && !done){
            done=1;
            NOTE("pose_passee",p+1,(v&0x100)!=0,online());
            suppress_obj=self;
            typedef void(__attribute__((thiscall))*Next_t)(void*);
            ((Next_t)VA(0x814670))(self);
        }
        prev[p]=v;
    }
}
__attribute__((naked)) static void playwin_hook(void){
    __asm__ volatile("pushal\n pushfl\n push %ecx\n call _playwin_note\n add $4,%esp\n popfl\n popal\n jmp *_playwin_update\n");
}

BYTE *next_tramp;
__attribute__((used)) int __cdecl next_note(BYTE *self,DWORD ret){
    if(suppress_obj==self && ret==(DWORD)VA(0x814783)){suppress_obj=NULL;
        NOTE("etape_suivante_ignoree",0,0,0);return 1;}
    BYTE *head=*(BYTE**)(self+0x5c),*n=head?*(BYTE**)head:NULL;
    DWORD f=(n && n!=head)?*(DWORD*)(n+8)-(DWORD)base+0x400000:0;
    if(f==0x814d80){done=0;prev[0]=prev[1]=0xffffffff;suppress_obj=NULL;NOTE("pose_victoire",0,0,0);}
    if(f==0x815020){InterlockedExchange(&menu_on,1);NOTE("menu_fin_affiche",0,0,0);}
    if(f==0x815130||f==0){if(InterlockedExchange(&menu_on,0))NOTE("menu_fin_ferme",0,0,0);}
    return 0;
}
__attribute__((naked)) static void next_hook(void){
    __asm__ volatile("pushal\n pushfl\n push 36(%esp)\n push %ecx\n call _next_note\n add $8,%esp\n test %eax,%eax\n jnz 1f\n"
                     "popfl\n popal\n jmp *_next_tramp\n"
                     "1: popfl\n popal\n mov $1,%al\n ret\n");
}
static void hook_next(void){
    static const BYTE pro[6]={0x55,0x8b,0xec,0x83,0xec,0x0c};
    BYTE *t=VA(0x814670);
    if(memcmp(t,pro,6)){NOTE("etape_suivante_inattendue",0,0,0);return;}
    BYTE *tr=VirtualAlloc(NULL,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    memcpy(tr,pro,6);tr[6]=0xe9;*(DWORD*)(tr+7)=(DWORD)(t+6)-(DWORD)(tr+11);next_tramp=tr;DWORD old;
    VirtualProtect(t,6,PAGE_EXECUTE_READWRITE,&old);t[0]=0xe9;*(DWORD*)(t+1)=(DWORD)next_hook-(DWORD)(t+5);t[5]=0x90;
    VirtualProtect(t,6,old,&old);FlushInstructionCache(GetCurrentProcess(),t,6);
}

static int row0_y=410,row_pitch=48,text_h=20,center_x=960;
static int half_w[8]={104,171,39,108,162,142,120,120};
static DWORD mark_t0[2];static int mark_row[2]={-1,-1};static DWORD both_t;
static void read_choices(int *row,int *chose){
    for(int i=0;i<2;i++){BYTE *a=VA(0x137a1a0+0x78+i*2);row[i]=a[0];chose[i]=a[1]!=0;}
}

static void poly_part(HDC dc,const POINT *pt,int n,float f){
    float tot=0,len[8];for(int i=1;i<n;i++){float dx=(float)(pt[i].x-pt[i-1].x),dy=(float)(pt[i].y-pt[i-1].y);len[i]=sqrtf(dx*dx+dy*dy);tot+=len[i];}
    float want=tot*f;MoveToEx(dc,pt[0].x,pt[0].y,NULL);
    for(int i=1;i<n && want>0;i++){if(want>=len[i]){LineTo(dc,pt[i].x,pt[i].y);want-=len[i];}
        else{float r=want/len[i];LineTo(dc,pt[i-1].x+(int)((pt[i].x-pt[i-1].x)*r),pt[i-1].y+(int)((pt[i].y-pt[i-1].y)*r));want=0;}}
}
#define AREA_X 520
#define AREA_Y 360
#define AREA_W 880
#define AREA_H 360
static IDirect3DTexture9 *mk_tex;static int mk_w,mk_h;static HDC mk_dc;static HBITMAP mk_bmp;static DWORD *mk_bits;static HFONT mk_font;
static BOOL paint_marks(IDirect3DDevice9 *dev,int w,int h,float s){
    if(w!=mk_w||h!=mk_h){
        if(mk_bmp)DeleteObject(mk_bmp);
        if(!mk_dc)mk_dc=CreateCompatibleDC(NULL);
        BITMAPINFO bi={{sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB}};
        mk_bmp=CreateDIBSection(mk_dc,&bi,DIB_RGB_COLORS,(void**)&mk_bits,NULL,0);if(!mk_bmp)return FALSE;
        SelectObject(mk_dc,mk_bmp);
        if(mk_font)DeleteObject(mk_font);
        mk_font=CreateFontW(-(int)(text_h*s),0,0,0,FW_BOLD,TRUE,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
        if(mk_tex){IDirect3DTexture9_Release(mk_tex);mk_tex=NULL;}
        if(FAILED(IDirect3DDevice9_CreateTexture(dev,w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&mk_tex,NULL)))return FALSE;
        mk_w=w;mk_h=h;
    }
    memset(mk_bits,0,(size_t)w*h*4);
    SetBkMode(mk_dc,TRANSPARENT);SelectObject(mk_dc,mk_font);
    DWORD now=GetTickCount();
    for(int i=0;i<2;i++){
        int r=mark_row[i];if(r<0||r>7)continue;
        float f=(now-mark_t0[i])/250.0f;if(f>1)f=1;if(f<0)f=0;
        float yc=(row0_y+r*row_pitch-AREA_Y)*s,hw=half_w[r]*s,xc=(center_x-AREA_X)*s,g=4*s,d=text_h*0.5f*s+g,sh=22*s;
        float yt=yc-text_h*0.5f*s-g,yb=yc+text_h*0.5f*s+g;
        COLORREF col=i==0?RGB(40,150,255):RGB(240,40,40);
        HPEN pen=CreatePen(PS_SOLID,(int)(3*s+0.5f)<2?2:(int)(3*s+0.5f),col);HGDIOBJ op=SelectObject(mk_dc,pen);
        POINT pt[4];
        if(i==0){pt[0].x=(int)(xc+hw+g);pt[0].y=(int)yt;pt[1].x=(int)(xc-hw-g);pt[1].y=(int)yt;
                 pt[2].x=(int)(xc-hw-g-d);pt[2].y=(int)yc;pt[3].x=(int)(xc-hw-g-d-sh);pt[3].y=(int)yc;}
        else    {pt[0].x=(int)(xc-hw-g);pt[0].y=(int)yb;pt[1].x=(int)(xc+hw+g);pt[1].y=(int)yb;
                 pt[2].x=(int)(xc+hw+g+d);pt[2].y=(int)yc;pt[3].x=(int)(xc+hw+g+d+sh);pt[3].y=(int)yc;}
        poly_part(mk_dc,pt,4,f);
        SelectObject(mk_dc,op);DeleteObject(pen);
        if(f>=1){SetTextColor(mk_dc,col);RECT rc;
            if(i==0){rc.right=pt[3].x-(int)(8*s);rc.left=rc.right-(int)(60*s);rc.top=(int)(yc-text_h*s);rc.bottom=(int)(yc+text_h*s);
                DrawTextW(mk_dc,L"P1",-1,&rc,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP);}
            else{rc.left=pt[3].x+(int)(4*s);rc.right=rc.left+(int)(60*s);rc.top=(int)(yc-text_h*s);rc.bottom=(int)(yc+text_h*s);
                DrawTextW(mk_dc,L"P2",-1,&rc,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP);}}
    }
    GdiFlush();
    DWORD a=255;if(both_t){DWORD e=now-both_t;a=e<150?255:e>450?0:(DWORD)(255*(450-e)/300);}
    D3DLOCKED_RECT lr;if(FAILED(IDirect3DTexture9_LockRect(mk_tex,0,&lr,NULL,0)))return FALSE;
    for(int y=0;y<h;y++){DWORD *src=mk_bits+y*w,*dst=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(int x=0;x<w;x++){DWORD c=src[x]&0xffffff;dst[x]=c?(c|(a<<24)):0;}}
    IDirect3DTexture9_UnlockRect(mk_tex,0);
    return TRUE;
}
static void draw_marks(IDirect3DDevice9 *dev){
    IDirect3DSurface9 *bb=NULL;
    if(FAILED(IDirect3DDevice9_GetBackBuffer(dev,0,0,D3DBACKBUFFER_TYPE_MONO,&bb))||!bb)return;
    D3DSURFACE_DESC sd;IDirect3DSurface9_GetDesc(bb,&sd);
    float gh=(float)sd.Height,gw=gh*16/9,gx=0,gy=0;
    if(gw>sd.Width){gw=(float)sd.Width;gh=gw*9/16;gy=(sd.Height-gh)/2;}else gx=(sd.Width-gw)/2;
    float s=gh/1080;int w=(int)(AREA_W*s),h=(int)(AREA_H*s);
    if(w<16||h<16||!paint_marks(dev,w,h,s)){IDirect3DSurface9_Release(bb);return;}
    IDirect3DStateBlock9 *sb=NULL;if(FAILED(IDirect3DDevice9_CreateStateBlock(dev,D3DSBT_ALL,&sb))){IDirect3DSurface9_Release(bb);return;}
    IDirect3DSurface9 *rt=NULL;IDirect3DDevice9_GetRenderTarget(dev,0,&rt);IDirect3DDevice9_SetRenderTarget(dev,0,bb);
    D3DVIEWPORT9 vp={0,0,sd.Width,sd.Height,0,1};IDirect3DDevice9_SetViewport(dev,&vp);
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_SetVertexShader(dev,NULL);IDirect3DDevice9_SetPixelShader(dev,NULL);
    IDirect3DDevice9_SetFVF(dev,D3DFVF_XYZRHW|D3DFVF_TEX1);
    IDirect3DDevice9_SetTexture(dev,0,(IDirect3DBaseTexture9*)mk_tex);for(int i=1;i<8;i++)IDirect3DDevice9_SetTexture(dev,i,NULL);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_TEXCOORDINDEX,0);IDirect3DDevice9_SetTextureStageState(dev,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev,1,D3DTSS_COLOROP,D3DTOP_DISABLE);IDirect3DDevice9_SetTextureStageState(dev,1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MINFILTER,D3DTEXF_POINT);IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);IDirect3DDevice9_SetSamplerState(dev,0,D3DSAMP_SRGBTEXTURE,0);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_SRGBWRITEENABLE,FALSE);IDirect3DDevice9_SetRenderState(dev,D3DRS_ALPHABLENDENABLE,TRUE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);IDirect3DDevice9_SetRenderState(dev,D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_BLENDOP,D3DBLENDOP_ADD);IDirect3DDevice9_SetRenderState(dev,D3DRS_ALPHATESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_ZENABLE,D3DZB_FALSE);IDirect3DDevice9_SetRenderState(dev,D3DRS_ZWRITEENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_STENCILENABLE,FALSE);IDirect3DDevice9_SetRenderState(dev,D3DRS_SCISSORTESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_CULLMODE,D3DCULL_NONE);IDirect3DDevice9_SetRenderState(dev,D3DRS_FOGENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(dev,D3DRS_LIGHTING,FALSE);IDirect3DDevice9_SetRenderState(dev,D3DRS_COLORWRITEENABLE,0xf);
    float x0=(float)(int)(gx+AREA_X*s)-0.5f,y0=(float)(int)(gy+AREA_Y*s)-0.5f,x1=x0+w,y1=y0+h;
    struct {float x,y,z,w,u,v;} q[4]={{x0,y0,0,1,0,0},{x1,y0,0,1,1,0},{x0,y1,0,1,0,1},{x1,y1,0,1,1,1}};
    IDirect3DDevice9_DrawPrimitiveUP(dev,D3DPT_TRIANGLESTRIP,2,q,sizeof(q[0]));
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_SetRenderTarget(dev,0,rt);if(rt)IDirect3DSurface9_Release(rt);
    IDirect3DStateBlock9_Apply(sb);IDirect3DStateBlock9_Release(sb);IDirect3DSurface9_Release(bb);
}
static void update_marks(void){
    int row[2],chose[2];read_choices(row,chose);
    static int lr[2]={-2,-2},lc[2]={-2,-2};
    for(int i=0;i<2;i++){
        if(menu_on&&chose[i]&&(row[i]!=lr[i]||chose[i]!=lc[i]))NOTE("choix",i+1,row[i]+1,0);
        lr[i]=row[i];lc[i]=chose[i];
        int want=(menu_on && chose[i])?row[i]:-1;
        if(want!=mark_row[i]){mark_row[i]=want;mark_t0[i]=GetTickCount();}
    }
    if(menu_on&&chose[0]&&chose[1]){if(!both_t)both_t=GetTickCount()|1;}else both_t=0;
}
typedef HRESULT(WINAPI *Present_t)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
typedef HRESULT(WINAPI *CreateDevice_t)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
static void **dev_vt;static Present_t present_orig;static CreateDevice_t createdevice_orig;
static HRESULT WINAPI present_hook(IDirect3DDevice9 *d,const RECT *a,const RECT *b,HWND w,const RGNDATA *r){
    if(opt_marks){update_marks();if(mark_row[0]>=0||mark_row[1]>=0)draw_marks(d);}
    return present_orig(d,a,b,w,r);
}
static HRESULT WINAPI createdevice_hook(IDirect3D9 *d3d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS *pp,IDirect3DDevice9 **out_){
    HRESULT hr=createdevice_orig(d3d,a,t,w,f,pp,out_);
    if(SUCCEEDED(hr)&&out_&&*out_&&t==D3DDEVTYPE_HAL&&!dev_vt){void **vt=*(void***)*out_;DWORD old;
        if(VirtualProtect(&vt[17],4,PAGE_EXECUTE_READWRITE,&old)){dev_vt=vt;present_orig=(Present_t)vt[17];vt[17]=(void*)present_hook;VirtualProtect(&vt[17],4,old,&old);NOTE("affichage_accroche",1,0,0);}}
    return hr;
}
static void install_d3d(void){
    void **vt=D3D9_Methodes();if(!vt){NOTE("d3d9_indisponible",0,0,0);return;}
    DWORD old;
    if(VirtualProtect(&vt[16],4,PAGE_EXECUTE_READWRITE,&old))
        for(;;){void *cur=vt[16];createdevice_orig=(CreateDevice_t)cur;if(InterlockedCompareExchangePointer(&vt[16],(void*)createdevice_hook,cur)==cur)break;}
}
static BOOL install_outro(void){
    void **slot=(void**)VA(0xdc0f68+3*4);DWORD old;
    for(int n=0;n<600 && *slot!=(void*)VA(0x817840);n++)Sleep(100);
    if(*slot!=(void*)VA(0x817840)){NOTE("outro_refus_version",0,0,0);return FALSE;}
    VirtualProtect(slot,4,PAGE_READWRITE,&old);playwin_update=*slot;*slot=(void*)playwin_hook;VirtualProtect(slot,4,old,&old);
    hook_next();
    static const DWORD vt[2]={0xdc086c,0xdc07f0},fn[2]={0x80d690,0x80cf50};void *hk[2]={(void*)after_hook0,(void*)after_hook1};
    for(int i=0;i<2&&ko_delay>=0;i++){void **sl=(void**)VA(vt[i]+3*4);
        if(*sl!=(void*)VA(fn[i])){NOTE("ko_refus_version",i,0,0);continue;}
        VirtualProtect(sl,4,PAGE_READWRITE,&old);after_update[i]=*sl;*sl=hk[i];VirtualProtect(sl,4,old,&old);}
    return TRUE;
}
static DWORD WINAPI worker(void *u){
    (void)u;
    if(opt_outro&&opt_marks)install_d3d();
    BOOL in=opt_intro&&install_intro(),ou=opt_outro&&install_outro();
    NOTE("pret intro/outro/marques",in,ou,opt_marks&&ou);
    return 0;
}
void IntroOutroSkip_Demarrer(void){
    if(GetModuleHandleA("DOA5LR-IntroOutroSkip.asi")){NOTE("module_separe_present",0,0,0);return;}
    base=(BYTE*)GetModuleHandleW(NULL);
    opt_intro=Reglage("IntroOutroSkip","Intro",1);opt_outro=Reglage("IntroOutroSkip","Outro",1);
    opt_marks=Reglage("IntroOutroSkip","Marks",1);ko_delay=Reglage("IntroOutroSkip","KODelay",ko_delay);
    NOTE("debut " VERSION_MODULE,0,0,0);
    HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);if(t)CloseHandle(t);
}

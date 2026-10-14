/*
  PhysicsSim 3D  -  20BSPH101 Engineering Physics
  Native OpenGL  |  GLFW  |  WGL bitmap fonts  |  MinGW-w64 -> Windows x64

  BUILD COMMAND (run from project root on Linux):
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -mwindows \
      -I libs/include -I /usr/x86_64-w64-mingw32/include \
      src/main.cpp \
      -L libs/win64 -L /usr/x86_64-w64-mingw32/lib \
      -lglfw3dll -lopengl32 -lglu32 -lgdi32 -luser32 -lkernel32 \
      -o build/PhysicsSim3D.exe

  KEY DESIGN: Every scene is a PURE DRAW FUNCTION.
  It reads g_vals + g_time each frame and draws directly.
  No object lists. No rebuild on slider change.
  Sliders write into g_vals[sym]. The next frame picks it up.
  Animation NEVER stops when you move sliders.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ─────────────────────────────────────────────────────────
   LAYOUT
───────────────────────────────────────────────────────── */
static int WIN_W      = 1280;
static int WIN_H      = 800;
static const int SB_W = 290;   // sidebar width
static const int TB_H = 36;    // topbar height
static const int CT_H = 160;   // controls panel height
#define CANVAS_X  SB_W
#define CANVAS_W  (WIN_W - SB_W)
#define CANVAS_H  (WIN_H - TB_H - CT_H)

/* ─────────────────────────────────────────────────────────
   COLOURS
───────────────────────────────────────────────────────── */
struct C3 { float r, g, b; };
#define MKRGB(r,g,b) C3{(r)/255.f,(g)/255.f,(b)/255.f}

static C3 BG0     = MKRGB(4,   4,  13);
static C3 BG1     = MKRGB(7,   7,  26);
static C3 BG2     = MKRGB(12, 12,  34);
static C3 BG3     = MKRGB(17, 17,  48);
static C3 ACCENT  = MKRGB(108, 92, 231);
static C3 ACCENT2 = MKRGB(162,155, 254);
static C3 ACCENT3 = MKRGB(0,  206, 201);
static C3 HOT     = MKRGB(253,121, 168);
static C3 TEXT0   = MKRGB(240,240, 255);
static C3 TEXT1   = MKRGB(160,160, 204);
static C3 TEXT2   = MKRGB(80,  80, 128);
static C3 BORDER  = MKRGB(40,  40, 100);
static C3 CBLUE   = MKRGB(68, 136, 255);
static C3 CORANGE = MKRGB(255,102,  68);
static C3 CGREEN  = MKRGB(68, 255, 136);
static C3 CYELLOW = MKRGB(255,204,   0);
static C3 CRED    = MKRGB(255, 51,  51);
static C3 CCYAN   = MKRGB(0,  200, 255);
static C3 CGRID   = MKRGB(20,  20,  55);

inline void gc(C3 c, float a=1.f){ glColor4f(c.r,c.g,c.b,a); }

/* ─────────────────────────────────────────────────────────
   GLOBAL STATE  (scenes read these, never write them)
───────────────────────────────────────────────────────── */
static double g_time     = 0.0;                    // always ticking
static std::map<std::string,double> g_vals;         // current slider values
static int    g_selIdx   = 0;                       // -1 = custom equation
static double g_camTheta = 0.6, g_camPhi = 0.4, g_camDist = 11.0;
static bool   g_drag     = false;
static double g_lmx = 0, g_lmy = 0;
static GLUquadric* g_q   = nullptr;

/* ─────────────────────────────────────────────────────────
   DATA TYPES
───────────────────────────────────────────────────────── */
struct Var {
    std::string sym, name, unit;
    double mn, mx, val, step;
};

struct Equation {
    std::string id, co, name, formula, result, unit, viz;
    std::vector<Var> vars;
    std::function<double(std::map<std::string,double>&)> calc;
    bool isCustom = false;
};

static std::vector<Equation> g_eqs;
static Equation               g_custom;
static bool                   g_hasCustom = false;

Equation& curEq(){
    return (g_selIdx == -1 && g_hasCustom) ? g_custom : g_eqs[g_selIdx];
}

void loadEq(int i){
    g_selIdx = i;
    g_vals.clear();
    for(auto& v : curEq().vars) g_vals[v.sym] = v.val;
}

/* ─────────────────────────────────────────────────────────
   FORMAT HELPERS
───────────────────────────────────────────────────────── */
std::string fmt(double n){
    if(std::isnan(n) || std::isinf(n)) return "N/A";
    char b[64];
    if(fabs(n) >= 1e9 || (fabs(n) < 0.0001 && n != 0))
        snprintf(b, 64, "%.3e", n);
    else if(fabs(n) >= 100)
        snprintf(b, 64, "%.2f", n);
    else
        snprintf(b, 64, "%.5g", n);
    return b;
}

std::string clip(const std::string& s, int n){
    return s.size() <= (size_t)n ? s : s.substr(0, n-2) + "..";
}

/* ─────────────────────────────────────────────────────────
   WGL BITMAP FONT SYSTEM  (zero external dependencies)
───────────────────────────────────────────────────────── */
static GLuint g_fontBase[4] = {0,0,0,0};

void initFonts(HDC hdc){
    int heights[] = {-10, -12, -18, -24};
    const char* face = "Courier New";
    for(int i = 0; i < 4; i++){
        g_fontBase[i] = glGenLists(96);
        HFONT hf = CreateFontA(heights[i], 0, 0, 0,
            i >= 2 ? FW_BOLD : FW_NORMAL,
            0, 0, 0, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FF_DONTCARE|DEFAULT_PITCH, face);
        HFONT old = (HFONT)SelectObject(hdc, hf);
        wglUseFontBitmaps(hdc, 32, 96, g_fontBase[i]);
        SelectObject(hdc, old);
        DeleteObject(hf);
    }
}

int fontIdx(int sz){
    if(sz <= 10) return 0;
    if(sz <= 12) return 1;
    if(sz <= 18) return 2;
    return 3;
}

void drawChar(int sz, char c){
    if(c < 32 || c > 127) return;
    int idx = fontIdx(sz);
    glListBase(g_fontBase[idx] - 32);
    glCallLists(1, GL_UNSIGNED_BYTE, (const GLubyte*)&c);
}

void txt(float x, float y, const std::string& s, int sz, C3 col, float a=1.f){
    gc(col, a);
    glRasterPos2f(x, y);
    for(char c : s) drawChar(sz, c);
}

/* ─────────────────────────────────────────────────────────
   GL DRAW HELPERS  (immediate mode, max compatibility)
───────────────────────────────────────────────────────── */
void drawSphere(float r, int sl=16){ gluSphere(g_q, r, sl, sl); }

void drawCyl(float rb, float rt, float h, int sl=20){
    gluCylinder(g_q, rb, rt, h, sl, 1);
}

void drawBox(float w, float h, float d){
    float hw=w/2, hh=h/2, hd=d/2;
    float n[6][3] = {{0,0,1},{0,0,-1},{0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
    float v[24][3] = {
        {-hw,-hh,hd},{hw,-hh,hd},{hw,hh,hd},{-hw,hh,hd},
        {-hw,-hh,-hd},{-hw,hh,-hd},{hw,hh,-hd},{hw,-hh,-hd},
        {-hw,hh,-hd},{-hw,hh,hd},{hw,hh,hd},{hw,hh,-hd},
        {-hw,-hh,-hd},{hw,-hh,-hd},{hw,-hh,hd},{-hw,-hh,hd},
        {hw,-hh,-hd},{hw,hh,-hd},{hw,hh,hd},{hw,-hh,hd},
        {-hw,-hh,-hd},{-hw,-hh,hd},{-hw,hh,hd},{-hw,hh,-hd}
    };
    int fi[6][4] = {{0,1,2,3},{4,5,6,7},{8,9,10,11},{12,13,14,15},{16,17,18,19},{20,21,22,23}};
    glBegin(GL_QUADS);
    for(int f=0; f<6; f++){
        glNormal3fv(n[f]);
        for(int j=0; j<4; j++) glVertex3fv(v[fi[f][j]]);
    }
    glEnd();
}

void drawTorus(float R, float r, int ns=36, int nr=14){
    for(int i=0; i<ns; i++){
        float a1=2*(float)M_PI*i/ns, a2=2*(float)M_PI*(i+1)/ns;
        glBegin(GL_QUAD_STRIP);
        for(int j=0; j<=nr; j++){
            float b=2*(float)M_PI*j/nr, cb=cosf(b), sb=sinf(b);
            for(float a : {a1, a2}){
                float ca=cosf(a), sa=sinf(a);
                glNormal3f(cb*ca, sb, cb*sa);
                glVertex3f((R+r*cb)*ca, r*sb, (R+r*cb)*sa);
            }
        }
        glEnd();
    }
}

void arrow(float ox, float oy, float oz,
           float dx, float dy, float dz,
           float len, C3 col)
{
    float mag = sqrtf(dx*dx+dy*dy+dz*dz);
    if(mag < 1e-9f) return;
    dx/=mag; dy/=mag; dz/=mag;
    float ex=ox+dx*len, ey=oy+dy*len, ez=oz+dz*len;
    gc(col);
    glLineWidth(2.f);
    glBegin(GL_LINES); glVertex3f(ox,oy,oz); glVertex3f(ex,ey,ez); glEnd();
    glLineWidth(1.f);
    glPushMatrix();
    glTranslatef(ex, ey, ez);
    float ax=1*dz-0*dy, ay=0*dx-1*dz, az=1*dy-0*dx;
    float am = sqrtf(ax*ax+ay*ay+az*az);
    float ang = acosf(std::max(-1.f,std::min(1.f,dy)))*180.f/(float)M_PI;
    if(am > 0.001f) glRotatef(ang, ax/am, ay/am, az/am);
    else if(dy < 0) glRotatef(180, 1, 0, 0);
    drawCyl(len*0.13f, 0, len*0.28f, 12);
    glPopMatrix();
}

void drawGrid(){
    glDisable(GL_LIGHTING);
    gc(CGRID, 0.5f);
    glLineWidth(0.5f);
    glBegin(GL_LINES);
    for(float i=-12; i<=12; i+=1.5f){
        glVertex3f(i,-3,-12); glVertex3f(i,-3,12);
        glVertex3f(-12,-3,i); glVertex3f(12,-3,i);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
}

void drawLatticeEdges(float a){
    float h=a/2;
    float pts[][3] = {
        {-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},{-h,-h,-h},
        {-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h},{-h,-h,h},
        {h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h},{-h,h,h},{-h,h,-h}
    };
    glDisable(GL_LIGHTING);
    gc(BORDER, 0.8f);
    glLineWidth(1.f);
    glBegin(GL_LINE_STRIP);
    for(auto& p : pts) glVertex3fv(p);
    glEnd();
    glEnable(GL_LIGHTING);
}

void sineLine(float x0, float x1, float amp, float freq, float phase, float z, C3 col, float lw=2.f){
    glDisable(GL_LIGHTING);
    gc(col);
    glLineWidth(lw);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=200; i++){
        float x = x0 + (x1-x0)*i/200;
        glVertex3f(x, amp*sinf(freq*x+phase), z);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
}

/* ─────────────────────────────────────────────────────────
   SCENE FUNCTIONS  — pure, stateless, called every frame
   g_vals  = current slider values (always up-to-date)
   g_time  = always-ticking clock
   Changing a slider just writes g_vals[sym].
   The scene reads it next frame. Animation NEVER stops.
───────────────────────────────────────────────────────── */
double V(const char* s, double def=1){ return g_vals.count(s) ? g_vals[s] : def; }
float  T(){ return (float)g_time; }

void sceneFCC(){
    double r=V("r",1);
    float a=(float)(2*sqrt(2.)*r*0.5), h=a/2, sr=(float)(r*0.115);
    glRotatef(T()*16, 1,1,0);
    gc(CBLUE);
    float c8[8][3] = {{-h,-h,-h},{h,-h,-h},{-h,h,-h},{h,h,-h},
                      {-h,-h,h},{h,-h,h},{-h,h,h},{h,h,h}};
    for(auto& p : c8){
        glPushMatrix(); glTranslatef(p[0],p[1],p[2]); drawSphere(sr); glPopMatrix();
    }
    gc(CORANGE);
    float fc[6][3] = {{0,0,-h},{0,0,h},{-h,0,0},{h,0,0},{0,-h,0},{0,h,0}};
    for(auto& p : fc){
        glPushMatrix(); glTranslatef(p[0],p[1],p[2]); drawSphere(sr*0.88f); glPopMatrix();
    }
    drawLatticeEdges(a);
}

void sceneBCC(){
    double r=V("r",1);
    float a=(float)(4*r/sqrt(3.)*0.5), h=a/2, sr=(float)(r*0.11);
    glRotatef(T()*16, 1,1,0);
    gc(CBLUE);
    float c8[8][3] = {{-h,-h,-h},{h,-h,-h},{-h,h,-h},{h,h,-h},
                      {-h,-h,h},{h,-h,h},{-h,h,h},{h,h,h}};
    for(auto& p : c8){
        glPushMatrix(); glTranslatef(p[0],p[1],p[2]); drawSphere(sr); glPopMatrix();
    }
    gc(CORANGE);
    glPushMatrix(); drawSphere(sr*1.15f); glPopMatrix();
    drawLatticeEdges(a);
}

void sceneLattice(){
    double h=V("h",1), k=V("k",1), l=V("l",0);
    float a=1.4f;
    glRotatef(T()*10, 0,1,0);
    gc(CBLUE, 0.85f);
    for(int x=0; x<4; x++) for(int y=0; y<4; y++) for(int z=0; z<4; z++){
        glPushMatrix();
        glTranslatef((x-1.5f)*a, (y-1.5f)*a, (z-1.5f)*a);
        drawSphere(0.1f);
        glPopMatrix();
    }
    arrow(0,0,0, (float)h,(float)k,(float)l, 2.2f, HOT);
    double mag=sqrt(h*h+k*k+l*l); if(mag<1e-9) mag=1;
    float nx=(float)(h/mag), ny=(float)(k/mag), nz=(float)(l/mag);
    float ux=-ny, uy=nx+nz, uz=-ny;
    float um=sqrtf(ux*ux+uy*uy+uz*uz);
    if(um<1e-9){ux=1;uy=0;uz=0;} else {ux/=um;uy/=um;uz/=um;}
    float vx=ny*uz-nz*uy, vy=nz*ux-nx*uz, vz=nx*uy-ny*ux, s=2.2f;
    glDisable(GL_LIGHTING);
    glColor4f(1,0.27f,0.53f,0.22f);
    glBegin(GL_QUADS);
    glVertex3f( s*ux+ s*vx, s*uy+ s*vy, s*uz+ s*vz);
    glVertex3f(-s*ux+ s*vx,-s*uy+ s*vy,-s*uz+ s*vz);
    glVertex3f(-s*ux- s*vx,-s*uy- s*vy,-s*uz- s*vz);
    glVertex3f( s*ux- s*vx, s*uy- s*vy, s*uz- s*vz);
    glEnd();
    glEnable(GL_LIGHTING);
}

void scenePhoton(){
    double nu=V("nu",5e14);
    float wl=(float)std::max(0.5,std::min(4.0,3e8/nu*3e6));
    C3 cols[3] = {HOT, CBLUE, CYELLOW};
    glDisable(GL_LIGHTING);
    for(int ci=0; ci<3; ci++){
        gc(cols[ci]);
        glLineWidth(2.f);
        glBegin(GL_LINE_STRIP);
        for(int i=0; i<=160; i++){
            float x=i/160.f*14-7;
            float y=sinf(x*(float)M_PI*2/wl*0.7f+T()*4+(float)(ci*2.1))*0.5f;
            glVertex3f(x, y, (float)(ci*0.6-0.6));
        }
        glEnd();
    }
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    gc(CYELLOW);
    glPushMatrix(); glTranslatef(6.5f,0,0); drawSphere(0.35f+0.08f*sinf(T()*12)); glPopMatrix();
}

void sceneLaser(){
    double gv=V("g",1), Lv=V("L",0.5);
    float gain=(float)exp(gv*Lv);
    gc(ACCENT2, 0.9f);
    glPushMatrix(); glTranslatef(-4,0,0); drawBox(0.2f,3,3); glPopMatrix();
    glColor4f(0.7f,0.7f,0.3f,0.65f);
    glPushMatrix(); glTranslatef(4,0,0);  drawBox(0.08f,3,3); glPopMatrix();
    glColor4f(0.2f,1,0.5f,0.1f);
    glPushMatrix(); glRotatef(90,0,0,1); glTranslatef(0,0,-3.8f); drawCyl(0.8f,0.8f,7.6f,28); glPopMatrix();
    float br=std::min(0.55f, 0.04f+gain*0.004f), intensity=0.65f+0.35f*sinf(T()*12);
    glColor4f(1,0.15f,0.05f,intensity);
    glPushMatrix(); glRotatef(90,0,0,1); glTranslatef(0,0,-3.9f); drawCyl(br,br,7.8f,16); glPopMatrix();
    arrow(4.1f,0,0, 1,0,0, 1.8f, CRED);
    glDisable(GL_LIGHTING);
    gc(CRED, 0.3f*sinf(T()*8+1));
    glPushMatrix(); glTranslatef(-3.95f,0,0);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0,0,0);
    for(int i=0; i<=20; i++){float a=2*(float)M_PI*i/20; glVertex3f(0,br*3*cosf(a),br*3*sinf(a));}
    glEnd();
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void sceneFibre(){
    double n1=V("n1",1.5), n2=V("n2",1.46);
    double x2=n1*n1-n2*n2, na=x2>0?sqrt(x2):0, acc=asin(std::min(na,1.0));
    glColor4f(0.13f,0.27f,0.67f,0.10f);
    glPushMatrix(); glRotatef(90,0,1,0); glTranslatef(0,0,-7); drawCyl(1,1,14,32); glPopMatrix();
    glColor4f(0.27f,0.67f,1,0.30f);
    glPushMatrix(); glRotatef(90,0,1,0); glTranslatef(0,0,-7); drawCyl(0.3f,0.3f,14,24); glPopMatrix();
    glDisable(GL_LIGHTING);
    gc(CYELLOW); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=200; i++){
        float xx=i/200.f*14-7;
        glVertex3f(xx, 0.18f*sinf(xx*2.2f+T()*3), 0);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    arrow(-7,0,0, (float)cos(acc),(float)sin(acc),0,  2.5f, CRED);
    arrow(-7,0,0, (float)cos(acc),-(float)sin(acc),0, 2.5f, CORANGE);
    glDisable(GL_LIGHTING);
    gc(ACCENT3, 0.35f); glLineWidth(1.f);
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<=40; i++){float a=2*(float)M_PI*i/40; glVertex3f(-7,(float)(na*sin(a)),(float)(na*cos(a)));}
    glEnd();
    glEnable(GL_LIGHTING);
}

void sceneCantilever(){
    double W=V("W",1), l=V("l",0.5), de=V("delta",0.01);
    float L=(float)(l*9), defl=(float)(de*50);
    gc(BG3, 1);
    glPushMatrix(); glTranslatef(-0.3f,0,0); drawBox(0.5f,3.2f,2.0f); glPopMatrix();
    glDisable(GL_LIGHTING);
    gc(CBLUE); glLineWidth(3.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=60; i++){
        float x=i/60.f*L;
        glVertex3f(x, -defl*powf(x/L,3), 0);
    }
    glEnd();
    gc(ACCENT2, 0.6f); glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=60; i++){
        float x=i/60.f*L;
        glVertex3f(x, -defl*powf(x/L,3)+0.14f, 0);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    arrow(L,-defl*0.3f,0, 0,-1,0, std::min(2.5f,0.25f+(float)W*0.18f), CRED);
}

void sceneTorsion(){
    double I=V("I",0.001), Cv=V("C",0.01);
    double period=2*M_PI*sqrt(I/std::max(Cv,1e-9));
    float angle=(float)(sin(g_time*2*M_PI/std::max(period,0.05))*0.55);
    gc(BG3, 1);
    glPushMatrix(); glTranslatef(0,4.3f,0); drawBox(2.0f,0.3f,2.0f); glPopMatrix();
    gc(TEXT1, 1);
    glPushMatrix(); glTranslatef(0,2.1f,0); glRotatef(90,1,0,0); drawCyl(0.06f,0.06f,4.0f,12); glPopMatrix();
    gc(ACCENT, 0.92f);
    glPushMatrix();
    glRotatef(angle*180.f/(float)M_PI, 0,1,0);
    glRotatef(90,1,0,0);
    drawCyl(2.0f,2.0f,0.22f,48);
    gc(CORANGE, 1);
    glPushMatrix(); glTranslatef( 1.6f,0,0); drawSphere(0.3f); glPopMatrix();
    glPushMatrix(); glTranslatef(-1.6f,0,0); drawSphere(0.3f); glPopMatrix();
    glPopMatrix();
    gc(ACCENT3, 0.5f);
    drawTorus(2.05f, 0.04f, 40, 12);
}

void sceneCompton(){
    double theta=V("theta",90);
    float th=(float)(theta*M_PI/180);
    glDisable(GL_LIGHTING);
    gc(CBLUE); glLineWidth(2.f);
    glBegin(GL_LINES); glVertex3f(-6,0,0); glVertex3f(0,0,0); glEnd();
    glEnable(GL_LIGHTING);
    for(int i=0; i<6; i++){
        float xd=-6.f+(float)fmod(i/6.f+T()*0.5,1.f)*6.f;
        glPushMatrix(); glTranslatef(xd,0.2f,0); gc(CCYAN,0.7f); drawSphere(0.07f); glPopMatrix();
    }
    arrow(0,0,0, cosf(th),sinf(th),0,  5.f, CRED);
    arrow(0,0,0, cosf((float)M_PI-th/2),-sinf((float)M_PI-th/2),0, 3.5f, CGREEN);
    gc(CGREEN, 1);
    glPushMatrix(); drawSphere(0.32f); glPopMatrix();
    float sp=0.8f+0.8f*(float)(1.0-cos(th));
    for(int i=1; i<=3; i++){
        glPushMatrix(); glTranslatef(cosf(th)*i*sp, sinf(th)*i*sp, 0);
        gc(CRED, 0.6f); drawSphere(0.07f); glPopMatrix();
    }
}

void sceneWave3D(){
    double m=V("m",9.11e-31), v=V("v",1e6);
    double lam=6.626e-34/(m*v);
    float freq=(float)std::max(0.3,std::min(4.0,1e10*lam));
    sineLine(-7,7, 1.0f, freq, T()*4, 0, CBLUE, 2.f);
    glDisable(GL_LIGHTING);
    gc(CORANGE); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=200; i++){
        float x=i/200.f*14-7;
        glVertex3f(x, 0, sinf(freq*x+T()*4)*1.f);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    arrow(6.5f,0,0, 1,0,0, 1.5f, TEXT1);
    gc(ACCENT2);
    glPushMatrix();
    glTranslatef((float)fmod(T()*2,14)-7, 0, 0);
    drawSphere(0.22f);
    glPopMatrix();
}

void sceneQuantumBox(){
    int N=std::max(1,(int)round(V("n",1)));
    gc(CRED, 0.9f);
    glPushMatrix(); glTranslatef(-5.1f,0,0); drawBox(0.15f,5,0.55f); glPopMatrix();
    glPushMatrix(); glTranslatef( 5.1f,0,0); drawBox(0.15f,5,0.55f); glPopMatrix();
    for(int lvl=1; lvl<=N; lvl++){
        glColor4f(0.1f,0.35f,0.1f,0.45f);
        glPushMatrix(); glTranslatef(0,(float)(lvl*lvl)*0.55f,0.12f); drawBox(10,0.035f,0.1f); glPopMatrix();
    }
    sineLine(-5,5, 1.6f, (float)(N*(float)M_PI/10.f), T()*2, 0, ACCENT2, 2.f);
    glDisable(GL_LIGHTING);
    glColor4f(ACCENT.r,ACCENT.g,ACCENT.b,0.35f);
    glBegin(GL_TRIANGLE_STRIP);
    for(int i=0; i<=200; i++){
        float x=-5+i/200.f*10;
        float psi=sinf((float)N*(float)M_PI*(i/200.f)+T()*2)*1.6f;
        glVertex3f(x, 0, 0.05f);
        glVertex3f(x, psi*psi*0.4f, 0.05f);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void scenePlanck(){
    double Tv=V("T",3000);
    glDisable(GL_LIGHTING);
    gc(CORANGE); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=1; i<=150; i++){
        double lam=i*0.062+0.07;
        double denom=exp(6.626e-34*3e8/((lam*1e-6)*1.38e-23*Tv))-1;
        float y=(float)std::min(
            denom>1e-300 ? (8*M_PI*6.626e-34*3e8)/pow(lam*1e-6,5)/denom*1e-95*5 : 0.0,
            4.0);
        glVertex3f((float)(lam*4-5), y, 0);
    }
    glEnd();
    glLineWidth(1.f);
    float pk=(float)(2.898e-3/Tv*1e6*4-5);
    glEnable(GL_LIGHTING);
    gc(CYELLOW);
    glPushMatrix(); glTranslatef(pk,0.5f,0); drawSphere(0.18f+0.05f*sinf(T()*5)); glPopMatrix();
    glDisable(GL_LIGHTING);
    float spc[7][3] = {{1,0,0},{1,0.33f,0},{1,0.8f,0},{0,1,0},{0,0.6f,1},{0,0,1},{0.5f,0,1}};
    for(int i=0; i<7; i++){
        glColor3fv(spc[i]);
        glPushMatrix(); glTranslatef(-3.5f+i*0.85f,-1.5f,0); drawBox(0.72f,0.2f,0.2f); glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

void sceneHeat(){
    double kv=V("k",50), dT=V("dT",100);
    glColor4f(0.12f,0.20f,0.40f,0.55f);
    glPushMatrix(); drawBox(7,3.5f,0.75f); glPopMatrix();
    glColor4f(1,0.1f,0,0.9f);
    glPushMatrix(); glTranslatef(-3.52f,0,0); drawBox(0.13f,3.5f,0.8f); glPopMatrix();
    glColor4f(0,0.5f,1,0.9f);
    glPushMatrix(); glTranslatef( 3.52f,0,0); drawBox(0.13f,3.5f,0.8f); glPopMatrix();
    int na=std::min(8,(int)(kv/30.0)+2);
    float sc=(float)std::min(2.5, 0.4+kv/200.0+dT/500.0);
    for(int i=0; i<na; i++){
        float ox=(float)(-2.0+fmod(T()*0.6,2.5)), oy=(float)((i-na/2+0.5)*0.42);
        arrow(ox,oy,0, 1,0,0, sc, CYELLOW);
    }
}

void sceneSnell(){
    double n1=V("n1",1), t1=V("t1",45), n2=V("n2",1.5);
    double s=n1*sin(t1*M_PI/180)/n2, t2=s<=1?asin(s):M_PI/2;
    glColor4f(0,0.05f,0.18f,0.4f);
    glPushMatrix(); drawBox(14,7,0.05f); glPopMatrix();
    glColor4f(0,0.05f,0.18f,0.2f);
    glPushMatrix(); glTranslatef(0,2.5f,0); drawBox(14,5,0.04f); glPopMatrix();
    glColor4f(0,0.08f,0.08f,0.18f);
    glPushMatrix(); glTranslatef(0,-2.5f,0); drawBox(14,5,0.04f); glPopMatrix();
    glDisable(GL_LIGHTING);
    gc(TEXT2, 0.6f); glLineWidth(1.f);
    glBegin(GL_LINES); glVertex3f(0,-4.5f,0); glVertex3f(0,4.5f,0); glEnd();
    glEnable(GL_LIGHTING);
    arrow(-(float)(sin(t1*M_PI/180))*5, (float)(cos(t1*M_PI/180))*5, 0,
          (float)sin(t1*M_PI/180), -(float)cos(t1*M_PI/180), 0, 5.5f, CBLUE);
    arrow(0,0,0, (float)sin(t2),-(float)cos(t2),0, 5.0f, CORANGE);
    glDisable(GL_LIGHTING);
    gc(CBLUE, 0.4f); glLineWidth(1.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=20; i++){
        float a=(float)((M_PI/2-t1*M_PI/180)*i/20);
        glVertex3f(sinf(a)*1.5f, cosf(a)*1.5f, 0);
    }
    glEnd();
    gc(CORANGE, 0.4f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=20; i++){
        float a=(float)(t2*i/20);
        glVertex3f(sinf(a)*1.5f, -cosf(a)*1.5f, 0);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void sceneBar(){
    auto& vars=curEq().vars;
    int n=(int)vars.size();
    float maxV=1.f;
    for(auto& v : vars) maxV=std::max(maxV,(float)fabs(V(v.sym.c_str(),v.val)));
    C3 pal[6] = {CBLUE,CORANGE,CGREEN,CYELLOW,HOT,CCYAN};
    for(int i=0; i<n; i++){
        float raw=(float)fabs(V(vars[i].sym.c_str(),vars[i].val));
        float h=std::max(0.15f,std::min(4.f,raw/maxV*3.5f))*(1.f+0.02f*sinf(T()*2+(float)i));
        float cx=(i-n/2+0.5f)*1.55f;
        gc(pal[i%6]);
        glPushMatrix(); glTranslatef(cx,h/2-0.2f,0); drawBox(0.65f,h,0.65f); glPopMatrix();
        gc(TEXT0, 0.7f);
        glPushMatrix(); glTranslatef(cx,h+0.5f,0); drawSphere(0.1f); glPopMatrix();
    }
    glColor4f(0.15f,0.15f,0.35f,0.4f);
    glPushMatrix(); glTranslatef(0,-0.3f,0); drawBox((float)n*1.8f,0.06f,1.f); glPopMatrix();
}

void drawCurrentScene(){
    const std::string& viz=curEq().viz;
    if     (viz=="fcc")        sceneFCC();
    else if(viz=="bcc")        sceneBCC();
    else if(viz=="lattice")    sceneLattice();
    else if(viz=="photon")     scenePhoton();
    else if(viz=="laser")      sceneLaser();
    else if(viz=="fibre")      sceneFibre();
    else if(viz=="cantilever") sceneCantilever();
    else if(viz=="torsion")    sceneTorsion();
    else if(viz=="compton")    sceneCompton();
    else if(viz=="wave3d")     sceneWave3D();
    else if(viz=="quantumbox") sceneQuantumBox();
    else if(viz=="planck")     scenePlanck();
    else if(viz=="heat")       sceneHeat();
    else if(viz=="snell")      sceneSnell();
    else                       sceneBar();
}

/* ─────────────────────────────────────────────────────────
   EQUATIONS  (15 from 20BSPH101)
───────────────────────────────────────────────────────── */
void initEquations(){
    g_eqs.clear();
    auto A=[&](Equation e){ g_eqs.push_back(e); };

    A({"apf_fcc","CO1","APF - FCC","APF=4(4/3pi*r3)/a3","APF","","fcc",
       {{"r","Atomic radius","A",0.5,2,1,0.05}},
       [](auto& v){double r=v["r"],a=2*sqrt(2.)*r; return 4*(4./3*M_PI*pow(r,3))/pow(a,3);}});

    A({"apf_bcc","CO1","APF - BCC","APF=2(4/3pi*r3)/a3","APF","","bcc",
       {{"r","Atomic radius","A",0.5,2,1,0.05}},
       [](auto& v){double r=v["r"],a=4*r/sqrt(3.); return 2*(4./3*M_PI*pow(r,3))/pow(a,3);}});

    A({"dspacing","CO1","Interplanar Spacing","d=a/sqrt(h2+k2+l2)","d","A","lattice",
       {{"a","Lattice param","A",2,6,4,0.1},{"h","Miller h","",1,5,1,1},{"k","Miller k","",0,5,1,1},{"l","Miller l","",0,5,0,1}},
       [](auto& v){return v["a"]/sqrt(v["h"]*v["h"]+v["k"]*v["k"]+v["l"]*v["l"]);}});

    A({"einstein_ab","CO2","Einstein A/B Coeff.","A/B=8pi*h*nu3/c3","A/B","Js/m3","photon",
       {{"nu","Frequency","Hz",1e13,1e15,5e14,1e13}},
       [](auto& v){return 8*M_PI*6.626e-34*pow(v["nu"],3)/pow(3e8,3);}});

    A({"co2_laser","CO2","CO2 Laser Gain","I=I0*exp(g*L)","I/I0","x","laser",
       {{"g","Gain coeff","m-1",0.01,5,1,0.05},{"L","Cavity length","m",0.1,3,0.5,0.05}},
       [](auto& v){return exp(v["g"]*v["L"]);}});

    A({"na","CO3","Numerical Aperture","NA=sqrt(n1^2-n2^2)","NA","","fibre",
       {{"n1","Core index","",1.4,1.8,1.5,0.01},{"n2","Clad index","",1.3,1.78,1.46,0.01}},
       [](auto& v){double x=v["n1"]*v["n1"]-v["n2"]*v["n2"]; return x>0?sqrt(x):0;}});

    A({"acceptance","CO3","Acceptance Angle","theta=arcsin(NA)","theta_a","deg","fibre",
       {{"n1","Core index","",1.4,1.8,1.5,0.01},{"n2","Clad index","",1.3,1.78,1.46,0.01}},
       [](auto& v){double x=v["n1"]*v["n1"]-v["n2"]*v["n2"];double na=x>0?sqrt(x):0;return asin(std::min(na,1.))*180/M_PI;}});

    A({"cantilever","CO4","Young's Mod (Cantilever)","Y=Wl3/3bd3*delta","Y","Pa","cantilever",
       {{"W","Load","N",0.1,10,1,0.1},{"l","Length","m",0.1,1,0.5,0.01},
        {"b","Breadth","m",0.005,0.05,0.02,0.001},{"d","Thickness","m",0.001,0.01,0.003,0.0001},
        {"delta","Depression","m",0.001,0.05,0.01,0.001}},
       [](auto& v){return v["W"]*pow(v["l"],3)/(3*v["b"]*pow(v["d"],3)*v["delta"]);}});

    A({"torsion","CO4","Torsional Pendulum","T=2pi*sqrt(I/C)","T","s","torsion",
       {{"I","Moment of inertia","kgm2",0.0001,0.01,0.001,0.0001},{"C","Torsional const","Nm/rad",0.001,0.1,0.01,0.001}},
       [](auto& v){return 2*M_PI*sqrt(v["I"]/v["C"]);}});

    A({"compton","CO5","Compton Shift","dL=(h/mec)(1-cos t)","dLambda","m","compton",
       {{"theta","Scatter angle","deg",0,180,90,1}},
       [](auto& v){return 6.626e-34/(9.11e-31*3e8)*(1-cos(v["theta"]*M_PI/180));}});

    A({"debroglie","CO5","de-Broglie Wavelength","lambda=h/(m*v)","lambda","m","wave3d",
       {{"m","Mass","kg",9e-31,1e-27,9.11e-31,1e-32},{"v","Velocity","m/s",1e4,3e7,1e6,1e4}},
       [](auto& v){return 6.626e-34/(v["m"]*v["v"]);}});

    A({"quantum_box","CO5","Particle in 1D Box","En=n2*h2/(8*m*a2)","En","J","quantumbox",
       {{"n","Quantum number","",1,6,1,1},{"a","Box length","m",1e-10,1e-9,1e-10,1e-11},
        {"m","Mass","kg",9e-31,2e-30,9.11e-31,1e-32}},
       [](auto& v){return v["n"]*v["n"]*pow(6.626e-34,2)/(8*v["m"]*v["a"]*v["a"]);}});

    A({"planck","CO5","Planck's Law (peak)","lambda_max=b/T","lambda_peak","um","planck",
       {{"T","Temperature","K",300,8000,3000,100}},
       [](auto& v){return 2.898e-3/v["T"]*1e6;}});

    A({"fourier","CO6","Fourier Heat Conduction","Q/t=k*A*dT/dx","Q/t","W","heat",
       {{"k","Conductivity","W/mK",0.01,400,50,1},{"A","Area","m2",0.001,1,0.1,0.001},
        {"dT","Temp diff","K",1,500,100,1},{"dx","Thickness","m",0.001,1,0.05,0.001}},
       [](auto& v){return v["k"]*v["A"]*v["dT"]/v["dx"];}});

    A({"snell","CO3","Snell's Law","t2=arcsin(n1*sin(t1)/n2)","theta2","deg","snell",
       {{"n1","Index 1","",1,2.5,1,0.05},{"t1","Angle 1","deg",1,89,45,1},{"n2","Index 2","",1,2.5,1.5,0.05}},
       [](auto& v){double s=v["n1"]*sin(v["t1"]*M_PI/180)/v["n2"];
                   return s<=1?asin(s)*180/M_PI:std::numeric_limits<double>::quiet_NaN();}});
}

/* ─────────────────────────────────────────────────────────
   LOCAL EQUATION PARSER  (no external deps, pure C++)
   Supports: + - * / ^ ( ) sin cos tan sqrt exp log abs pi
───────────────────────────────────────────────────────── */
struct Eval {
    const std::string& s;
    int pos=0;
    std::map<std::string,double>* vars;
    std::vector<std::string>& found;

    double e(){ return as(); }
    double as(){
        double r=md();
        while(pos<(int)s.size()){
            ws(); char c=s[pos];
            if(c=='+'){pos++;r+=md();}
            else if(c=='-'){pos++;r-=md();}
            else break;
        }
        return r;
    }
    double md(){
        double r=pw();
        while(pos<(int)s.size()){
            ws(); char c=s[pos];
            if(c=='*'){pos++;r*=pw();}
            else if(c=='/'){pos++;double d=pw();r=d?r/d:1e300;}
            else break;
        }
        return r;
    }
    double pw(){
        double r=un(); ws();
        if(pos<(int)s.size()&&s[pos]=='^'){pos++;double x=un();r=pow(r,x);}
        return r;
    }
    double un(){
        ws();
        if(pos<(int)s.size()&&s[pos]=='-'){pos++;return -pr();}
        if(pos<(int)s.size()&&s[pos]=='+') pos++;
        return pr();
    }
    double pr(){
        ws();
        if(pos>=(int)s.size()) return 0;
        char c=s[pos];
        if(c=='('){
            pos++;double r=e();ws();
            if(pos<(int)s.size()&&s[pos]==')') pos++;
            return r;
        }
        if(isdigit(c)||c=='.'){
            std::string t;
            while(pos<(int)s.size()&&(isdigit(s[pos])||s[pos]=='.'||s[pos]=='e'||s[pos]=='E'||
                  ((s[pos]=='+'||s[pos]=='-')&&pos>0&&(s[pos-1]=='e'||s[pos-1]=='E'))))
                t+=s[pos++];
            return atof(t.c_str());
        }
        if(isalpha(c)||c=='_'){
            std::string nm;
            while(pos<(int)s.size()&&(isalnum(s[pos])||s[pos]=='_')) nm+=s[pos++];
            ws(); char p2=pos<(int)s.size()?s[pos]:0;
            auto fn=[&](std::function<double(double)> f)->double{
                pos++;double a=e();ws();if(pos<(int)s.size()&&s[pos]==')')pos++;return f(a);};
            if(nm=="sin"  &&p2=='(') return fn([](double a){return sin(a);});
            if(nm=="cos"  &&p2=='(') return fn([](double a){return cos(a);});
            if(nm=="tan"  &&p2=='(') return fn([](double a){return tan(a);});
            if(nm=="sqrt" &&p2=='(') return fn([](double a){return sqrt(fabs(a));});
            if(nm=="exp"  &&p2=='(') return fn([](double a){return ::exp(a);});
            if(nm=="log"  &&p2=='(') return fn([](double a){return log(fabs(a)+1e-300);});
            if(nm=="abs"  &&p2=='(') return fn([](double a){return fabs(a);});
            if(nm=="pi"||nm=="PI") return M_PI;
            if(vars&&vars->count(nm)) return (*vars)[nm];
            bool ex=false;
            for(auto& n2:found) if(n2==nm){ex=true;break;}
            if(!ex&&nm.size()<=8) found.push_back(nm);
            return 1.0;
        }
        return 0;
    }
    void ws(){ while(pos<(int)s.size()&&s[pos]==' ')pos++; }
};

struct PR { bool ok=false; std::string lhs,rhs,err; std::vector<std::string> vars; };

PR parseRaw(const std::string& raw){
    PR pr;
    size_t eq=raw.find('=');
    if(eq==std::string::npos){pr.err="Missing '='";return pr;}
    pr.lhs=raw.substr(0,eq);
    while(!pr.lhs.empty()&&pr.lhs.back()==' ') pr.lhs.pop_back();
    pr.rhs=raw.substr(eq+1);
    while(!pr.rhs.empty()&&pr.rhs[0]==' ') pr.rhs=pr.rhs.substr(1);
    if(pr.rhs.empty()){pr.err="Empty RHS";return pr;}
    std::vector<std::string> f;
    Eval d{pr.rhs,0,nullptr,f};
    try{ d.e(); pr.vars=f; } catch(...){pr.err="Parse error";return pr;}
    if(pr.vars.empty()){pr.err="No variables found";return pr;}
    if(pr.vars.size()>6){pr.err="Too many vars (max 6)";return pr;}
    std::map<std::string,double> t2;
    for(auto& v:pr.vars) t2[v]=1.0;
    Eval ep{pr.rhs,0,&t2,f};
    try{ ep.e(); } catch(...){pr.err="Eval error";return pr;}
    pr.ok=true;
    return pr;
}

Var guessVar(const std::string& sym){
    struct D{const char*s;double mn,mx,val,step;const char*u,*n;};
    static D t[]={
        {"m",0.1,100,10,0.1,"kg","Mass"},
        {"v",0,100,20,0.5,"m/s","Velocity"},
        {"r",0.1,10,2,0.05,"m","Radius"},
        {"t",0.1,20,5,0.1,"s","Time"},
        {"g",1,25,9.8,0.1,"m/s2","Gravity"},
        {"h",0,200,10,1,"m","Height"},
        {"l",0.1,10,1,0.05,"m","Length"},
        {"a",0,50,5,0.1,"m/s2","Accel"},
        {"f",0,1000,100,1,"N","Force"},
        {"k",1,500,50,1,"N/m","Spring"},
        {"n",1,8,1,1,"","Quantum n"},
        {"p",1,10000,101,1,"Pa","Pressure"},
        {nullptr,0,0,0,0,"",""}
    };
    for(int i=0;t[i].s;i++)
        if(sym==t[i].s)
            return {sym,t[i].n,t[i].u,t[i].mn,t[i].mx,t[i].val,t[i].step};
    return {sym,sym,"",0.1,100,5,0.1};
}

void applyCustom(const PR& pr){
    g_custom.id="custom"; g_custom.co="CUST";
    g_custom.name="Custom: "+pr.lhs;
    g_custom.formula=pr.lhs+" = "+pr.rhs;
    g_custom.result=pr.lhs; g_custom.unit=""; g_custom.viz="bar3d";
    g_custom.isCustom=true;
    g_custom.vars.clear();
    for(auto& s:pr.vars) g_custom.vars.push_back(guessVar(s));
    std::string rhs=pr.rhs;
    g_custom.calc=[rhs](std::map<std::string,double>& vv)->double{
        std::vector<std::string> f;
        Eval ep{rhs,0,&vv,f};
        try{ return ep.e(); }
        catch(...){ return std::numeric_limits<double>::quiet_NaN(); }
    };
    g_hasCustom=true; g_selIdx=-1;
    g_vals.clear();
    for(auto& v:g_custom.vars) g_vals[v.sym]=v.val;
}

/* ─────────────────────────────────────────────────────────
   SLIDER WIDGET
   onDrag() writes g_vals[sym] — that's ALL it does.
   Scene reads it next frame. Animation never stops.
───────────────────────────────────────────────────────── */
struct Slider {
    int vi;
    float x, y, w, tx=0;
    bool drag=false;

    void sync(){
        Var& v=curEq().vars[vi];
        tx=x+10+(float)((v.val-v.mn)/std::max(1e-30,v.mx-v.mn))*(w-20);
    }
    bool hit(float mx, float my){ return fabs(mx-tx)<=11 && fabs(my-(y+38))<=11; }
    void onDrag(float mx){
        Var& v=curEq().vars[vi];
        float t=std::max(0.f,std::min(1.f,(mx-(x+10))/(w-20)));
        v.val=std::max(v.mn,std::min(v.mx,v.mn+round(t*(v.mx-v.mn)/v.step)*v.step));
        g_vals[v.sym]=v.val; // only thing slider does — scene reads it next frame
        sync();
    }
    void draw2D(){
        glColor4f(BG2.r,BG2.g,BG2.b,0.92f);
        glBegin(GL_QUADS);
        glVertex2f(x-4,y-2);glVertex2f(x+w+4,y-2);glVertex2f(x+w+4,y+52);glVertex2f(x-4,y+52);
        glEnd();
        glColor4f(BORDER.r,BORDER.g,BORDER.b,0.6f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x-4,y-2);glVertex2f(x+w+4,y-2);glVertex2f(x+w+4,y+52);glVertex2f(x-4,y+52);
        glEnd();
        float ty=y+38, bx=x+10, bw=w-20;
        glColor4f(BG3.r,BG3.g,BG3.b,1);
        glBegin(GL_QUADS);
        glVertex2f(bx,ty-1.5f);glVertex2f(bx+bw,ty-1.5f);glVertex2f(bx+bw,ty+1.5f);glVertex2f(bx,ty+1.5f);
        glEnd();
        Var& v=curEq().vars[vi];
        float fill=(float)((v.val-v.mn)/std::max(1e-30,v.mx-v.mn))*bw;
        gc(ACCENT);
        glBegin(GL_QUADS);
        glVertex2f(bx,ty-1.5f);glVertex2f(bx+fill,ty-1.5f);glVertex2f(bx+fill,ty+1.5f);glVertex2f(bx,ty+1.5f);
        glEnd();
        gc(ACCENT2);
        glBegin(GL_TRIANGLE_FAN); glVertex2f(tx,ty);
        for(int i=0;i<=20;i++){float a=2*(float)M_PI*i/20;glVertex2f(tx+6.5f*cosf(a),ty+6.5f*sinf(a));}
        glEnd();
        glColor4f(BG0.r,BG0.g,BG0.b,1);
        glBegin(GL_TRIANGLE_FAN); glVertex2f(tx,ty);
        for(int i=0;i<=20;i++){float a=2*(float)M_PI*i/20;glVertex2f(tx+3.5f*cosf(a),ty+3.5f*sinf(a));}
        glEnd();
    }
};

static std::vector<Slider> g_sliders;

void rebuildSliders(){
    g_sliders.clear();
    auto& eq=curEq();
    int n=(int)eq.vars.size();
    if(!n) return;
    int pad=12, sw=(CANVAS_W-pad*(n+1))/n;
    for(int i=0;i<n;i++){
        Slider sl; sl.vi=i;
        sl.x=(float)(CANVAS_X+pad+i*(sw+pad));
        sl.y=(float)(WIN_H-CT_H+14);
        sl.w=(float)sw;
        sl.sync();
        g_sliders.push_back(sl);
    }
}

/* ─────────────────────────────────────────────────────────
   2D UI  (OpenGL ortho overlay)
───────────────────────────────────────────────────────── */
static std::string g_inp;
static bool        g_inpActive=false;
static std::string g_msg;
static bool        g_msgOk=true;
static double      g_msgT=0;

void drawUI(){
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0,WIN_W,WIN_H,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // sidebar background
    glColor4f(BG1.r,BG1.g,BG1.b,0.98f);
    glBegin(GL_QUADS);
    glVertex2f(0,0);glVertex2f(SB_W,0);glVertex2f(SB_W,WIN_H);glVertex2f(0,WIN_H);
    glEnd();
    glColor4f(ACCENT.r,ACCENT.g,ACCENT.b,0.22f);
    glBegin(GL_LINES); glVertex2f(SB_W,0); glVertex2f(SB_W,WIN_H); glEnd();

    // logo
    txt(10,20,"SRI SAI RAM ENGINEERING COLLEGE",10,ACCENT3,0.7f);
    txt(10,38,"PhysicsSim 3D  --  OpenGL/C++",18,TEXT0);
    txt(10,53,"20BSPH101  Engineering Physics",10,TEXT2);
    txt(10,63,"Native OpenGL | GLFW | Local Eq Parser",10,TEXT2,0.6f);
    glColor4f(BORDER.r,BORDER.g,BORDER.b,0.7f);
    glBegin(GL_LINES); glVertex2f(0,70); glVertex2f(SB_W,70); glEnd();

    // prompt zone
    glColor4f(BG0.r,BG0.g,BG0.b,0.98f);
    glBegin(GL_QUADS);
    glVertex2f(0,70);glVertex2f(SB_W,70);glVertex2f(SB_W,160);glVertex2f(0,160);
    glEnd();
    glColor4f(BORDER.r,BORDER.g,BORDER.b,0.7f);
    glBegin(GL_LINES); glVertex2f(0,160); glVertex2f(SB_W,160); glEnd();
    txt(10,84,"EQUATION PROMPT",10,ACCENT3,0.8f);
    txt(10,136,"e.g.  E=0.5*m*v^2  or  F=m*a",10,TEXT2,0.8f);
    txt(10,148,"* ^ sqrt() sin() cos() -- vars auto-detected",10,TEXT2,0.6f);

    // input box
    C3 bc=g_inpActive?ACCENT:BORDER;
    glColor4f(BG2.r,BG2.g,BG2.b,1);
    glBegin(GL_QUADS);
    glVertex2f(8,90);glVertex2f(SB_W-62,90);glVertex2f(SB_W-62,122);glVertex2f(8,122);
    glEnd();
    glColor4f(bc.r,bc.g,bc.b,1);
    glBegin(GL_LINE_LOOP);
    glVertex2f(8,90);glVertex2f(SB_W-62,90);glVertex2f(SB_W-62,122);glVertex2f(8,122);
    glEnd();
    std::string disp=g_inp.size()>34?g_inp.substr(g_inp.size()-34):g_inp;
    if(g_inpActive&&fmod(g_time,1.0)<0.5) disp+='|';
    txt(13,111,disp,10,g_inpActive?ACCENT2:TEXT2);

    // RUN button
    gc(ACCENT);
    glBegin(GL_QUADS);
    glVertex2f(SB_W-56,90);glVertex2f(SB_W-6,90);glVertex2f(SB_W-6,122);glVertex2f(SB_W-56,122);
    glEnd();
    txt(SB_W-46,111,"RUN",12,TEXT0);

    // parse message
    if(!g_msg.empty()&&g_time-g_msgT<5.0){
        float al=(float)std::min(1.0,5.0-(g_time-g_msgT));
        C3 mc=g_msgOk?ACCENT3:HOT;
        C3 bg=g_msgOk?C3{0,0.07f,0.06f}:C3{0.07f,0.01f,0.01f};
        glColor4f(bg.r,bg.g,bg.b,0.9f*al);
        glBegin(GL_QUADS);
        glVertex2f(0,160);glVertex2f(SB_W,160);glVertex2f(SB_W,178);glVertex2f(0,178);
        glEnd();
        txt(8,173,clip(g_msg,44),10,mc,al);
    }

    // equation list
    txt(10,193,"SYLLABUS EQUATIONS",10,TEXT2,0.65f);
    glColor4f(BORDER.r,BORDER.g,BORDER.b,0.4f);
    glBegin(GL_LINES); glVertex2f(10,197); glVertex2f(SB_W-10,197); glEnd();

    float iy=200; int ih=46;
    auto item=[&](bool active, const std::string& name, const std::string& co, const std::string& formula){
        if(iy>WIN_H-CT_H-10){iy+=(float)ih;return;}
        if(active){
            glColor4f(BG3.r,BG3.g,BG3.b,0.92f);
            glBegin(GL_QUADS);
            glVertex2f(4,iy);glVertex2f(SB_W-4,iy);glVertex2f(SB_W-4,iy+ih-3);glVertex2f(4,iy+ih-3);
            glEnd();
            gc(ACCENT);
            glBegin(GL_QUADS);
            glVertex2f(4,iy);glVertex2f(7,iy);glVertex2f(7,iy+ih-3);glVertex2f(4,iy+ih-3);
            glEnd();
        }
        txt(12,iy+15,clip(name,30),12,active?ACCENT2:TEXT0);
        txt(SB_W-38,iy+15,co,10,active?ACCENT2:ACCENT,0.9f);
        txt(12,iy+28,clip(formula,36),10,TEXT2,active?0.85f:0.6f);
        iy+=(float)ih;
    };

    if(g_hasCustom) item(g_selIdx==-1, g_custom.name, "CUST", g_custom.formula);
    for(int i=0;i<(int)g_eqs.size();i++)
        item(g_selIdx==i, g_eqs[i].name, g_eqs[i].co, g_eqs[i].formula);

    // topbar
    glColor4f(BG1.r,BG1.g,BG1.b,1);
    glBegin(GL_QUADS);
    glVertex2f(SB_W,0);glVertex2f(WIN_W,0);glVertex2f(WIN_W,TB_H);glVertex2f(SB_W,TB_H);
    glEnd();
    glColor4f(ACCENT.r,ACCENT.g,ACCENT.b,0.22f);
    glBegin(GL_LINES); glVertex2f(SB_W,TB_H); glVertex2f(WIN_W,TB_H); glEnd();
    auto& ceq=curEq();
    glColor4f(BG2.r,BG2.g,BG2.b,1);
    glBegin(GL_QUADS);
    glVertex2f(SB_W+8,6);glVertex2f(SB_W+52,6);glVertex2f(SB_W+52,30);glVertex2f(SB_W+8,30);
    glEnd();
    txt(SB_W+13,22,ceq.co,10,ACCENT2);
    txt(SB_W+60,23,clip(ceq.name,32),18,TEXT0);
    txt(WIN_W-295,23,clip(ceq.formula,44),12,ACCENT2,0.85f);
    txt(WIN_W-98,13,"drag/scroll",10,TEXT2,0.45f);
    txt(WIN_W-98,25,"arrows: eq",10,TEXT2,0.45f);

    // result HUD
    float hx=(float)WIN_W-168, hy=(float)TB_H+12;
    glColor4f(BG0.r,BG0.g,BG0.b,0.93f);
    glBegin(GL_QUADS);
    glVertex2f(hx,hy);glVertex2f(hx+156,hy);glVertex2f(hx+156,hy+90);glVertex2f(hx,hy+90);
    glEnd();
    glColor4f(ACCENT.r,ACCENT.g,ACCENT.b,0.35f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(hx,hy);glVertex2f(hx+156,hy);glVertex2f(hx+156,hy+90);glVertex2f(hx,hy+90);
    glEnd();
    glColor4f(ACCENT.r,ACCENT.g,ACCENT.b,0.45f);
    glBegin(GL_LINES); glVertex2f(hx,hy); glVertex2f(hx+156,hy); glEnd();
    txt(hx+8,hy+14,ceq.result,10,TEXT2);
    double res=std::numeric_limits<double>::quiet_NaN();
    try{ res=ceq.calc(g_vals); } catch(...){}
    txt(hx+8,hy+46,fmt(res),24,(!std::isnan(res)&&!std::isinf(res))?ACCENT2:HOT);
    txt(hx+8,hy+62,ceq.unit,10,TEXT1);
    txt(hx+8,hy+76,clip(ceq.name,20),10,TEXT2,0.65f);

    // controls panel
    glColor4f(BG1.r,BG1.g,BG1.b,1);
    glBegin(GL_QUADS);
    glVertex2f(SB_W,WIN_H-CT_H);glVertex2f(WIN_W,WIN_H-CT_H);glVertex2f(WIN_W,WIN_H);glVertex2f(SB_W,WIN_H);
    glEnd();
    glColor4f(ACCENT.r,ACCENT.g,ACCENT.b,0.22f);
    glBegin(GL_LINES); glVertex2f(SB_W,WIN_H-CT_H); glVertex2f(WIN_W,WIN_H-CT_H); glEnd();
    txt(SB_W+8,WIN_H-CT_H+13,"VARIABLES  --  drag sliders (animation never stops)",10,TEXT2,0.6f);

    for(auto& sl : g_sliders){
        sl.draw2D();
        Var& v=ceq.vars[sl.vi];
        txt(sl.x,sl.y+17,v.sym,12,ACCENT2);
        std::string vs=fmt(v.val)+(v.unit.empty()?"":" "+v.unit);
        txt(sl.x+sl.w-(float)vs.size()*7,sl.y+17,vs,10,ACCENT);
        txt(sl.x,sl.y+29,clip(v.name,22),10,TEXT2,0.85f);
    }

    txt(SB_W+8,WIN_H-5,"C++ OpenGL | MinGW-w64 | GLFW | WGL fonts | Local Eq Parser",10,TEXT2,0.35f);
    glEnable(GL_LIGHTING); glEnable(GL_DEPTH_TEST);
}

/* ─────────────────────────────────────────────────────────
   GLFW CALLBACKS
───────────────────────────────────────────────────────── */
static GLFWwindow* g_win=nullptr;

void tryParse(){
    auto pr=parseRaw(g_inp);
    if(pr.ok){
        applyCustom(pr); rebuildSliders();
        std::string varlist;
        for(int i=0;i<(int)pr.vars.size();i++){if(i)varlist+=",";varlist+=pr.vars[i];}
        g_msg="OK  "+pr.lhs+" = f("+varlist+")";
        g_msgOk=true;
    } else {
        g_msg="ERR: "+pr.err;
        g_msgOk=false;
    }
    g_msgT=g_time;
}

void cbKey(GLFWwindow*,int key,int,int action,int){
    if(action!=GLFW_PRESS&&action!=GLFW_REPEAT) return;
    if(g_inpActive){
        if(key==GLFW_KEY_ENTER||key==GLFW_KEY_KP_ENTER) tryParse();
        else if(key==GLFW_KEY_BACKSPACE&&!g_inp.empty()) g_inp.pop_back();
        else if(key==GLFW_KEY_ESCAPE) g_inpActive=false;
    } else {
        if(key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(g_win,1);
        if(key==GLFW_KEY_DOWN){
            int n=(int)g_eqs.size();
            g_selIdx=(g_selIdx==-1)?0:(g_selIdx+1)%n;
            loadEq(g_selIdx); rebuildSliders();
        }
        if(key==GLFW_KEY_UP){
            int n=(int)g_eqs.size();
            g_selIdx=(g_selIdx<=0)?n-1:g_selIdx-1;
            loadEq(g_selIdx); rebuildSliders();
        }
    }
}

void cbChar(GLFWwindow*,unsigned int cp){
    if(g_inpActive&&cp!='\r'&&cp!='\n'&&g_inp.size()<80)
        g_inp+=(char)cp;
}

void cbMBtn(GLFWwindow*,int btn,int action,int){
    if(btn!=GLFW_MOUSE_BUTTON_LEFT) return;
    double mx,my; glfwGetCursorPos(g_win,&mx,&my);
    if(action==GLFW_PRESS){
        if(mx>=8&&mx<=SB_W-62&&my>=90&&my<=122){ g_inpActive=true; return; }
        if(mx>=SB_W-56&&mx<=SB_W-6&&my>=90&&my<=122){ tryParse(); g_inpActive=false; return; }
        g_inpActive=false;
        if(mx<SB_W&&my>200){
            float iy=200; int ih=46;
            if(g_hasCustom){
                if(my>=iy&&my<iy+ih){
                    g_selIdx=-1; g_vals.clear();
                    for(auto& v:g_custom.vars) g_vals[v.sym]=v.val;
                    rebuildSliders(); return;
                }
                iy+=(float)ih;
            }
            for(int i=0;i<(int)g_eqs.size();i++){
                if(my>=iy&&my<iy+ih){ loadEq(i); rebuildSliders(); return; }
                iy+=(float)ih;
            }
        }
        for(auto& sl:g_sliders) if(sl.hit((float)mx,(float)my)){sl.drag=true;return;}
        if(mx>SB_W&&my>TB_H&&my<WIN_H-CT_H){ g_drag=true; g_lmx=mx; g_lmy=my; }
    }
    if(action==GLFW_RELEASE){
        g_drag=false;
        for(auto& sl:g_sliders) sl.drag=false;
    }
}

void cbMMove(GLFWwindow*,double mx,double my){
    if(g_drag){
        g_camTheta-=(mx-g_lmx)*0.007;
        g_camPhi=std::max(0.05,std::min(1.45,g_camPhi-(my-g_lmy)*0.007));
        g_lmx=mx; g_lmy=my;
    }
    for(auto& sl:g_sliders) if(sl.drag) sl.onDrag((float)mx);
}

void cbScroll(GLFWwindow*,double,double dy){
    double mx,my; glfwGetCursorPos(g_win,&mx,&my);
    if(mx>SB_W) g_camDist=std::max(2.0,std::min(28.0,g_camDist-dy*0.5));
}

void cbFB(GLFWwindow*,int w,int h){
    WIN_W=w; WIN_H=std::max(h,400); rebuildSliders();
}

/* ─────────────────────────────────────────────────────────
   MAIN
───────────────────────────────────────────────────────── */
int main(int argc, char** argv){
    (void)argc; (void)argv;

    if(!glfwInit()){
        MessageBoxA(NULL,"GLFW init failed","Error",MB_OK);
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
    glfwWindowHint(GLFW_SAMPLES,4);

    g_win=glfwCreateWindow(WIN_W,WIN_H,"PhysicsSim 3D  --  20BSPH101 Engineering Physics",NULL,NULL);
    if(!g_win){
        glfwTerminate();
        MessageBoxA(NULL,"Window creation failed","Error",MB_OK);
        return 1;
    }

    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);

    // Init WGL bitmap fonts (needs GL context + HDC)
    HDC hdc=GetDC(glfwGetWin32Window(g_win));
    initFonts(hdc);

    glfwSetKeyCallback(g_win,cbKey);
    glfwSetCharCallback(g_win,cbChar);
    glfwSetMouseButtonCallback(g_win,cbMBtn);
    glfwSetCursorPosCallback(g_win,cbMMove);
    glfwSetScrollCallback(g_win,cbScroll);
    glfwSetFramebufferSizeCallback(g_win,cbFB);

    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH); glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0); glEnable(GL_LIGHT1); glEnable(GL_LIGHT2);
    float l0p[]={5,10,7,1},  l0d[]={0.6f,0.55f,1,1}, l0a[]={0.08f,0.08f,0.15f,1};
    float l1p[]={-5,3,-4,1}, l1d[]={0.1f,0.7f,0.6f,1};
    float l2p[]={0,-5,8,1},  l2d[]={0.3f,0.25f,0.5f,1};
    glLightfv(GL_LIGHT0,GL_POSITION,l0p); glLightfv(GL_LIGHT0,GL_DIFFUSE,l0d); glLightfv(GL_LIGHT0,GL_AMBIENT,l0a);
    glLightfv(GL_LIGHT1,GL_POSITION,l1p); glLightfv(GL_LIGHT1,GL_DIFFUSE,l1d);
    glLightfv(GL_LIGHT2,GL_POSITION,l2p); glLightfv(GL_LIGHT2,GL_DIFFUSE,l2d);

    g_q=gluNewQuadric(); gluQuadricNormals(g_q,GLU_SMOOTH);

    initEquations();
    loadEq(0);
    rebuildSliders();

    printf("PhysicsSim 3D started.\n");
    printf("  Mouse drag (3D)  : rotate   |  Scroll : zoom\n");
    printf("  Click sidebar    : select equation\n");
    printf("  Arrow Up/Down    : cycle equations\n");
    printf("  Drag sliders     : change variables (animation never stops)\n");
    printf("  Type in box + Enter/RUN : custom equation  e.g.  F = m * a\n");
    printf("  ESC              : quit\n");

    /* ── MAIN LOOP ──
       g_time ticks every frame.
       Scenes are pure functions of g_time + g_vals.
       Slider drag updates g_vals[sym] only.
       Scene reads it next frame. Zero interruption. */
    while(!glfwWindowShouldClose(g_win)){
        g_time=glfwGetTime();
        int fw,fh; glfwGetFramebufferSize(g_win,&fw,&fh);
        WIN_W=fw; WIN_H=std::max(fh,400);

        // 3D scene viewport
        glViewport(CANVAS_X, CT_H, CANVAS_W, CANVAS_H);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        gluPerspective(50.0,(double)CANVAS_W/std::max(1,CANVAS_H),0.01,200);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        double cx=g_camDist*sin(g_camTheta)*cos(g_camPhi);
        double cy=g_camDist*sin(g_camPhi);
        double cz=g_camDist*cos(g_camTheta)*cos(g_camPhi);
        gluLookAt(cx,cy,cz, 0,0,0, 0,1,0);

        glClearColor(BG0.r,BG0.g,BG0.b,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        drawGrid();

        // pure stateless scene call — reads g_vals + g_time, never stops
        glPushMatrix();
        drawCurrentScene();
        glPopMatrix();

        // 2D overlay
        glViewport(0,0,WIN_W,WIN_H);
        drawUI();

        glfwSwapBuffers(g_win);
        glfwPollEvents();
    }

    gluDeleteQuadric(g_q);
    glfwDestroyWindow(g_win);
    glfwTerminate();
    return 0;
}
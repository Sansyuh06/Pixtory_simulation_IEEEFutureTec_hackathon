/*
  PixelFactory  -  Turning Invisible Forces Into Interactive Experience
  IEEE Future Tech Explorers Challenge  |  Sri Sairam Institute of Technology
  Native OpenGL  |  GLFW  |  WGL bitmap fonts  |  MinGW-w64 -> Windows x64

  BUILD COMMAND (run from project root on Linux):
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -mwindows \
      -I libs/include -I /usr/x86_64-w64-mingw32/include \
      src/main.cpp \
      -L libs/win64 -L /usr/x86_64-w64-mingw32/lib \
      -lglfw3dll -lopengl32 -lglu32 -lgdi32 -luser32 -lkernel32 -lwinhttp \
      -o build/PhysicsSim3D.exe

  KEY DESIGN: Every scene is a PURE DRAW FUNCTION.
  It reads g_vals + g_time each frame and draws directly.
  No object lists. No rebuild on slider change.
  Sliders write into g_vals[sym]. The next frame picks it up.
  Animation NEVER stops when you move sliders.

  CONTROLS:
    Mouse drag (3D view)  : Orbit / rotate (full spherical — all angles)
    Scroll wheel          : Zoom in / out
    Click sidebar         : Switch simulation
    Arrow Up / Down       : Cycle equations
    Drag sliders          : Change variable values (animation never stops)
    Type in prompt box    : Enter a custom equation
    Enter or click RUN    : Simulate custom equation
    ESC                   : Quit
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
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
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ─────────────────────────────────────────────────────────
   LAYOUT
───────────────────────────────────────────────────────── */
static int WIN_W      = 1280;
static int WIN_H      = 800;
static const int SB_W   = 340;   // left sidebar width
static const int TB_H   = 36;    // topbar height
static const int CT_H   = 160;   // controls panel height
static const int INFO_W = 380;   // right info panel width

static bool g_leftCollapsed = false;
static bool g_rightCollapsed = false;

inline int canvasX() { return g_leftCollapsed ? 0 : SB_W; }
inline int canvasW() { return WIN_W - (g_leftCollapsed ? 0 : SB_W) - (g_rightCollapsed ? 0 : INFO_W); }
inline int canvasH() { return WIN_H - TB_H - CT_H; }

#define CANVAS_X  (canvasX())
#define CANVAS_W  (canvasW())
#define CANVAS_H  (canvasH())

/* ─────────────────────────────────────────────────────────
   COLOURS
───────────────────────────────────────────────────────── */
struct C3 { float r, g, b; };
#define MKRGB(r,g,b) C3{(r)/255.f,(g)/255.f,(b)/255.f}

static C3 BG0     = MKRGB(13,  17,  23);  // Dark Navy Background
static C3 BG1     = MKRGB(22,  27,  34);  // Gray-navy sidebar
static C3 BG2     = MKRGB(33,  38,  45);  // Medium gray-navy
static C3 BG3     = MKRGB(48,  54,  61);  // Light gray-navy
static C3 ACCENT  = MKRGB(0,   229, 255); // Bright Cyan
static C3 ACCENT2 = MKRGB(128, 240, 255); // Light Cyan
static C3 ACCENT3 = MKRGB(0,   206, 201); // Teal
static C3 HOT     = MKRGB(253, 121, 168); // Pink/Red
static C3 TEXT0   = MKRGB(240, 246, 252); // White-gray text
static C3 TEXT1   = MKRGB(139, 148, 158); // Muted gray text
static C3 TEXT2   = MKRGB(110, 118, 129); // Darker muted text
static C3 BORDER  = MKRGB(48,  54,  61);  // Border gray
static C3 CBLUE   = MKRGB(0,   140, 255); // Nice Blue
static C3 CORANGE = MKRGB(255, 120, 0);   // Nice Orange
static C3 CGREEN  = MKRGB(0,   220, 100); // Nice Green
static C3 CYELLOW = MKRGB(255, 230, 0);   // Bright Yellow
static C3 CRED    = MKRGB(255, 51,   51);
static C3 CCYAN   = MKRGB(0,   229, 255);
static C3 CGRID   = MKRGB(28,  33,  40);  // Dark Grid

inline void gc(C3 c, float a=1.f){ glColor4f(c.r,c.g,c.b,a); }

/* ─────────────────────────────────────────────────────────
   GLOBAL STATE  (scenes read these, never write them)
───────────────────────────────────────────────────────── */
static double g_time     = 0.0;                    // always ticking
static std::map<std::string,double> g_vals;         // current slider values
static int    g_selIdx   = 0;                       // -1 = custom equation
static double g_camTheta = 0.6, g_camPhi = 0.4, g_camDist = 11.0;
static double g_camThetaT = 0.6, g_camPhiT = 0.4, g_camDistT = 11.0; // camera targets for interpolation
static bool   g_drag     = false;
static double g_lmx = 0, g_lmy = 0;
// Accumulated raw orbit deltas — allows dragging in any direction freely
static double g_orbitX = 0.6;  // horizontal angle (azimuth)
static double g_orbitY = 0.4;  // vertical angle (elevation)  clamped [-1.45, 1.45]
static GLFWwindow* g_win = nullptr; // global window pointer
static GLUquadric* g_q   = nullptr;
static int    g_predictChoice = -1;
static bool   g_predictAnswered = false;
static int    g_learningXP = 0;

/* ─────────────────────────────────────────────────────────
   AI TUTOR CHATBOT  (Ollama LLM integration)
───────────────────────────────────────────────────────── */
struct ChatMessage {
    bool isUser;          // true = student question, false = AI response
    double timestamp;
    std::string text;
};

static std::vector<ChatMessage> g_chatMessages;
static std::string g_chatInput;                 // current chat typing buffer
static bool        g_chatInputActive = false;   // is the chat input focused
static std::atomic<bool> g_chatLoading{false};  // AI is generating
static std::mutex  g_chatMutex;                 // protects g_chatMessages
static float       g_infoPanelScroll = 0.f;     // right panel scroll offset
static bool        g_ollamaAvailable = true;    // assume available until first fail
static std::string g_ollamaModel = "phi3:mini"; // default model

// Forward declarations
std::string buildSystemPrompt();

// Simple JSON string extractor (no library needed)
static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t start = json.find(needle);
    if (start == std::string::npos) {
        needle = "\"" + key + "\": \"";
        start = json.find(needle);
        if (start == std::string::npos) return "";
    }
    start += needle.size();
    std::string result;
    bool escaped = false;
    for (size_t i = start; i < json.size(); i++) {
        char c = json[i];
        if (escaped) {
            if (c == 'n') result += '\n';
            else if (c == 't') result += '\t';
            else if (c == '\\') result += '\\';
            else if (c == '"') result += '"';
            else result += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

// Call Ollama API in background thread using WinHTTP
static void ollamaQuery(const std::string& userQuestion, const std::string& systemPrompt) {
    g_chatLoading.store(true);

    auto escapeJson = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    };

    std::string body = "{\"model\":\"" + g_ollamaModel + "\","
                       "\"prompt\":\"" + escapeJson(userQuestion) + "\","
                       "\"system\":\"" + escapeJson(systemPrompt) + "\","
                       "\"stream\":false,"
                       "\"options\":{\"temperature\":0.7,\"num_predict\":256}}";

    std::string responseText;

    HINTERNET hSession = WinHttpOpen(L"PixelFactory/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"localhost", 11434, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                L"POST", L"/api/generate",
                NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (hRequest) {
                WinHttpSetTimeouts(hRequest, 5000, 5000, 30000, 60000);
                LPCWSTR headers = L"Content-Type: application/json";
                BOOL bResult = WinHttpSendRequest(hRequest,
                    headers, (DWORD)-1L,
                    (LPVOID)body.c_str(), (DWORD)body.size(),
                    (DWORD)body.size(), 0);
                if (bResult) {
                    bResult = WinHttpReceiveResponse(hRequest, NULL);
                    if (bResult) {
                        DWORD dwSize = 0;
                        std::string fullResponse;
                        do {
                            dwSize = 0;
                            WinHttpQueryDataAvailable(hRequest, &dwSize);
                            if (dwSize > 0) {
                                std::vector<char> buf(dwSize + 1, 0);
                                DWORD dwRead = 0;
                                WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead);
                                fullResponse.append(buf.data(), dwRead);
                            }
                        } while (dwSize > 0);
                        responseText = extractJsonString(fullResponse, "response");
                        if (responseText.empty()) {
                            std::string err = extractJsonString(fullResponse, "error");
                            if (!err.empty()) responseText = "Error: " + err;
                            else responseText = "Empty response from Ollama.";
                        }
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }

    if (responseText.empty()) {
        responseText = "Cannot connect to Ollama. Run 'ollama serve' and 'ollama pull " + g_ollamaModel + "'.";
        g_ollamaAvailable = false;
    } else {
        g_ollamaAvailable = true;
    }

    // Trim whitespace
    while (!responseText.empty() && (responseText.front() == ' ' || responseText.front() == '\n'))
        responseText.erase(responseText.begin());
    while (!responseText.empty() && (responseText.back() == ' ' || responseText.back() == '\n'))
        responseText.pop_back();

    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        g_chatMessages.push_back({false, g_time, responseText});
    }
    g_chatLoading.store(false);
}

static void sendChatMessage() {
    if (g_chatInput.empty() || g_chatLoading.load()) return;
    std::string question = g_chatInput;
    g_chatInput.clear();
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        g_chatMessages.push_back({true, g_time, question});
    }
    std::string sysPrompt = buildSystemPrompt();
    std::thread(ollamaQuery, question, sysPrompt).detach();
}

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

struct Category {
    std::string name;
    std::vector<int> eqIndices;
    bool expanded = false;
};

static std::vector<Equation> g_eqs;
static std::vector<Category> g_categories;
static Equation               g_custom;
static bool                   g_hasCustom = false;

Equation& curEq(){
    return (g_selIdx == -1 && g_hasCustom) ? g_custom : g_eqs[g_selIdx];
}

void setDefaultCamera(const std::string& viz) {
    if (viz == "fcc" || viz == "bcc" || viz == "lattice") {
        g_camThetaT = 0.78; g_camPhiT = 0.5; g_camDistT = 8.5;
    } else if (viz == "torsion") {
        g_camThetaT = 0.5; g_camPhiT = 0.25; g_camDistT = 10.5;
    } else if (viz == "cantilever") {
        g_camThetaT = 0.2; g_camPhiT = 0.15; g_camDistT = 11.0;
    } else if (viz == "projectile") {
        g_camThetaT = 0.3; g_camPhiT = 0.25; g_camDistT = 13.0;
    } else if (viz == "circular") {
        g_camThetaT = 0.1; g_camPhiT = 0.8; g_camDistT = 12.0;
    } else if (viz == "spring") {
        g_camThetaT = 0.3; g_camPhiT = 0.2; g_camDistT = 12.0;
    } else if (viz == "pendulum") {
        g_camThetaT = 0.0; g_camPhiT = 0.05; g_camDistT = 11.0;
    } else if (viz == "electric") {
        g_camThetaT = 0.1; g_camPhiT = 0.4; g_camDistT = 11.0;
    } else if (viz == "magfield") {
        g_camThetaT = 0.4; g_camPhiT = 0.55; g_camDistT = 12.0;
    } else if (viz == "vectorlab" || viz == "interference" || viz == "optics" || viz == "decay" || viz == "workpower" || viz == "pressure") {
        g_camThetaT = 0.0; g_camPhiT = 0.0; g_camDistT = 12.5;
    } else if (viz == "circuitlab" || viz == "aclab") {
        g_camThetaT = 0.0; g_camPhiT = 0.1; g_camDistT = 12.0;
    } else {
        // 2D and other front-facing simulations
        g_camThetaT = 0.0; g_camPhiT = 0.0; g_camDistT = 14.0;
    }
    // Sync orbit accumulators so next drag starts from the right position
    g_orbitX = g_camThetaT;
    g_orbitY = g_camPhiT;
}

void loadEq(int i){
    g_selIdx = i;
    g_predictChoice = -1;
    g_predictAnswered = false;
    g_vals.clear();
    for(auto& v : curEq().vars) g_vals[v.sym] = v.val;
    setDefaultCamera(curEq().viz);
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
   WGL BITMAP FONT SYSTEM  (Unicode wide-character support)
───────────────────────────────────────────────────────── */
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Slot 0-3: Segoe UI Bold (heading font) at 14,16,18,28px
// Slot 4-7: Consolas (body/mono font) at 14,16,18,20px
static GLuint g_fontBase[8] = {0,0,0,0,0,0,0,0};

void initFonts(HDC hdc){
    // --- Heading font: Segoe UI Bold ---
    int hHeights[] = {-14, -16, -18, -28};
    const wchar_t* hFace = L"Segoe UI";
    for(int i = 0; i < 4; i++){
        g_fontBase[i] = glGenLists(10000);
        HFONT hf = CreateFontW(hHeights[i], 0, 0, 0,
            FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FF_DONTCARE|DEFAULT_PITCH, hFace);
        HFONT old = (HFONT)SelectObject(hdc, hf);
        wglUseFontBitmapsW(hdc, 32, 224, g_fontBase[i] + 32);
        wglUseFontBitmapsW(hdc, 0x0370, 144, g_fontBase[i] + 0x0370);
        wglUseFontBitmapsW(hdc, 0x2070, 48,  g_fontBase[i] + 0x2070);
        wglUseFontBitmapsW(hdc, 0x2200, 256, g_fontBase[i] + 0x2200);
        wglUseFontBitmapsW(hdc, 0x2190, 32,  g_fontBase[i] + 0x2190);
        SelectObject(hdc, old); DeleteObject(hf);
    }
    // --- Body font: Consolas Regular ---
    int bHeights[] = {-13, -15, -17, -19};
    const wchar_t* bFace = L"Consolas";
    for(int i = 0; i < 4; i++){
        g_fontBase[4+i] = glGenLists(10000);
        HFONT hf = CreateFontW(bHeights[i], 0, 0, 0,
            FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FF_DONTCARE|DEFAULT_PITCH, bFace);
        HFONT old = (HFONT)SelectObject(hdc, hf);
        wglUseFontBitmapsW(hdc, 32, 224, g_fontBase[4+i] + 32);
        wglUseFontBitmapsW(hdc, 0x2022, 1,  g_fontBase[4+i] + 0x2022); // bullet •
        SelectObject(hdc, old); DeleteObject(hf);
    }
}

// fontIdx: sz maps to slot 0-3 (Segoe UI Bold)
int fontIdx(int sz){
    if(sz <= 14) return 0;
    if(sz <= 16) return 1;
    if(sz <= 18) return 2;
    return 3;              // 28px
}
// bodyFontIdx: maps to slot 4-7 (Consolas)
int bodyFontIdx(int sz){
    if(sz <= 13) return 4;
    if(sz <= 15) return 5;
    if(sz <= 17) return 6;
    return 7;
}

void drawCharW(int sz, wchar_t wc){
    if(wc < 32 || wc >= 10000) return;
    int idx = fontIdx(sz);
    glCallList(g_fontBase[idx] + wc);
}

void txt(float x, float y, const std::string& s, int sz, C3 col, float a=1.f){
    gc(col, a);
    glRasterPos2f(x, y);
    std::wstring ws = utf8_to_wstring(s);
    for(wchar_t wc : ws) drawCharW(sz, wc);
}

// 3D text helper for axes and labels
void txt3D(float x, float y, float z, const std::string& s, int sz, C3 col, float a=1.f){
    gc(col, a);
    glRasterPos3f(x, y, z);
    std::wstring ws = utf8_to_wstring(s);
    for(wchar_t wc : ws) drawCharW(sz, wc);
}

void drawWrappedText(float x, float y, const std::string& text, int width, int fontSize, C3 color, float alpha = 1.0f) {
    int charWidth = (fontSize <= 16) ? 9 : ((fontSize <= 18) ? 10 : 12);
    int maxCharsPerLine = width / charWidth;
    if (maxCharsPerLine < 10) maxCharsPerLine = 10;

    std::wstring wtext = utf8_to_wstring(text);
    std::vector<std::wstring> lines;
    std::wstring currentLine = L"";
    std::wstring word = L"";
    
    for (size_t i = 0; i < wtext.size(); i++) {
        wchar_t c = wtext[i];
        if (c == L'\n') {
            currentLine += word;
            lines.push_back(currentLine);
            currentLine = L"";
            word = L"";
        } else if (c == L' ') {
            if (currentLine.size() + word.size() + 1 > (size_t)maxCharsPerLine) {
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                    currentLine = word;
                } else {
                    lines.push_back(word);
                    currentLine = L"";
                }
            } else {
                if (!currentLine.empty()) currentLine += L" ";
                currentLine += word;
            }
            word = L"";
        } else {
            word += c;
        }
    }
    if (!word.empty() || !currentLine.empty()) {
        if (currentLine.size() + word.size() + 1 > (size_t)maxCharsPerLine && !currentLine.empty()) {
            lines.push_back(currentLine);
            lines.push_back(word);
        } else {
            if (!currentLine.empty()) currentLine += L" ";
            currentLine += word;
            lines.push_back(currentLine);
        }
    }

    float currentY = y;
    int lineHeight = fontSize + 4;
    for (const auto& line : lines) {
        gc(color, alpha);
        glRasterPos2f(x, currentY);
        for (wchar_t wc : line) drawCharW(fontSize, wc);
        currentY += lineHeight;
    }
}

// ── Body text using Consolas (slots 4-7) ─────────────────────────────────
void drawBodyCharW(int sz, wchar_t wc){
    if(wc < 32 || wc >= 10000) return;
    int idx = bodyFontIdx(sz);
    glCallList(g_fontBase[idx] + wc);
}

void txtBody(float x, float y, const std::string& s, int sz, C3 col, float a=1.f){
    gc(col, a);
    glRasterPos2f(x, y);
    std::wstring ws = utf8_to_wstring(s);
    for(wchar_t wc : ws) drawBodyCharW(sz, wc);
}

void drawWrappedTextBody(float x, float y, const std::string& text, int width,
                         int fontSize, C3 color, float alpha = 1.0f) {
    // Consolas is monospaced: each char ~8px at 15px, ~9px at 17px
    int charWidth = (fontSize <= 13) ? 8 : (fontSize <= 15) ? 9 : 10;
    int maxCharsPerLine = std::max(10, width / charWidth);

    std::wstring wtext = utf8_to_wstring(text);
    std::vector<std::wstring> lines;
    std::wstring currentLine = L"";
    std::wstring word = L"";

    for (size_t i = 0; i < wtext.size(); i++) {
        wchar_t c = wtext[i];
        if (c == L'\n') {
            currentLine += word; lines.push_back(currentLine);
            currentLine = L""; word = L"";
        } else if (c == L' ') {
            if (currentLine.size() + word.size() + 1 > (size_t)maxCharsPerLine) {
                if (!currentLine.empty()) { lines.push_back(currentLine); currentLine = word; }
                else { lines.push_back(word); currentLine = L""; }
            } else {
                if (!currentLine.empty()) currentLine += L" ";
                currentLine += word;
            }
            word = L"";
        } else { word += c; }
    }
    if (!word.empty() || !currentLine.empty()) {
        if (currentLine.size() + word.size() + 1 > (size_t)maxCharsPerLine && !currentLine.empty()) {
            lines.push_back(currentLine); lines.push_back(word);
        } else {
            if (!currentLine.empty()) currentLine += L" ";
            currentLine += word; lines.push_back(currentLine);
        }
    }
    float curY = y;
    int lh = fontSize + 6; // generous leading
    for (const auto& line : lines) {
        gc(color, alpha);
        glRasterPos2f(x, curY);
        for (wchar_t wc : line) drawBodyCharW(fontSize, wc);
        curY += lh;
    }
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

void arrow(float ox, float oy, float oz,
           float dx, float dy, float dz,
           float len, C3 col, float alpha)
{
    float mag = sqrtf(dx*dx+dy*dy+dz*dz);
    if(mag < 1e-9f) return;
    dx/=mag; dy/=mag; dz/=mag;
    float ex=ox+dx*len, ey=oy+dy*len, ez=oz+dz*len;
    gc(col, alpha);
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
    // Draw FCC bonds (lines from each face center to its four adjacent corner atoms)
    glDisable(GL_LIGHTING);
    glLineWidth(1.5f);
    gc(ACCENT2, 0.4f);
    glBegin(GL_LINES);
    for(auto& corner : c8) {
        if (corner[2] == -h) { glVertex3f(0,0,-h); glVertex3fv(corner); }
        if (corner[2] == h)  { glVertex3f(0,0,h);  glVertex3fv(corner); }
        if (corner[0] == -h) { glVertex3f(-h,0,0); glVertex3fv(corner); }
        if (corner[0] == h)  { glVertex3f(h,0,0);  glVertex3fv(corner); }
        if (corner[1] == -h) { glVertex3f(0,-h,0); glVertex3fv(corner); }
        if (corner[1] == h)  { glVertex3f(0,h,0);  glVertex3fv(corner); }
    }
    glEnd();
    glEnable(GL_LIGHTING);

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

    // Draw BCC bonds (lines from body center to all 8 corners)
    glDisable(GL_LIGHTING);
    glLineWidth(1.5f);
    gc(ACCENT2, 0.5f);
    glBegin(GL_LINES);
    for(auto& p : c8) {
        glVertex3f(0,0,0);
        glVertex3fv(p);
    }
    glEnd();
    glEnable(GL_LIGHTING);

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
    // Core beam
    glColor4f(1,0.15f,0.05f,intensity);
    glPushMatrix(); glRotatef(90,0,0,1); glTranslatef(0,0,-3.9f); drawCyl(br,br,7.8f,16); glPopMatrix();
    // Volumetric glow
    glColor4f(1,0.15f,0.05f,intensity * 0.22f);
    glPushMatrix(); glRotatef(90,0,0,1); glTranslatef(0,0,-3.9f); drawCyl(br * 2.5f,br * 2.5f,7.8f,16); glPopMatrix();
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
    // Core ray
    glColor4f(0.27f,0.67f,1,0.30f);
    glPushMatrix(); glRotatef(90,0,1,0); glTranslatef(0,0,-7); drawCyl(0.3f,0.3f,14,24); glPopMatrix();
    // Core glow
    glColor4f(0.27f,0.67f,1,0.12f);
    glPushMatrix(); glRotatef(90,0,1,0); glTranslatef(0,0,-7); drawCyl(0.6f,0.6f,14,24); glPopMatrix();
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
    float t = T() * 4.f;

    // Propagation direction: +X axis
    // E-field wave in X-Y plane (Cyan)
    glDisable(GL_LIGHTING);
    gc(ACCENT, 0.9f); glLineWidth(2.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=120; i++){
        float x = -6.f + i/120.f*12.f;
        float y = 1.5f * sinf(freq * x - t);
        glVertex3f(x, y, 0.f);
    }
    glEnd();

    // B-field wave in X-Z plane (Yellow)
    gc(CYELLOW, 0.9f); glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=120; i++){
        float x = -6.f + i/120.f*12.f;
        float z = 1.5f * sinf(freq * x - t);
        glVertex3f(x, 0.f, z);
    }
    glEnd();

    // Vector arrows from X-axis
    glLineWidth(1.f);
    for(int i=0; i<=24; i++){
        float x = -6.f + i/24.f*12.f;
        float y = 1.5f * sinf(freq * x - t);
        float z = 1.5f * sinf(freq * x - t);
        // E-field vector (vertical, red)
        arrow(x, 0.f, 0.f, 0.f, y, 0.f, fabs(y), CRED);
        // B-field vector (horizontal, blue)
        arrow(x, 0.f, 0.f, 0.f, 0.f, z, fabs(z), CBLUE);
    }

    // Axis propagation line
    gc(TEXT0); glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex3f(-6.5f, 0.f, 0.f); glVertex3f(6.5f, 0.f, 0.f);
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);

    // Propagation vector
    arrow(6.5f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.2f, TEXT0);
    txt3D(7.5f, 0.3f, 0.f, "k", 16, TEXT0);
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
    double n1=V("n1",1.0), n2=V("n2",1.5);
    double theta1 = g_vals.count("theta1") ? V("theta1", 45.0) : (g_vals.count("t1") ? V("t1", 45.0) : 45.0);
    double s = n1 * sin(theta1 * M_PI / 180.0) / n2;
    double theta2 = s <= 1.0 ? asin(s) : M_PI / 2.0;
    bool tir = (s > 1.0);

    // Medium 2 block (glass) in 3D (y < 0)
    glColor4f(0.f, 0.5f, 1.f, 0.15f);
    glPushMatrix();
    glTranslatef(0.f, -2.5f, 0.f);
    drawBox(14.f, 5.f, 4.f);
    glPopMatrix();

    // Normal line through the interface (vertical along Y-axis)
    glDisable(GL_LIGHTING);
    gc(TEXT2, 0.6f);
    glLineWidth(1.f);
    glBegin(GL_LINES);
    glVertex3f(0.f, -4.5f, 0.f); glVertex3f(0.f, 4.5f, 0.f);
    glEnd();
    glEnable(GL_LIGHTING);

    float t1_rad = (float)(theta1 * M_PI / 180.f);
    float t2_rad = (float)theta2;

    // Incident Ray (pointing to 0,0,0)
    float ix = -sinf(t1_rad) * 5.f;
    float iy = cosf(t1_rad) * 5.f;
    arrow(ix, iy, 0.f, sinf(t1_rad), -cosf(t1_rad), 0.f, 5.f, CBLUE);

    if (tir) {
        // Total Internal Reflection: Reflected ray goes up-right
        arrow(0.f, 0.f, 0.f, sinf(t1_rad), cosf(t1_rad), 0.f, 5.f, CRED);
        gc(HOT);
        txt3D(0.5f, 2.0f, 0.f, "T.I.R.", 18, HOT);
    } else {
        // Refracted Ray goes down-right
        arrow(0.f, 0.f, 0.f, sinf(t2_rad), -cosf(t2_rad), 0.f, 5.f, CORANGE);
        // Faint reflected ray
        arrow(0.f, 0.f, 0.f, sinf(t1_rad), cosf(t1_rad), 0.f, 5.f, CBLUE, 0.35f);
    }

    // Angle arcs
    glDisable(GL_LIGHTING);
    gc(CBLUE, 0.4f);
    glLineWidth(1.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=20; i++){
        float ang = i / 20.f * t1_rad;
        glVertex3f(-sinf(ang) * 1.5f, cosf(ang) * 1.5f, 0.f);
    }
    glEnd();

    if (!tir) {
        gc(CORANGE, 0.4f);
        glBegin(GL_LINE_STRIP);
        for(int i=0; i<=20; i++){
            float ang = i / 20.f * t2_rad;
            glVertex3f(sinf(ang) * 1.5f, -cosf(ang) * 1.5f, 0.f);
        }
        glEnd();
    }
    glEnable(GL_LIGHTING);

    // Refractive index labels
    txt3D(-5.f, 0.5f, 0.5f, "n1 = " + fmt(n1), 16, CBLUE);
    txt3D(5.f, -1.0f, 0.5f, "n2 = " + fmt(n2), 16, CORANGE);
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

void scenePressure(){
    double F=V("F",V("stress",200)), A=V("A",0.8);
    double P=F/std::max(A,1e-9);
    float plateW=(float)std::max(1.0,std::min(5.5,A*1.7));
    float crush=(float)std::min(1.7,P/350.0);
    gc(BG3,0.9f);
    glPushMatrix(); glTranslatef(0,-2.7f,0); drawBox(7.0f,0.25f,4.0f); glPopMatrix();
    gc(ACCENT2,0.9f);
    glPushMatrix(); glTranslatef(0,-1.4f-crush*0.15f,0); drawBox(plateW,0.28f,2.4f); glPopMatrix();
    arrow(0,2.7f,0, 0,-1,0, std::min(4.2f,1.0f+(float)F/180.f), CRED);
    glDisable(GL_LIGHTING);
    gc(CYELLOW,0.35f);
    glBegin(GL_QUADS);
    glVertex3f(-plateW/2,-1.22f,1.3f); glVertex3f( plateW/2,-1.22f,1.3f);
    glVertex3f( plateW/2,-1.22f,-1.3f); glVertex3f(-plateW/2,-1.22f,-1.3f);
    glEnd();
    glEnable(GL_LIGHTING);
    txt3D(-3.2f,1.7f,0,"Force spreads over area",16,TEXT0);
    txt3D(-3.2f,1.1f,0,"Pressure rises when area shrinks",16,ACCENT2);
}

void sceneWorkPower(){
    double F=V("F",20), s=V("s",V("d",10)), theta=V("theta",0), Wv=V("W",200), tv=V("t",5);
    if(curEq().id=="kin_speed"){
        double d=V("d",50), t=V("t",5);
        double speed=d/std::max(t,0.1);
        float dist=(float)std::min(8.0,std::max(0.8,d*0.04));
        float progress=(float)std::min(1.0,std::max(0.0,t/20.0));
        float bx=-4.0f+dist*progress;
        gc(BG3,0.8f);
        glPushMatrix(); glTranslatef(0,-2.2f,0); drawBox(10,0.12f,2.2f); glPopMatrix();
        glDisable(GL_LIGHTING);
        gc(ACCENT2,0.55f); glLineWidth(2.f);
        glBegin(GL_LINES); glVertex3f(-4,-1.55f,0); glVertex3f(-4+dist,-1.55f,0); glEnd();
        glLineWidth(1.f);
        glEnable(GL_LIGHTING);
        gc(CORANGE);
        glPushMatrix(); glTranslatef(bx,-1.18f,0); drawBox(0.9f,0.75f,0.9f); glPopMatrix();
        arrow(bx+0.55f,-1.18f,0,1,0,0,std::min(3.5f,0.7f+(float)speed*0.15f),CGREEN);
        txt3D(-4.7f,1.6f,0,"distance / time = speed",16,TEXT0);
        txt3D(-4.7f,1.0f,0,"Longer time lowers speed",16,ACCENT2);
        txt3D(2.0f,-1.0f,0,"speed " + fmt(speed) + " m/s",16,CYELLOW);
        return;
    }
    float ang=(float)(theta*M_PI/180.0);
    float dist=(float)std::min(6.0,std::max(0.6,s*0.14));
    float bx=-3.2f+fmodf(T()*0.45f,1.f)*dist;
    gc(BG3,0.8f);
    glPushMatrix(); glTranslatef(0,-2.2f,0); drawBox(10,0.12f,2.2f); glPopMatrix();
    gc(CBLUE);
    glPushMatrix(); glTranslatef(bx,-1.35f,0); drawBox(1.0f,1.0f,1.0f); glPopMatrix();
    arrow(bx+0.5f,-1.0f,0, cosf(ang),sinf(ang),0, std::min(3.8f,0.6f+(float)F/28.f), CRED);
    arrow(-3.2f,-2.0f,0, 1,0,0, dist, CGREEN);
    float rate=(float)(Wv/std::max(tv,0.1));
    gc(CYELLOW,0.85f);
    glPushMatrix(); glTranslatef(4.4f,-1.1f,0); drawCyl(0.55f,0.55f,std::min(3.0f,0.5f+rate/80.f),28); glPopMatrix();
    txt3D(-4.7f,1.6f,0,"Only force along motion does work",16,TEXT0);
    txt3D(3.4f,1.5f,0,"Power = work per second",16,CYELLOW);
}

void sceneVectorLab(){
    double A=V("A",5), B=V("B",5), theta=V("theta",60);
    float as=(float)std::min(4.0,A*0.22), bs=(float)std::min(4.0,B*0.22), th=(float)(theta*M_PI/180.0);
    float bx=bs*cosf(th), by=bs*sinf(th);
    arrow(0,0,0, 1,0,0, as, CBLUE);
    arrow(as,0,0, cosf(th),sinf(th),0, bs, CORANGE);
    arrow(0,0,0, as+bx,by,0, sqrtf((as+bx)*(as+bx)+by*by), CGREEN);
    arrow(0,0,0, cosf(th),sinf(th),0, bs, CORANGE,0.35f);
    glDisable(GL_LIGHTING);
    gc(TEXT2,0.6f); glLineWidth(1.f);
    glBegin(GL_LINES); glVertex3f(-5,0,0); glVertex3f(5,0,0); glVertex3f(0,-3,0); glVertex3f(0,3,0); glEnd();
    gc(CYELLOW,0.45f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=32;i++){float a=th*i/32.f; glVertex3f(cosf(a)*1.1f,sinf(a)*1.1f,0);}
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    txt3D(as*0.5f,-0.45f,0,"A",16,CBLUE);
    txt3D(as+bx*0.5f,by*0.5f+0.25f,0,"B",16,CORANGE);
    txt3D((as+bx)*0.55f,by*0.55f+0.25f,0,"Result",16,CGREEN);
}

void sceneInterference(){
    double lam=V("lam",500), D=V("D",1.5), d=V("d",0.5);
    float spacing=(float)std::max(0.25,std::min(1.8,(lam*1e-9*D/std::max(d*1e-3,1e-9))*8500));
    glDisable(GL_LIGHTING);
    gc(TEXT2,0.75f); glLineWidth(2.f);
    glBegin(GL_LINES); glVertex3f(-5,-2.8f,0); glVertex3f(-5,2.8f,0); glVertex3f(4.8f,-3.0f,0); glVertex3f(4.8f,3.0f,0); glEnd();
    gc(ACCENT,0.35f); glLineWidth(1.f);
    for(int s=-1;s<=1;s+=2){
        for(int i=-4;i<=4;i++){
            glBegin(GL_LINE_STRIP);
            for(int j=0;j<=40;j++){
                float x=-4.9f+j/40.f*9.7f;
                float y=s*0.25f+i*spacing*(j/40.f);
                glVertex3f(x,y,0);
            }
            glEnd();
        }
    }
    for(int i=-7;i<=7;i++){
        float y=i*spacing;
        float bright=0.18f+0.72f*powf(cosf(i*(float)M_PI*0.5f),2);
        gc(i==0?CYELLOW:ACCENT2,bright);
        glBegin(GL_QUADS);
        glVertex3f(4.65f,y-0.045f,0); glVertex3f(5.0f,y-0.045f,0);
        glVertex3f(5.0f,y+0.045f,0); glVertex3f(4.65f,y+0.045f,0);
        glEnd();
    }
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    txt3D(-4.8f,3.15f,0,"Double slit",16,TEXT0);
    txt3D(3.6f,3.15f,0,"Bright fringes",16,CYELLOW);
}

void sceneCircuitLab(){
    double R1=V("R1",10), R2=V("R2",20), Q=V("Q",10), Vv=V("V",5);
    bool parallel=curEq().id=="c_parallel";
    bool capacitor=curEq().id=="e_cap";
    glDisable(GL_LIGHTING);
    gc(TEXT0); glLineWidth(2.f);
    glBegin(GL_LINES);
    glVertex3f(-5,1.5f,0); glVertex3f(5,1.5f,0);
    glVertex3f(5,1.5f,0); glVertex3f(5,-1.5f,0);
    glVertex3f(5,-1.5f,0); glVertex3f(-5,-1.5f,0);
    glVertex3f(-5,-1.5f,0); glVertex3f(-5,1.5f,0);
    glEnd();
    glEnable(GL_LIGHTING);
    gc(CBLUE); glPushMatrix(); glTranslatef(-5,0,0); drawBox(0.35f,1.3f,0.35f); glPopMatrix();
    if(capacitor){
        gc(ACCENT2);
        glPushMatrix(); glTranslatef(-0.25f,0,0); drawBox(0.08f,2.0f,0.6f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.25f,0,0); drawBox(0.08f,2.0f,0.6f); glPopMatrix();
        int dots=(int)std::min(20.0,std::max(4.0,Q/5.0));
        for(int i=0;i<dots;i++){ gc(i%2?CRED:CBLUE); glPushMatrix(); glTranslatef((i%2?-0.65f:0.65f),-0.85f+(i/2)*0.18f,0); drawSphere(0.045f); glPopMatrix(); }
        txt3D(-1.8f,2.2f,0,"Charge storage between plates",16,ACCENT2);
    }else{
        gc(CORANGE);
        glPushMatrix(); glTranslatef(parallel?-1.4f:0, parallel?0.7f:1.5f,0); drawBox(std::min(2.6f,0.8f+(float)R1/50.f),0.45f,0.45f); glPopMatrix();
        glPushMatrix(); glTranslatef(parallel?1.4f:0, parallel?-0.7f:-1.5f,0); drawBox(std::min(2.6f,0.8f+(float)R2/50.f),0.45f,0.45f); glPopMatrix();
        if(parallel) txt3D(-2.3f,2.4f,0,"Parallel adds paths, Req drops",16,CGREEN);
        else txt3D(-2.2f,2.4f,0,"Series adds resistance directly",16,CYELLOW);
    }
    double flow=capacitor?Vv:(20.0/std::max(R1+(parallel?0:R2),1.0));
    glDisable(GL_LIGHTING);
    gc(CYELLOW); glPointSize(6.f);
    glBegin(GL_POINTS);
    for(int i=0;i<10;i++){float p=fmodf(T()*(float)flow*0.15f+i/10.f,1.f); glVertex3f(-4.5f+p*9.0f,1.5f,0.1f);}
    glEnd();
    glPointSize(1.f);
    glEnable(GL_LIGHTING);
}

void sceneACLab(){
    double V0=V("V0",230), I0=V("I0",5), R=V("R",50), XL=V("XL",150), XC=V("XC",100);
    double Z=sqrt(R*R+(XL-XC)*(XL-XC));
    float t=T(), phase=(float)atan2(XL-XC,std::max(R,1e-9));
    glDisable(GL_LIGHTING);
    gc(TEXT2,0.55f); glLineWidth(1.f);
    glBegin(GL_LINES); glVertex3f(-6,0,0); glVertex3f(6,0,0); glVertex3f(0,-2.4f,0); glVertex3f(0,2.4f,0); glEnd();
    gc(CBLUE); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=160;i++){float x=-6+i/160.f*12; glVertex3f(x,1.2f*sinf(x*2.0f+t*3.0f),0);}
    glEnd();
    gc(CORANGE); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=160;i++){float x=-6+i/160.f*12; glVertex3f(x,1.2f*sinf(x*2.0f+t*3.0f-phase),0);}
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    arrow(0,0,0, cosf(t),sinf(t),0, std::min(3.5f,0.6f+(float)V0/100.f), CBLUE);
    arrow(0,0,0, cosf(t-phase),sinf(t-phase),0, std::min(3.0f,0.6f+(float)I0/5.f+(float)(200/std::max(Z,1.0))*0.2f), CORANGE);
    txt3D(-5.6f,2.5f,0,"Voltage and current can shift phase",16,TEXT0);
}

void sceneOpticsBench(){
    double u=V("u",-3), f=V("f",1.5), v=0;
    if(curEq().id=="o_lens") v=(u*f)/(u+f);
    else if(curEq().id=="o_mirror") v=(u*f)/(u-f);
    else v=V("v",3);
    float os=(float)std::max(-5.0,std::min(-0.7,u));
    float fs=(float)std::max(-3.5,std::min(3.5,f));
    float is=(float)std::max(-5.5,std::min(5.5,v));
    glDisable(GL_LIGHTING);
    gc(TEXT2,0.75f); glLineWidth(1.5f);
    glBegin(GL_LINES); glVertex3f(-6,0,0); glVertex3f(6,0,0); glEnd();
    gc(ACCENT2); glLineWidth(2.f);
    glBegin(GL_LINES); glVertex3f(0,-2.5f,0); glVertex3f(0,2.5f,0); glEnd();
    gc(CBLUE); glLineWidth(2.f);
    glBegin(GL_LINES); glVertex3f(os,0,0); glVertex3f(os,1.6f,0); glEnd();
    gc(CGREEN); glBegin(GL_LINES); glVertex3f(is,0,0); glVertex3f(is,(v<0?-1.2f:1.2f),0); glEnd();
    gc(CYELLOW,0.85f); glLineWidth(1.2f);
    glBegin(GL_LINES);
    glVertex3f(os,1.6f,0); glVertex3f(0,1.6f,0); glVertex3f(0,1.6f,0); glVertex3f(is,(v<0?-1.2f:1.2f),0);
    glVertex3f(os,1.6f,0); glVertex3f(0,0,0); glVertex3f(0,0,0); glVertex3f(is,(v<0?-1.2f:1.2f),0);
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    gc(CRED); glPushMatrix(); glTranslatef(fs,0,0); drawSphere(0.08f); glPopMatrix();
    gc(CRED); glPushMatrix(); glTranslatef(-fs,0,0); drawSphere(0.08f); glPopMatrix();
    txt3D(os-0.4f,1.95f,0,"Object",16,CBLUE);
    txt3D(is-0.35f,(v<0?-1.55f:1.55f),0,"Image",16,CGREEN);
}

void sceneDecay(){
    double N0=V("N0",1000), lam=V("lam",0.01), tv=V("t",100);
    float now=(float)std::max(0.0,tv);
    float axisT=(float)std::max(1.0, curEq().vars.size() >= 3 ? curEq().vars[2].mx : std::max(100.0,tv));
    glDisable(GL_LIGHTING);
    gc(TEXT2,0.6f); glLineWidth(1.f);
    glBegin(GL_LINES); glVertex3f(-5,-2.2f,0); glVertex3f(5,-2.2f,0); glVertex3f(-5,-2.2f,0); glVertex3f(-5,2.4f,0); glEnd();
    gc(ACCENT); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=120;i++){
        double tt=i/120.0*axisT;
        double n=N0*exp(-lam*tt);
        glVertex3f(-5+i/120.f*10,(float)(-2.2+4.4*n/std::max(N0,1.0)),0);
    }
    glEnd();
    float x=-5+std::min(1.0f,now/axisT)*10;
    float y=(float)(-2.2+4.4*(N0*exp(-lam*now))/std::max(N0,1.0));
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    gc(CYELLOW); glPushMatrix(); glTranslatef(x,y,0); drawSphere(0.16f); glPopMatrix();
    txt3D(-4.8f,2.75f,0,"Radioactive decay is exponential",16,ACCENT2);
    txt3D(1.2f,2.1f,0,"N(t) = " + fmt(N0*exp(-lam*now)),16,CYELLOW);
}

/* ─────────────────────────────────────────────────────────
   NEW SCENES  —  15–17 year old syllabus equations
───────────────────────────────────────────────────────── */

// Newton's 2nd Law  F = m * a  — shows mass block accelerating
void sceneNewton(){
    double m=V("m",2);
    double a = V("a",0);
    double F = g_vals.count("F") ? V("F",10) : m*a;
    if(!g_vals.count("a")) a = m > 1e-9 ? F/m : 0;
    double vNow = V("v", a*T());
    double impulse = V("dt",0.0) > 0 ? F*V("dt",0.5) : 0;
    float  t = T();
    // Ground
    glColor4f(0.1f,0.1f,0.25f,0.7f);
    glPushMatrix(); glTranslatef(0,-1.55f,0); drawBox(14,0.12f,3); glPopMatrix();
    // Block sliding
    float bx = fmodf(t*(float)std::max(0.2, fabs(a))*0.18f, 12.f) - 5.f;
    if(g_vals.count("v")) bx = fmodf(t*(float)fabs(vNow)*0.08f, 12.f) - 5.f;
    gc(CBLUE);
    glPushMatrix(); glTranslatef(bx,-0.7f,0); drawBox(1.4f,1.4f,1.4f); glPopMatrix();
    // Force arrow
    arrow(bx+0.7f,-0.7f,0, F>=0?1.f:-1.f,0,0, (float)std::min(fabs(F)*0.25,4.0), CRED);
    // Acceleration label dots (trail)
    glDisable(GL_LIGHTING);
    for(int i=1;i<=5;i++){
        float tx2 = bx - (float)i*0.38f*(float)a*0.05f;
        gc(ACCENT2, 0.6f-(float)i*0.1f);
        glPointSize(5.f-(float)i*0.6f);
        glBegin(GL_POINTS); glVertex3f(tx2,-0.7f,0); glEnd();
    }
    glPointSize(1.f);
    glEnable(GL_LIGHTING);
    if(g_vals.count("v")){
        txt3D(-5.2f,2.0f,0,"Momentum grows with mass and velocity",16,TEXT0);
        txt3D(1.8f,1.45f,0,"p = " + fmt(m*vNow),16,CYELLOW);
    } else if(impulse != 0) {
        txt3D(-5.2f,2.0f,0,"Impulse is force applied over time",16,TEXT0);
        txt3D(1.8f,1.45f,0,"J = " + fmt(impulse) + " N*s",16,CYELLOW);
    } else {
        txt3D(-5.2f,2.0f,0,"Mass resists acceleration",16,TEXT0);
        txt3D(1.8f,1.45f,0,"F = " + fmt(F) + " N",16,CYELLOW);
    }
}

// Kinematics  v = u + a*t  — shows velocity-time graph + moving body
void sceneKinematics(){
    double u=V("u",0);
    double t = V("t", fmod(T(), 6.0));
    double a = V("a",3);
    if(curEq().id=="kin_acc") a = (V("v",20)-u)/std::max(t,0.1);
    if(curEq().id=="kin_vel"){
        double s=V("s",20), tv=V("t",5);
        double vel=s/std::max(tv,0.1);
        float dist=(float)std::max(-8.0,std::min(8.0,s*0.08));
        float bx=-dist*0.5f + dist*(float)std::min(1.0,std::max(0.0,tv/20.0));
        glColor4f(0.08f,0.08f,0.22f,0.7f);
        glPushMatrix(); glTranslatef(0,-2.1f,0); drawBox(14,0.1f,2); glPopMatrix();
        glDisable(GL_LIGHTING);
        gc(ACCENT2,0.5f); glLineWidth(2.f);
        glBegin(GL_LINES); glVertex3f(-dist*0.5f,-1.7f,0); glVertex3f(dist*0.5f,-1.7f,0); glEnd();
        glLineWidth(1.f);
        glEnable(GL_LIGHTING);
        gc(CORANGE);
        glPushMatrix(); glTranslatef(bx,-1.25f,0); drawSphere(0.5f); glPopMatrix();
        arrow(bx,-1.25f,0, vel>=0?1.f:-1.f,0,0,std::min(3.5f,0.7f+(float)fabs(vel)*0.16f),CGREEN);
        txt3D(-5.2f,2.0f,0,"Velocity includes direction",16,TEXT0);
        txt3D(-5.2f,1.45f,0,"v = displacement / time",16,ACCENT2);
        txt3D(1.4f,1.45f,0,"v = " + fmt(vel) + " m/s",16,CYELLOW);
        return;
    }
    float tmax = g_vals.count("t") ? (float)std::max(1.0, curEq().vars.size() ? curEq().vars.back().mx : 10.0) : 6.f;
    if(curEq().id=="kin_v2" && g_vals.count("s")){
        double s=V("s",10);
        double v2=u*u+2*a*s;
        double vf = v2>=0 ? sqrt(v2) : 0;
        float dist=(float)std::min(8.0,std::max(0.4,s*0.08));
        float bx=-4.4f+dist;
        glColor4f(0.08f,0.08f,0.22f,0.7f);
        glPushMatrix(); glTranslatef(0,-2.1f,0); drawBox(14,0.1f,2); glPopMatrix();
        glDisable(GL_LIGHTING);
        gc(ACCENT2,0.45f); glLineWidth(2.f);
        glBegin(GL_LINES); glVertex3f(-4.4f,-1.7f,0); glVertex3f(bx,-1.7f,0); glEnd();
        glLineWidth(1.f);
        glEnable(GL_LIGHTING);
        gc(CORANGE);
        glPushMatrix(); glTranslatef(bx,-1.25f,0); drawSphere(0.5f); glPopMatrix();
        arrow(bx,-1.25f,0,1,0,0,std::min(3.5f,0.7f+(float)vf*0.08f),CGREEN);
        txt3D(-5.0f,1.8f,0,"Displacement sets the final speed",16,TEXT0);
        txt3D(-5.0f,1.2f,0,"v^2 = u^2 + 2as",16,ACCENT2);
        txt3D(1.2f,-0.65f,0,"v = " + fmt(vf) + " m/s",16,CYELLOW);
        return;
    }
    // Ground
    glColor4f(0.08f,0.08f,0.22f,0.7f);
    glPushMatrix(); glTranslatef(0,-2.1f,0); drawBox(14,0.1f,2); glPopMatrix();
    // Rolling ball with position x = u*t + 0.5*a*t^2
    float tt = (float)std::max(0.0, std::min((double)tmax, t));
    float bx = (float)(u*tt + 0.5*a*tt*tt)*0.3f - 5.f;
    bx = std::max(-6.5f, std::min(6.5f, bx));
    gc(CORANGE);
    glPushMatrix(); glTranslatef(bx,-1.35f,0);
    glRotatef(bx*30.f,0,0,-1);
    drawSphere(0.5f);
    glPopMatrix();
    // v-t graph axes
    glDisable(GL_LIGHTING);
    gc(TEXT2, 0.5f); glLineWidth(1.f);
    glBegin(GL_LINES);
    glVertex3f(-5,-2.f,0.2f); glVertex3f(5,-2.f,0.2f); // time axis
    glVertex3f(-5,-2.f,0.2f); glVertex3f(-5,2.f,0.2f); // v axis
    glEnd();
    // v-t line  v = u + a*t
    gc(CGREEN); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=60;i++){
        float ti = i/60.f*tmax;
        float vi = (float)(u + a*ti)*0.25f;
        glVertex3f(-5+i/60.f*10, -2.f+vi, 0.2f);
    }
    glEnd();
    // Current point
    float curV = (float)(u + a*tt)*0.25f;
    gc(CYELLOW);
    glPointSize(8.f);
    glBegin(GL_POINTS); glVertex3f(-5+tt/tmax*10,-2.f+curV,0.2f); glEnd();
    glPointSize(1.f);
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    arrow(bx,-1.35f,0, (float)(u+a*tt),0,0, std::min(3.5f,0.7f+fabsf((float)(u+a*tt))*0.08f), CGREEN);
    txt3D(-5.2f,2.35f,0,"Drag t to scrub the motion",16,TEXT0);
    txt3D(-5.2f,1.8f,0,"Position: s = ut + 0.5at^2",16,ACCENT2);
    txt3D(1.3f,1.8f,0,"v = " + fmt(u+a*tt) + " m/s",16,CYELLOW);
    txt3D(1.3f,1.25f,0,"s = " + fmt(u*tt+0.5*a*tt*tt) + " m",16,CGREEN);
}

// Projectile Motion  — parabolic trajectory
void sceneProjectile(){
    double u=V("u",15), ang=V("ang",45);
    if (g_vals.count("u") == 0 && g_vals.count("v") > 0) u = g_vals["v"];
    float ux=(float)(u*cos(ang*M_PI/180));
    float uy=(float)(u*sin(ang*M_PI/180));
    float g2=9.8f;
    float tFlight = 2.f*uy/g2;
    float t = T();
    float tt = fmodf(t, tFlight>0.1f?tFlight:1.f);
    // Ground
    glColor4f(0.05f,0.22f,0.05f,0.7f);
    glPushMatrix(); glTranslatef(0,-3.f,0); drawBox(14,0.08f,2); glPopMatrix();
    // Trajectory path
    glDisable(GL_LIGHTING);
    gc(ACCENT2, 0.5f); glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=80;i++){
        float ti = i/80.f*std::max(tFlight,0.1f);
        float px = ux*ti*0.3f - 5.f;
        float py = (uy*ti - 0.5f*g2*ti*ti)*0.3f - 3.f;
        glVertex3f(px,py,0);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    // Apex Marker: highest point at tApex = uy / g2
    float tApex = uy / g2;
    float ax = ux*tApex*0.3f - 5.f;
    float ay = (uy*tApex - 0.5f*g2*tApex*tApex)*0.3f - 3.f;
    glDisable(GL_LIGHTING);
    gc(CYELLOW, 0.5f);
    glLineWidth(1.f);
    glBegin(GL_LINES);
    glVertex3f(ax, -3.f, 0.f); glVertex3f(ax, ay, 0.f);
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    gc(CYELLOW);
    glPushMatrix(); glTranslatef(ax, ay, 0.f); drawSphere(0.12f); glPopMatrix();
    txt3D(ax - 0.5f, ay + 0.3f, 0.f, "APEX", 16, CYELLOW);
    // Range Marker at landing point
    float rx = ux*tFlight*0.3f - 5.f;
    gc(CRED);
    glPushMatrix(); glTranslatef(rx, -2.9f, 0.f); drawCyl(0.2f, 0.f, 0.4f, 12); glPopMatrix();
    txt3D(rx - 0.6f, -2.4f, 0.f, "RANGE", 16, CRED);
    // Ball
    float bx = ux*tt*0.3f - 5.f;
    float by = (uy*tt - 0.5f*g2*tt*tt)*0.3f - 3.f;
    gc(CORANGE);
    glPushMatrix(); glTranslatef(bx,by,0); drawSphere(0.28f); glPopMatrix();
    // Velocity vector at current ball position (tangent vector)
    float vx = ux;
    float vy = uy - g2*tt;
    float v_mag = sqrtf(vx*vx + vy*vy);
    if(v_mag > 0.1f){
        arrow(bx, by, 0.f, vx/v_mag, vy/v_mag, 0.f, 1.5f, CGREEN);
        txt3D(bx + vx/v_mag*1.5f, by + vy/v_mag*1.5f, 0.f, "v", 16, CGREEN);
    }
    // Launch arrow
    arrow(-5.f,-3.f,0, ux,uy,0, 1.2f, CGREEN);
}

// Simple Harmonic Motion  — spring-mass oscillation
void sceneSpring(){
    double k=V("k",10), m=V("m",1);
    double omega = sqrt(std::max(k,0.01)/std::max(m,0.01));
    double A=V("A",2);
    float t = (float)V("t", T());
    if(curEq().id=="f_kx"){
        A=fabs(V("x",2));
        t=0.f;
    }
    float x = (float)(A * cos(omega*t));
    if(curEq().id=="f_kx") x = (float)V("x",2);
    // Ceiling
    glColor4f(0.2f,0.2f,0.4f,0.9f);
    glPushMatrix(); glTranslatef(0,3.5f,0); drawBox(2,0.18f,1.5f); glPopMatrix();
    // 3D Coiled Spring Mesh
    int coils=16;
    float r=0.25f;
    glDisable(GL_LIGHTING);
    gc(ACCENT, 0.9f);
    glLineWidth(2.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=coils*24; i++){
        float theta = i * (2.f * (float)M_PI / 24.f);
        float fy = 3.5f - i / (float)(coils*24) * (3.5f - (x + 0.5f));
        float fx = r * cosf(theta);
        float fz = r * sinf(theta);
        glVertex3f(fx, fy, fz);
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    // Mass block
    gc(CBLUE);
    glPushMatrix(); glTranslatef(0,x-0.5f,0); drawBox(1.f,1.f,1.f); glPopMatrix();
    if(curEq().id=="f_kx"){
        arrow(0.f,x-0.5f,0, 0, x>=0?-1.f:1.f,0, std::min(3.5f,0.6f+(float)(k*fabs(V("x",2)))*0.05f), CRED);
        txt3D(1.2f,2.0f,0,"Hooke's law: restoring force",16,TEXT0);
        txt3D(1.2f,1.45f,0,"F = -kx = " + fmt(-k*V("x",2)) + " N",16,CYELLOW);
    }
    // Equilibrium line
    glDisable(GL_LIGHTING);
    gc(TEXT2,0.3f); glLineWidth(1.f);
    glBegin(GL_LINES); glVertex3f(-4,-0.5f,0); glVertex3f(4,-0.5f,0); glEnd();
    // Sinusoidal scrolling trail to the right
    gc(CYELLOW, 0.7f);
    glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=100; i++){
        float dx = i / 100.f * 5.f;
        float past_t = t - dx * 0.4f;
        float past_x = (float)(A * cos(omega * past_t));
        glVertex3f(dx + 0.8f, past_x - 0.5f, 0.f);
    }
    glEnd();
    // Connecting line from mass center to scrolling trail start
    gc(CYELLOW, 0.4f);
    glLineWidth(1.f);
    glBegin(GL_LINES);
    glVertex3f(0.f, x - 0.5f, 0.f);
    glVertex3f(0.8f, x - 0.5f, 0.f);
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    if(curEq().id!="f_kx"){
        double period=2*M_PI/std::max(omega,1e-9);
        txt3D(1.2f,2.0f,0,"Mass and stiffness set the rhythm",16,TEXT0);
        txt3D(1.2f,1.45f,0,"T = " + fmt(period) + " s",16,CYELLOW);
    }
}

// Pendulum  T = 2*pi*sqrt(l/g)
void scenePendulum(){
    double lv=V("l",2), gv=V("g",9.8);
    double omega = sqrt(std::max(gv,0.01)/std::max(lv,0.01));
    double A=V("A",0.4);
    float t = T();
    float angle = (float)(A * cos(omega*t));
    float px = (float)(lv*1.8*sin(angle));
    float py = (float)(-lv*1.8*cos(angle)) + 3.f;
    // Pivot
    gc(TEXT1);
    glPushMatrix(); glTranslatef(0,3.f,0); drawSphere(0.12f); glPopMatrix();
    // String
    glDisable(GL_LIGHTING);
    gc(TEXT0); glLineWidth(1.5f);
    glBegin(GL_LINES); glVertex3f(0,3.f,0); glVertex3f(px,py,0); glEnd();
    glLineWidth(1.f);
    // Swing arc
    gc(ACCENT2,0.3f); glLineWidth(1.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=60;i++){
        float th=(float)(-A + 2*A*i/60.f);
        glVertex3f((float)(lv*1.8*sin(th)), (float)(-lv*1.8*cos(th))+3.f, 0);
    }
    glEnd();
    glEnable(GL_LIGHTING);
    // Bob
    gc(CORANGE);
    glPushMatrix(); glTranslatef(px,py,0); drawSphere(0.38f); glPopMatrix();
}

// Ohm's Law  V = I * R  — circuit visualization
void sceneOhm(){
    double Vv=V("V",12), R=V("R",4);
    double I = Vv / std::max(R,0.01);
    float t = T();
    // Battery left
    gc(CBLUE,0.9f);
    glPushMatrix(); glTranslatef(-5,0,0); drawBox(0.5f,2,0.5f); glPopMatrix();
    // Resistor centre
    gc(CORANGE,0.9f);
    glPushMatrix(); drawBox(2.5f,0.8f,0.8f); glPopMatrix();
    // Wires
    glDisable(GL_LIGHTING);
    gc(TEXT0); glLineWidth(2.f);
    glBegin(GL_LINES);
    glVertex3f(-4.75f, 0.9f,0); glVertex3f(-1.25f, 0.9f,0);
    glVertex3f( 1.25f, 0.9f,0); glVertex3f( 5.f,   0.9f,0);
    glVertex3f( 5.f,   0.9f,0); glVertex3f( 5.f,  -0.9f,0);
    glVertex3f( 5.f,  -0.9f,0); glVertex3f(-4.75f,-0.9f,0);
    glVertex3f(-4.75f,-0.9f,0); glVertex3f(-4.75f, 0.9f,0);
    glEnd();
    // Electrons flowing (dots moving along top wire)
    int ne = std::max(1, (int)(I*1.5));
    gc(CYELLOW);
    glPointSize(6.f);
    glBegin(GL_POINTS);
    for(int i=0;i<ne;i++){
        float pos = fmodf((float)(t*(I*0.4)) + i/(float)ne, 1.f);
        float ex = -4.75f + pos*(4.75f+1.25f);
        if(ex <= 5.f) glVertex3f(ex, 0.9f, 0.1f);
    }
    glEnd();
    glPointSize(1.f);
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
}

// Coulomb's Law  F = k*q1*q2/r^2  — two charges
void sceneCoulomb(){
    double q1=V("q1",1), q2=V("q2",1), r=V("r",3);
    double k=8.99e9;
    double F=k*fabs(q1)*fabs(q2)/(r*r);
    bool attract = (q1*q2 < 0);
    float sep = (float)r*0.8f;
    // Charges
    C3 c1col = q1>=0 ? CRED : CBLUE;
    C3 c2col = q2>=0 ? CRED : CBLUE;
    gc(c1col);
    glPushMatrix(); glTranslatef(-sep,0,0); drawSphere(0.55f); glPopMatrix();
    gc(c2col);
    glPushMatrix(); glTranslatef( sep,0,0); drawSphere(0.55f); glPopMatrix();
    // Force arrows
    float fscale = (float)std::min(F*1e-9*3.0, 3.0);
    float dir = attract ? 1.f : -1.f;
    arrow(-sep,0,0,  dir,0,0, fscale+0.3f, attract?CGREEN:CORANGE);
    arrow( sep,0,0, -dir,0,0, fscale+0.3f, attract?CGREEN:CORANGE);
    // 3D Field lines
    glDisable(GL_LIGHTING);
    gc(ACCENT2,0.3f); glLineWidth(1.f);
    int numRotations = 6;
    int numLinesPerRot = 8;
    for(int r_idx=0; r_idx<numRotations; r_idx++){
        float rotAngle = r_idx * (float)M_PI / numRotations;
        glPushMatrix();
        glRotatef(rotAngle * 180.f / (float)M_PI, 1, 0, 0); // Rotate around X-axis (line connecting charges)
        for(int i=0; i<numLinesPerRot; i++){
            float ang = (float)(i * 2.f * M_PI / numLinesPerRot);
            glBegin(GL_LINE_STRIP);
            for(int j=0; j<=30; j++){
                float tt = (float)j / 30.f;
                float fx, fy;
                if (attract) {
                    fx = -sep + tt * (2.f * sep);
                    fy = sinf(ang) * sinf(tt * (float)M_PI) * 1.5f;
                } else {
                    // Repelling field lines curve away from the middle plane
                    fx = -sep + tt * sep * 0.9f;
                    fy = sinf(ang) * tt * 2.0f;
                    if (j > 15) {
                        fx = sep - (1.f - tt) * sep * 0.9f;
                        fy = sinf(ang) * (1.f - tt) * 2.0f;
                    }
                }
                glVertex3f(fx, fy, 0.f);
            }
            glEnd();
        }
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

// Magnetic force on a wire  F = B*I*L  — wire in magnetic field
void sceneMagField(){
    double B=V("B",1), I2=V("I",5), L=V("L",2);
    double F = B * I2 * L;
    float t = T();
    // Wire
    gc(CORANGE,0.9f);
    glPushMatrix(); glTranslatef(0,0,0); drawCyl(0.12f,0.12f,(float)L*2.5f,20);
    glPopMatrix();
    // Magnetic field dots/crosses (B into page = dots, shown as glowing spheres in Z grid)
    glDisable(GL_LIGHTING);
    gc(CBLUE,0.4f);
    for(int ix=-3;ix<=3;ix++) for(int iz=-3;iz<=3;iz++){
        glPushMatrix(); glTranslatef((float)ix*1.5f,0,(float)iz*1.5f);
        glBegin(GL_POINTS); glPointSize(6.f); glVertex3f(0,0,0); glEnd();
        glPointSize(1.f);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
    // Force arrow (upward = F = BIL)
    float fscale = (float)std::min(F*0.25, 3.5);
    arrow(0,0,0, 0,1,0, fscale+0.3f, CRED);
    // Animated current electrons
    glDisable(GL_LIGHTING);
    gc(CYELLOW);
    glPointSize(7.f);
    float Lhalf=(float)L*1.25f;
    glBegin(GL_POINTS);
    for(int i=0;i<6;i++){
        float pos = fmodf(t*(float)I2*0.15f + i/6.f, 1.f);
        glVertex3f(0, -Lhalf+pos*2*Lhalf, 0);
    }
    glEnd();
    glPointSize(1.f);
    glEnable(GL_LIGHTING);
}

// Wave equation  v = f * lambda
void sceneWaveEq(){
    double fv=V("f",2), lam=V("lam",3);
    double spd = fv * lam;
    float t = T();
    glDisable(GL_LIGHTING);
    // Wave 1 (transverse)
    gc(CBLUE); glLineWidth(2.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=200;i++){
        float x=i/200.f*14-7;
        glVertex3f(x, 1.5f*sinf((float)(2*M_PI/lam)*x - (float)(2*M_PI*fv)*t), 0);
    }
    glEnd();
    // Wave 2 (longitudinal representation — compression bands)
    gc(CORANGE,0.6f); glLineWidth(1.f);
    for(int i=0;i<=60;i++){
        float x0=-7+i/60.f*14;
        float compress = 0.3f*sinf((float)(2*M_PI/lam)*x0-(float)(2*M_PI*fv)*t);
        glBegin(GL_LINES);
        glVertex3f(x0+compress,-0.8f,0); glVertex3f(x0+compress,-1.5f,0);
        glEnd();
    }
    // Wavelength marker
    gc(CYELLOW,0.7f); glLineWidth(1.f);
    float x1=-1.f, x2=x1+(float)lam;
    glBegin(GL_LINES); glVertex3f(x1,2.5f,0); glVertex3f(x2,2.5f,0); glEnd();
    glBegin(GL_LINES); glVertex3f(x1,2.2f,0); glVertex3f(x1,2.8f,0); glEnd();
    glBegin(GL_LINES); glVertex3f(x2,2.2f,0); glVertex3f(x2,2.8f,0); glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
}

// Gravitational PE  PE = m*g*h  — ball at height
void sceneGravPE(){
    double mv=V("m",2), hv=V("h",4), gv=V("g",9.8);
    if(curEq().id=="w_ke"){
        double vv=V("v",15);
        double KE=0.5*mv*vv*vv;
        float bx=fmodf(T()*(float)std::max(0.5,fabs(vv))*0.12f,12.f)-5.5f;
        glColor4f(0.05f,0.12f,0.22f,0.8f);
        glPushMatrix(); glTranslatef(0,-3.2f,0); drawBox(12,0.12f,2.5f); glPopMatrix();
        gc(CORANGE);
        glPushMatrix(); glTranslatef(bx,-2.55f,0); drawSphere(0.45f); glPopMatrix();
        arrow(bx,-2.55f,0, vv>=0?1.f:-1.f,0,0, std::min(4.0f,0.6f+(float)fabs(vv)*0.08f), CGREEN);
        gc(CGREEN,0.85f);
        float kh=std::min(4.f,(float)(KE/500.f));
        glPushMatrix(); glTranslatef(5.3f,kh/2-3.2f,0); drawBox(0.7f,kh,0.7f); glPopMatrix();
        txt3D(-5.0f,2.1f,0,"Kinetic energy grows with v squared",16,TEXT0);
        txt3D(-5.0f,1.5f,0,"Double speed -> four times KE",16,ACCENT2);
        txt3D(3.5f,1.5f,0,"KE = " + fmt(KE) + " J",16,CYELLOW);
        return;
    }
    if(curEq().id=="f_mg"){
        double W=mv*gv;
        glColor4f(0.05f,0.25f,0.05f,0.8f);
        glPushMatrix(); glTranslatef(0,-3.5f,0); drawBox(12,0.12f,2.5f); glPopMatrix();
        gc(CBLUE);
        glPushMatrix(); glTranslatef(0,-1.4f,0); drawSphere(std::min(0.9f,0.25f+(float)mv*0.035f)); glPopMatrix();
        arrow(0,-1.4f,0,0,-1,0,std::min(4.0f,0.5f+(float)W*0.04f),CRED);
        txt3D(-5.0f,2.1f,0,"Weight is gravity pulling on mass",16,TEXT0);
        txt3D(-5.0f,1.5f,0,"W = mg = " + fmt(W) + " N",16,CYELLOW);
        return;
    }
    double PE=mv*gv*hv;
    float t = T();
    float h=(float)hv;
    float bounce = fabs(sinf(t*2.5f))*h;
    // Ground
    glColor4f(0.05f,0.25f,0.05f,0.8f);
    glPushMatrix(); glTranslatef(0,-3.5f,0); drawBox(12,0.12f,2.5f); glPopMatrix();
    // Energy bar (PE)
    gc(CRED,0.8f);
    float ph=std::min(4.f,(float)(PE/20.f));
    glPushMatrix(); glTranslatef(5,ph/2-3.5f,0); drawBox(0.6f,ph,0.6f); glPopMatrix();
    gc(CGREEN,0.8f);
    float kh=std::max(0.05f,4.f-ph);
    glPushMatrix(); glTranslatef(5.8f,kh/2-3.5f,0); drawBox(0.6f,kh,0.6f); glPopMatrix();
    // Ball
    gc(CYELLOW);
    glPushMatrix(); glTranslatef(-1.f,-3.f+bounce,0); drawSphere(0.45f); glPopMatrix();
    // Arrow showing height
    arrow(-1.f,-3.f,0, 0,1,0, bounce+0.1f, ACCENT2);
    txt3D(-5.0f,2.1f,0,"Height stores gravitational energy",16,TEXT0);
    txt3D(-5.0f,1.5f,0,"PE = " + fmt(PE) + " J",16,CYELLOW);
}

// Circular Motion  — orbiting sphere with centripetal force and velocity vectors
void sceneCircular(){
    double mv=V("m",1.0), rv=V("r",2.0), vv=V("v",10.0);
    float r = (float)rv;
    float t = T();
    float omega = (float)(vv / std::max(rv, 0.01));
    float px = r * cosf(omega * t);
    float pz = r * sinf(omega * t);
    // Orbit ring
    glDisable(GL_LIGHTING);
    gc(ACCENT2, 0.4f); glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=64; i++){
        float a = 2.f*(float)M_PI*i/64.f;
        glVertex3f(r*cosf(a), 0.f, r*sinf(a));
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    // Centre sphere (pivot)
    gc(TEXT2, 0.5f);
    glPushMatrix(); drawSphere(0.15f); glPopMatrix();
    // Orbiting body
    gc(CORANGE);
    glPushMatrix(); glTranslatef(px, 0.f, pz); drawSphere(0.4f); glPopMatrix();
    // Centripetal force arrow (pointing inward)
    float F = (float)(mv * vv * vv / std::max(rv, 0.01));
    float fscale = std::min(3.0f, F * 0.08f + 0.3f);
    arrow(px, 0.f, pz, -px/r, 0.f, -pz/r, fscale, CRED);
    // Velocity vector (tangent to circle)
    float vx_dir = -sinf(omega * t);
    float vz_dir =  cosf(omega * t);
    arrow(px, 0.f, pz, vx_dir, 0.f, vz_dir, 1.5f, CGREEN);
    // Labels
    txt3D(px+0.1f, 0.6f, pz, "v", 16, CGREEN);
    txt3D(px-0.8f, -0.8f, pz, "F_c", 16, CRED);
}

// Magnetic Force on Charge — helical path of charge in B-field
void sceneMagneticForce(){
    double qv=V("q",10.0), vv=V("v",3.0), Bv=V("B",1.0);
    double r_helix = std::max(0.5, (qv*1e-6)*(vv*1e5) / std::max(Bv*qv*1e-6, 1e-20));
    float r = std::min(3.0f, (float)(r_helix * 1e-4));
    if(r < 0.3f) r = 1.5f;
    float t = T();
    // B-field arrows (along +Y)
    for(int iz=-2; iz<=2; iz++) for(int ix=-2; ix<=2; ix++){
        arrow((float)ix*2.f, -3.5f, (float)iz*2.f, 0,1,0, 1.5f, CBLUE);
    }
    // Helical path
    glDisable(GL_LIGHTING);
    gc(CYELLOW, 0.7f); glLineWidth(2.f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=120; i++){
        float angle = i / 120.f * 4.f * (float)M_PI;
        float y = -3.f + i / 120.f * 6.f;
        glVertex3f(r*cosf(angle), y, r*sinf(angle));
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    // Animated particle on helix
    float angle = fmodf(t * 2.f, 4.f*(float)M_PI);
    float cy = -3.f + fmodf(t * 2.f, 4.f*(float)M_PI) / (4.f*(float)M_PI) * 6.f;
    gc(HOT);
    glPushMatrix(); glTranslatef(r*cosf(angle), cy, r*sinf(angle)); drawSphere(0.3f); glPopMatrix();
    // Lorentz force arrow
    float vdir_x = -sinf(angle);
    float vdir_z =  cosf(angle);
    arrow(r*cosf(angle), cy, r*sinf(angle), vdir_x, 0.f, vdir_z, 1.0f, CGREEN);
    txt3D(0.f, 3.5f, 0.f, "B", 18, CBLUE);
}

// EM Induction — coil + moving magnet, Faraday flux and EMF arrow
void sceneInduction(){
    double Nv=V("N",100.0), dphi=V("dPhi_dt",2.0);
    float t = T();
    float magX = -6.f + fmodf(t * 1.5f, 12.f); // magnet slides left to right
    // Coil geometry (multiple rings)
    gc(ACCENT, 0.9f);
    for(int i=0; i<8; i++){
        float cx = (float)i * 0.45f;
        glPushMatrix();
        glTranslatef(cx, 0.f, 0.f);
        glRotatef(90.f, 0.f, 1.f, 0.f);
        drawTorus(1.4f, 0.08f, 24, 8);
        glPopMatrix();
    }
    // Magnet
    float mw = 1.0f;
    glPushMatrix(); glTranslatef(magX, 0.f, 0.f);
    gc(CRED, 0.9f);
    glPushMatrix(); glTranslatef(0.3f, 0.f, 0.f); drawBox(0.6f, 0.7f, 0.7f); glPopMatrix();
    gc(CBLUE, 0.9f);
    glPushMatrix(); glTranslatef(-0.3f, 0.f, 0.f); drawBox(0.6f, 0.7f, 0.7f); glPopMatrix();
    glPopMatrix();
    // Flux arrows through coil (density depends on magnet proximity)
    float dist = fabs(magX - 1.8f);
    int numFlux = (int)std::min(6.f, std::max(1.f, 8.f - dist));
    glDisable(GL_LIGHTING);
    gc(CYELLOW, 0.5f); glLineWidth(1.f);
    for(int i=0; i<numFlux; i++){
        float fy = -0.6f + i * 0.22f;
        glBegin(GL_LINES);
        glVertex3f(-1.f, fy, 0.f); glVertex3f(4.5f, fy, 0.f);
        glEnd();
    }
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    // EMF arrow (top of coil)
    float emfDir = (magX < 1.8f) ? -1.f : 1.f;
    if(dist < 3.f){
        arrow(1.8f, 2.f, 0.f, emfDir, 0.f, 0.f, 2.0f * (1.f - dist/3.f) + 0.3f, HOT);
        txt3D(1.8f + emfDir*2.2f, 2.2f, 0.f, "EMF", 16, HOT);
    }
    txt3D(-0.5f, 2.5f, 0.f, "N", 16, CRED);
    txt3D(magX-0.5f, -1.2f, 0.f, magX < 1.8f ? "S  N" : "N  S", 16, TEXT0);
}

// Gravitational Orbit — Kepler ellipse with animated planet
void sceneOrbit(){
    double Mv=V("M",1e25), Rv=V("r",1e7);
    float a = 4.5f, b = 3.0f; // semi-major and semi-minor axes in scene units
    float t = T();
    // Orbital period proportional to Kepler's 3rd law
    double T_kepler = 2.0 * M_PI * sqrt(pow(Rv, 3.0) / (6.6743e-11 * Mv));
    float orbit_t = fmodf(t * 0.8f, 2.f*(float)M_PI);
    float px = a * cosf(orbit_t);
    float pz = b * sinf(orbit_t);
    // Draw Ellipse
    glDisable(GL_LIGHTING);
    gc(ACCENT2, 0.4f); glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    for(int i=0; i<=64; i++){
        float angle = 2.f*(float)M_PI*i/64.f;
        glVertex3f(a*cosf(angle), 0.f, b*sinf(angle));
    }    glEnd();
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
    // Sun at focus
    gc(CYELLOW);
    glPushMatrix(); glTranslatef(sqrtf(a*a - b*b), 0.f, 0.f); drawSphere(0.6f); glPopMatrix();
    // Planet
    gc(CBLUE);
    glPushMatrix(); glTranslatef(px, 0.f, pz); drawSphere(0.35f); glPopMatrix();
    // Gravitational force arrow from planet to sun
    float fx = sqrtf(a*a-b*b) - px;
    float fz = -pz;
    arrow(px, 0.f, pz, fx, 0.f, fz, 1.5f, CRED);
    // Velocity tangent vector
    float vx_dir = -a * sinf(orbit_t);
    float vz_dir =  b * cosf(orbit_t);
    float v_mag = sqrtf(vx_dir*vx_dir + vz_dir*vz_dir);
    if(v_mag > 0.1f){
        arrow(px, 0.f, pz, vx_dir/v_mag, 0.f, vz_dir/v_mag, 1.5f, CGREEN);
    }
    // Labels
    txt3D(sqrtf(a*a-b*b)+0.2f, 1.0f, 0.f, "Star", 16, CYELLOW);
    txt3D(px+0.3f, 0.5f, pz, "Planet", 16, CBLUE);
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
    // NEW — 15-17 syllabus scenes
    else if(viz=="newton")     sceneNewton();
    else if(viz=="kinematics") sceneKinematics();
    else if(viz=="projectile") sceneProjectile();
    else if(viz=="spring")     sceneSpring();
    else if(viz=="pendulum")   scenePendulum();
    else if(viz=="ohm")        sceneOhm();
    else if(viz=="coulomb")    sceneCoulomb();
    else if(viz=="magfield")   sceneMagField();
    else if(viz=="waveeq")     sceneWaveEq();
    else if(viz=="gravpe")     sceneGravPE();
    else if(viz=="circular")   sceneCircular();
    else if(viz=="magnetic_force") sceneMagneticForce();
    else if(viz=="induction")  sceneInduction();
    else if(viz=="orbit")      sceneOrbit();
    else if(viz=="pressure")   scenePressure();
    else if(viz=="workpower")  sceneWorkPower();
    else if(viz=="vectorlab")  sceneVectorLab();
    else if(viz=="interference") sceneInterference();
    else if(viz=="circuitlab") sceneCircuitLab();
    else if(viz=="aclab")      sceneACLab();
    else if(viz=="optics")     sceneOpticsBench();
    else if(viz=="decay")      sceneDecay();
    else                       sceneBar();
}

/* ─────────────────────────────────────────────────────────
   EQUATIONS  (15 from 20BSPH101)
───────────────────────────────────────────────────────── */
void initEquations(){
    g_eqs.clear();
    g_categories.clear();

    auto addCat = [&](const std::string& name) {
        g_categories.push_back({name, {}, false});
    };

    auto A = [&](Equation e) {
        g_eqs.push_back(e);
        g_categories.back().eqIndices.push_back((int)g_eqs.size() - 1);
    };

    // 1. Motion & Kinematics
    addCat("Motion & Kinematics");
    A({"kin_v", "KIN", "v⃗ = u⃗ + a⃗t", "v = u + a*t", "v⃗", "m/s", "kinematics",
       {{"u", "Initial velocity", "m/s", 0, 30, 0, 0.5}, {"a", "Acceleration", "m/s²", -10, 20, 3, 0.5}, {"t", "Time", "s", 0, 10, 5, 0.1}},
       [](auto& v){ return v["u"] + v["a"] * v["t"]; }});
    A({"kin_s", "KIN", "s⃗ = u⃗t + ½a⃗t²", "s = u*t + 0.5*a*t^2", "s⃗", "m", "kinematics",
       {{"u", "Initial velocity", "m/s", 0, 30, 5, 0.5}, {"a", "Acceleration", "m/s²", -10, 20, 3, 0.5}, {"t", "Time", "s", 0, 10, 5, 0.1}},
       [](auto& v){ return v["u"] * v["t"] + 0.5 * v["a"] * v["t"] * v["t"]; }});
    A({"kin_v2", "KIN", "v⃗² = u⃗² + 2a⃗s⃗", "v2 = u^2 + 2*a*s", "v²", "m²/s²", "kinematics",
       {{"u", "Initial velocity", "m/s", 0, 30, 0, 0.5}, {"a", "Acceleration", "m/s²", -10, 20, 3, 0.5}, {"s", "Displacement", "m", 0, 100, 10, 1}},
       [](auto& v){ return v["u"]*v["u"] + 2 * v["a"] * v["s"]; }});
    A({"kin_speed", "KIN", "Speed", "Speed = d / t", "Speed", "m/s", "workpower",
       {{"d", "Distance", "m", 0, 200, 50, 1}, {"t", "Time", "s", 0.1, 20, 5, 0.1}},
       [](auto& v){ return v["d"] / v["t"]; }});
    A({"kin_vel", "KIN", "Velocity", "v = s / t", "v⃗", "m/s", "kinematics",
       {{"s", "Displacement", "m", -100, 100, 20, 1}, {"t", "Time", "s", 0.1, 20, 5, 0.1}},
       [](auto& v){ return v["s"] / v["t"]; }});
    A({"kin_acc", "KIN", "Acceleration", "a = (v - u) / t", "a⃗", "m/s²", "kinematics",
       {{"v", "Final velocity", "m/s", 0, 50, 20, 0.5}, {"u", "Initial velocity", "m/s", 0, 50, 5, 0.5}, {"t", "Time", "s", 0.1, 10, 3, 0.1}},
       [](auto& v){ return (v["v"] - v["u"]) / v["t"]; }});

    // 2. Forces & Newton's Laws
    addCat("Forces & Newton's Laws");
    A({"f_ma", "FRC", "F⃗ = m a⃗", "F = m * a", "F⃗", "N", "newton",
       {{"m", "Mass", "kg", 0.5, 20, 2, 0.5}, {"a", "Acceleration", "m/s²", 0.1, 25, 5, 0.1}},
       [](auto& v){ return v["m"] * v["a"]; }});
    A({"f_mg", "FRC", "W⃗ = m g⃗", "W = m * g", "W⃗", "N", "gravpe",
       {{"m", "Mass", "kg", 0.5, 20, 2, 0.5}, {"g", "Gravity", "m/s²", 1, 25, 9.8, 0.1}},
       [](auto& v){ return v["m"] * v["g"]; }});
    A({"f_grav", "FRC", "F⃗ = G m₁m₂/r²", "F = 6.674e-11*m1*m2/(r^2)", "F⃗", "N", "orbit",
       {{"m1", "Mass 1", "kg", 1e24, 1e25, 5.97e24, 1e23}, {"m2", "Mass 2", "kg", 1e22, 1e23, 7.34e22, 1e21}, {"r", "Separation", "m", 1e7, 1e8, 3.84e7, 1e6}},
       [](auto& v){ return 6.6743e-11 * v["m1"] * v["m2"] / (v["r"] * v["r"]); }});
    A({"f_kx", "FRC", "F⃗ = −k x⃗", "F = -k * x", "F⃗", "N", "spring",
       {{"k", "Spring constant", "N/m", 1, 100, 20, 0.5}, {"x", "Displacement", "m", -5, 5, 2, 0.1}},
       [](auto& v){ return -v["k"] * v["x"]; }});
    A({"f_centrip", "FRC", "F⃗ = m v⃗²/r", "F = m * v^2 / r", "F⃗_c", "N", "circular",
       {{"m", "Mass", "kg", 0.1, 10, 1, 0.1}, {"v", "Velocity", "m/s", 1, 30, 10, 0.5}, {"r", "Radius", "m", 0.5, 5, 2, 0.05}},
       [](auto& v){ return v["m"] * v["v"] * v["v"] / v["r"]; }});
    A({"f_pressure", "FRC", "P = F / A", "P = F / A", "P", "Pa", "pressure",
       {{"F", "Force", "N", 1, 1000, 200, 5}, {"A", "Area", "m²", 0.01, 10, 1, 0.05}},
       [](auto& v){ return v["F"] / v["A"]; }});

    // 3. Momentum & Collisions
    addCat("Momentum & Collisions");
    A({"p_mv", "MOM", "p⃗ = m v⃗", "p = m * v", "p⃗", "kg·m/s", "newton",
       {{"m", "Mass", "kg", 0.5, 20, 2, 0.5}, {"v", "Velocity", "m/s", -30, 30, 10, 0.5}},
       [](auto& v){ return v["m"] * v["v"]; }});
    A({"p_imp", "MOM", "Impulse = F⃗ Δt", "Impulse = F * dt", "Impulse", "N·s", "newton",
       {{"F", "Force", "N", 1, 100, 20, 0.5}, {"dt", "Duration", "s", 0.01, 2, 0.5, 0.01}},
       [](auto& v){ return v["F"] * v["dt"]; }});

    // 4. Work, Energy & Power
    addCat("Work, Energy & Power");
    A({"w_work", "WEP", "W = F⃗ s⃗ cosθ", "W = F * s * cos(theta*pi/180)", "W", "J", "workpower",
       {{"F", "Force", "N", 1, 100, 20, 0.5}, {"s", "Displacement", "m", 0, 50, 10, 0.5}, {"theta", "Angle", "deg", 0, 180, 0, 1}},
       [](auto& v){ return v["F"] * v["s"] * cos(v["theta"] * M_PI / 180.0); }});
    A({"w_ke", "WEP", "KE = ½ m v⃗²", "KE = 0.5 * m * v^2", "KE", "J", "gravpe",
       {{"m", "Mass", "kg", 0.5, 20, 2, 0.5}, {"v", "Velocity", "m/s", 0, 40, 15, 0.5}},
       [](auto& v){ return 0.5 * v["m"] * v["v"] * v["v"]; }});
    A({"w_pe", "WEP", "PE = m g⃗ h", "PE = m * g * h", "PE", "J", "gravpe",
       {{"m", "Mass", "kg", 0.5, 20, 2, 0.5}, {"g", "Gravity", "m/s²", 1, 20, 9.8, 0.1}, {"h", "Height", "m", 0.5, 8, 4, 0.5}},
       [](auto& v){ return v["m"] * v["g"] * v["h"]; }});
    A({"w_powt", "WEP", "P = W / t", "P = W / t", "P", "W", "workpower",
       {{"W", "Work done", "J", 1, 1000, 200, 5}, {"t", "Time", "s", 0.1, 20, 5, 0.1}},
       [](auto& v){ return v["W"] / v["t"]; }});
    A({"w_powf", "WEP", "P = F⃗ v⃗", "P = F * v", "P", "W", "newton",
       {{"F", "Force", "N", 1, 100, 20, 0.5}, {"v", "Velocity", "m/s", 0, 30, 5, 0.1}},
       [](auto& v){ return v["F"] * v["v"]; }});
    A({"w_vit", "WEP", "E = V I t", "E = V * I * t", "E", "J", "ohm",
       {{"V", "Voltage", "V", 1, 50, 12, 0.5}, {"I", "Current", "A", 0.1, 10, 2, 0.1}, {"t", "Time", "s", 1, 100, 10, 1}},
       [](auto& v){ return v["V"] * v["I"] * v["t"]; }});

    // 5. Rotational Motion
    addCat("Rotational Motion");
    A({"r_tor", "ROT", "τ⃗ = r⃗ × F⃗ sinθ", "T = r * F * sin(theta*pi/180)", "τ", "N·m", "torsion",
       {{"r", "Lever arm", "m", 0.1, 5, 2, 0.05}, {"F", "Force", "N", 1, 100, 20, 0.5}, {"theta", "Angle", "deg", 0, 180, 90, 1}},
       [](auto& v){ return v["r"] * v["F"] * sin(v["theta"] * M_PI / 180.0); }});
    A({"r_am", "ROT", "L⃗ = I ω⃗", "L = I * w", "L⃗", "kg·m²/s", "torsion",
       {{"I", "Inertia", "kgm²", 0.001, 1, 0.1, 0.001}, {"w", "Ang. velocity", "rad/s", 0, 50, 10, 0.5}},
       [](auto& v){ return v["I"] * v["w"]; }});
    A({"r_ker", "ROT", "KE_rot = ½ I ω⃗²", "KE = 0.5 * I * w^2", "KE_rot", "J", "torsion",
       {{"I", "Inertia", "kgm²", 0.001, 1, 0.1, 0.001}, {"w", "Ang. velocity", "rad/s", 0, 50, 10, 0.5}},
       [](auto& v){ return 0.5 * v["I"] * v["w"] * v["w"]; }});
    A({"r_w", "ROT", "ω = θ / t", "w = theta / t", "ω", "rad/s", "torsion",
       {{"theta", "Angle", "rad", 0, 100, 10, 0.5}, {"t", "Time", "s", 0.1, 20, 5, 0.1}},
       [](auto& v){ return v["theta"] / v["t"]; }});
    A({"r_wf", "ROT", "ω = 2 π f", "w = 2 * pi * f", "ω", "rad/s", "circular",
       {{"f", "Frequency", "Hz", 0.1, 20, 2, 0.1}},
       [](auto& v){ return 2 * M_PI * v["f"]; }});

    // 6. Gravitation & Orbits
    addCat("Gravitation & Orbits");
    A({"g_u", "GRV", "U = −G M m / r", "U = -6.674e-11*M*m/r", "U", "J", "orbit",
       {{"M", "Star mass", "kg", 1e24, 1e26, 1e25, 1e23}, {"m", "Planet mass", "kg", 1e21, 1e23, 1e22, 1e21}, {"r", "Distance", "m", 1e6, 1e8, 1e7, 1e5}},
       [](auto& v){ return -6.6743e-11 * v["M"] * v["m"] / v["r"]; }});
    A({"g_ve", "GRV", "vₑ = √(2GM/R)", "ve = sqrt(2*6.674e-11*M/R)", "vₑ", "m/s", "orbit",
       {{"M", "Planet mass", "kg", 1e24, 1e25, 5.97e24, 1e23}, {"R", "Radius", "m", 1e6, 1e7, 6.37e6, 1e5}},
       [](auto& v){ return sqrt(2 * 6.6743e-11 * v["M"] / v["R"]); }});
    A({"g_vo", "GRV", "v₀ = √(GM/R)", "vo = sqrt(6.674e-11*M/R)", "v₀", "m/s", "orbit",
       {{"M", "Star mass", "kg", 1e24, 1e26, 1e25, 1e23}, {"R", "Orbit radius", "m", 1e6, 1e8, 1e7, 1e5}},
       [](auto& v){ return sqrt(6.6743e-11 * v["M"] / v["R"]); }});

    // 7. Oscillations & Waves
    addCat("Oscillations & Waves");
    A({"w_sin", "OSC", "x = A sin(ω t)", "x = A * sin(w * t)", "x", "m", "spring",
       {{"A", "Amplitude", "m", 0.1, 5, 2, 0.05}, {"w", "Frequency", "rad/s", 0.1, 10, 3, 0.1}, {"t", "Time", "s", 0, 10, 2, 0.1}},
       [](auto& v){ return v["A"] * sin(v["w"] * v["t"]); }});
    A({"w_t", "OSC", "T = 2pi √(m/k)", "T = 2*pi*sqrt(m/k)", "T", "s", "spring",
       {{"m", "Mass", "kg", 0.1, 20, 1, 0.1}, {"k", "Spring const", "N/m", 1, 100, 10, 0.5}},
       [](auto& v){ return 2 * M_PI * sqrt(v["m"] / v["k"]); }});
    A({"w_vfl", "OSC", "v = f λ", "v = f * lam", "v", "m/s", "waveeq",
       {{"f", "Frequency", "Hz", 0.5, 10, 2, 0.1}, {"lam", "Wavelength", "m", 0.5, 8, 3, 0.1}},
       [](auto& v){ return v["f"] * v["lam"]; }});
    A({"w_f1t", "OSC", "f = 1 / T", "f = 1 / T", "f", "Hz", "waveeq",
       {{"T", "Period", "s", 0.05, 5, 0.5, 0.05}},
       [](auto& v){ return 1.0 / v["T"]; }});
    A({"w_beta", "OSC", "β = λ D / d", "beta = lam * D / d", "β", "m", "interference",
       {{"lam", "Wavelength", "nm", 400, 700, 500, 5}, {"D", "Screen dist", "m", 0.5, 3, 1.5, 0.05}, {"d", "Slit separation", "mm", 0.1, 2, 0.5, 0.05}},
       [](auto& v){ return (v["lam"] * 1e-9) * v["D"] / (v["d"] * 1e-3); }});
    A({"w_diff", "OSC", "a sinθ = n λ", "theta = asin(n * lam / a) * 180 / pi", "θ", "deg", "snell",
       {{"n", "Order", "", 1, 5, 1, 1}, {"lam", "Wavelength", "nm", 400, 700, 500, 5}, {"a", "Slit width", "um", 1, 10, 2, 0.1}},
       [](auto& v){ double s = v["n"] * (v["lam"]*1e-9) / (v["a"]*1e-6); return s <= 1.0 ? asin(s)*180.0/M_PI : 0.0; }});

    // 8. Properties of Matter
    addCat("Properties of Matter");
    A({"m_stress", "MAT", "Stress = F / A", "Stress = F / A", "Stress", "Pa", "cantilever",
       {{"F", "Applied force", "N", 1, 1000, 100, 5}, {"A", "Area", "m²", 0.001, 0.1, 0.01, 0.001}},
       [](auto& v){ return v["F"] / v["A"]; }});
    A({"m_strain", "MAT", "Strain = ΔL / L", "Strain = dL / L", "Strain", "", "cantilever",
       {{"dL", "Extension", "mm", 0, 10, 2, 0.1}, {"L", "Original len", "m", 0.5, 5, 2, 0.05}},
       [](auto& v){ return (v["dL"] * 1e-3) / v["L"]; }});
    A({"m_ym", "MAT", "Y = Stress / Strain", "Y = stress / strain", "Y", "Pa", "cantilever",
       {{"stress", "Stress", "Pa", 1e4, 1e6, 1e5, 1e3}, {"strain", "Strain", "", 1e-4, 1e-2, 1e-3, 1e-5}},
       [](auto& v){ return v["stress"] / v["strain"]; }});

    // 9. Vectors
    addCat("Vectors");
    A({"v_res", "VEC", "R² = A²+B²+2ABcosθ", "R = sqrt(A^2 + B^2 + 2*A*B*cos(theta*pi/180))", "R", "", "vectorlab",
       {{"A", "Vector A", "", 1, 20, 5, 0.1}, {"B", "Vector B", "", 1, 20, 5, 0.1}, {"theta", "Angle", "deg", 0, 180, 60, 1}},
       [](auto& v){ return sqrt(v["A"]*v["A"] + v["B"]*v["B"] + 2 * v["A"] * v["B"] * cos(v["theta"] * M_PI / 180.0)); }});
    A({"v_dot", "VEC", "A · B = AB cosθ", "dot = A * B * cos(theta*pi/180)", "A·B", "", "vectorlab",
       {{"A", "Vector A", "", 1, 20, 5, 0.1}, {"B", "Vector B", "", 1, 20, 5, 0.1}, {"theta", "Angle", "deg", 0, 180, 60, 1}},
       [](auto& v){ return v["A"] * v["B"] * cos(v["theta"] * M_PI / 180.0); }});
    A({"v_cross", "VEC", "A × B = AB sinθ", "cross = A * B * sin(theta*pi/180)", "A×B", "", "vectorlab",
       {{"A", "Vector A", "", 1, 20, 5, 0.1}, {"B", "Vector B", "", 1, 20, 5, 0.1}, {"theta", "Angle", "deg", 0, 180, 60, 1}},
       [](auto& v){ return v["A"] * v["B"] * sin(v["theta"] * M_PI / 180.0); }});

    // 10. Electrostatics
    addCat("Electrostatics");
    A({"e_coul", "EST", "F = k q₁q₂ / r²", "F = 8.99e9 * q1 * q2 / r^2", "F⃗", "N", "coulomb",
       {{"q1", "Charge 1", "uC", 0.1, 10, 2, 0.1}, {"q2", "Charge 2", "uC", -10, 10, -2, 0.1}, {"r", "Separation", "m", 0.5, 5, 2, 0.05}},
       [](auto& v){ return 8.99e9 * (v["q1"] * 1e-6) * (v["q2"] * 1e-6) / (v["r"] * v["r"]); }});
    A({"e_field", "EST", "E = F / q", "E = F / q", "E⃗", "N/C", "coulomb",
       {{"F", "Force", "N", 1e-6, 1e-3, 1e-4, 1e-6}, {"q", "Charge", "uC", 0.1, 10, 2, 0.1}},
       [](auto& v){ return v["F"] / (v["q"] * 1e-6); }});
    A({"e_pot", "EST", "V = W / q", "V = W / q", "V", "V", "coulomb",
       {{"W", "Work done", "J", 1e-6, 1e-3, 1e-4, 1e-6}, {"q", "Charge", "uC", 0.1, 10, 2, 0.1}},
       [](auto& v){ return v["W"] / (v["q"] * 1e-6); }});
    A({"e_cap", "EST", "C = Q / V", "C = Q / V", "C", "F", "circuitlab",
       {{"Q", "Charge", "uC", 0.1, 100, 10, 0.5}, {"V", "Voltage", "V", 0.5, 20, 5, 0.1}},
       [](auto& v){ return (v["Q"] * 1e-6) / v["V"]; }});

    // 11. Current Electricity
    addCat("Current Electricity");
    A({"c_ohm", "CUR", "V = I R", "V = I * R", "V", "V", "ohm",
       {{"I", "Current", "A", 0.1, 10, 2, 0.1}, {"R", "Resistance", "Ohm", 1, 100, 6, 0.5}},
       [](auto& v){ return v["I"] * v["R"]; }});
    A({"c_cur", "CUR", "I = Q / t", "I = Q / t", "I", "A", "ohm",
       {{"Q", "Charge", "C", 1, 100, 10, 0.5}, {"t", "Time", "s", 0.5, 20, 5, 0.1}},
       [](auto& v){ return v["Q"] / v["t"]; }});
    A({"c_power", "CUR", "P = V I", "P = V * I", "P", "W", "ohm",
       {{"V", "Voltage", "V", 0.5, 50, 12, 0.5}, {"I", "Current", "A", 0.1, 10, 2, 0.1}},
       [](auto& v){ return v["V"] * v["I"]; }});
    A({"c_drift", "CUR", "I = n A e v_d", "vd = I / (n * A * 1.6e-19)", "vd", "m/s", "ohm",
       {{"I", "Current", "A", 0.1, 10, 2, 0.1}, {"n", "Carrier density", "10^28/m3", 1, 10, 8.5, 0.1}, {"A", "Area", "mm²", 0.1, 5, 1, 0.1}},
       [](auto& v){ return v["I"] / (v["n"]*1e28 * v["A"]*1e-6 * 1.602e-19); }});
    A({"c_series", "CUR", "Series R", "Req = R1 + R2", "Req", "Ohm", "circuitlab",
       {{"R1", "Resistance 1", "Ohm", 1, 100, 10, 0.5}, {"R2", "Resistance 2", "Ohm", 1, 100, 20, 0.5}},
       [](auto& v){ return v["R1"] + v["R2"]; }});
    A({"c_parallel", "CUR", "Parallel R", "Req = (R1*R2)/(R1+R2)", "Req", "Ohm", "circuitlab",
       {{"R1", "Resistance 1", "Ohm", 1, 100, 10, 0.5}, {"R2", "Resistance 2", "Ohm", 1, 100, 20, 0.5}},
       [](auto& v){ return (v["R1"] * v["R2"]) / (v["R1"] + v["R2"]); }});
    A({"c_kirchhoff", "CUR", "Kirchhoff's Laws", "I3 = I1 + I2", "I3", "A", "ohm",
       {{"I1", "Current 1", "A", 0.1, 10, 2, 0.1}, {"I2", "Current 2", "A", 0.1, 10, 3, 0.1}},
       [](auto& v){ return v["I1"] + v["I2"]; }});

    // 12. Magnetism
    addCat("Magnetism");
    A({"m_force", "MAG", "F⃗ = q v⃗ × B⃗ sinθ", "F = q * v * B * sin(theta*pi/180)", "F⃗", "N", "magnetic_force",
       {{"q", "Charge", "uC", 1, 100, 10, 0.5}, {"v", "Velocity", "10^5 m/s", 1, 10, 3, 0.1}, {"B", "B-field", "T", 0.1, 5, 1, 0.05}, {"theta", "Angle", "deg", 0, 180, 90, 1}},
       [](auto& v){ return (v["q"] * 1e-6) * (v["v"] * 1e5) * v["B"] * sin(v["theta"] * M_PI / 180.0); }});
    A({"m_lorentz", "MAG", "F⃗ = q(E⃗ + v⃗×B⃗)", "F = q * (E + v * B)", "F⃗", "N", "magnetic_force",
       {{"q", "Charge", "uC", 1, 100, 10, 0.5}, {"E", "E-field", "V/m", 0, 1000, 200, 5}, {"v", "Velocity", "m/s", 1, 100, 10, 0.5}, {"B", "B-field", "T", 0.1, 5, 1, 0.05}},
       [](auto& v){ return (v["q"] * 1e-6) * (v["E"] + v["v"] * v["B"]); }});
    A({"m_ampere", "MAG", "B = μ₀I / 2\\pi r", "B = 4*pi*1e-7*I/(2*pi*r)", "B", "T", "magfield",
       {{"I", "Current", "A", 0.1, 50, 10, 0.1}, {"r", "Distance", "m", 0.01, 1, 0.1, 0.01}},
       [](auto& v){ return 2e-7 * v["I"] / v["r"]; }});

    // 13. Electromagnetic Induction
    addCat("Electromagnetic Induction");
    A({"i_faraday", "IND", "ε = −dΦ/dt", "emf = -N * dPhi_dt", "ε", "V", "induction",
       {{"N", "Number of turns", "", 10, 500, 100, 5}, {"dPhi_dt", "Flux change rate", "Wb/s", -10, 10, 2, 0.1}},
       [](auto& v){ return -v["N"] * v["dPhi_dt"]; }});
    A({"i_self", "IND", "ε = −L dI/dt", "emf = -L * dI_dt", "ε", "V", "induction",
       {{"L", "Inductance", "H", 0.01, 5, 0.5, 0.01}, {"dI_dt", "Current change rate", "A/s", -50, 50, 10, 0.5}},
       [](auto& v){ return -v["L"] * v["dI_dt"]; }});

    // 14. Alternating Current
    addCat("Alternating Current");
    A({"ac_vrms", "AC", "V_rms = V₀ / √2", "Vrms = V0 / sqrt(2)", "V_rms", "V", "aclab",
       {{"V0", "Peak voltage", "V", 1, 340, 230, 1}},
       [](auto& v){ return v["V0"] / sqrt(2.0); }});
    A({"ac_irms", "AC", "I_rms = I₀ / √2", "Irms = I0 / sqrt(2)", "I_rms", "A", "aclab",
       {{"I0", "Peak current", "A", 0.1, 20, 5, 0.1}},
       [](auto& v){ return v["I0"] / sqrt(2.0); }});
    A({"ac_z", "AC", "Z = √(R²+(X_L−X_C)²)", "Z = sqrt(R^2 + (XL - XC)^2)", "Z", "Ohm", "aclab",
       {{"R", "Resistance", "Ohm", 1, 200, 50, 0.5}, {"XL", "Ind. reactance", "Ohm", 0, 500, 150, 0.5}, {"XC", "Cap. reactance", "Ohm", 0, 500, 100, 0.5}},
       [](auto& v){ return sqrt(v["R"]*v["R"] + (v["XL"] - v["XC"])*(v["XL"] - v["XC"])); }});

    // 15. Optics
    addCat("Optics");
    A({"o_mirror", "OPT", "Mirror formula", "v = u * f / (u - f)", "v", "m", "optics",
       {{"u", "Object distance", "m", -10, -0.1, -3, 0.05}, {"f", "Focal length", "m", -5, 5, 1.5, 0.05}},
       [](auto& v){ return (v["u"] * v["f"]) / (v["u"] - v["f"]); }});
    A({"o_lens", "OPT", "Lens formula", "v = u * f / (u + f)", "v", "m", "optics",
       {{"u", "Object distance", "m", -10, -0.1, -3, 0.05}, {"f", "Focal length", "m", 0.1, 5, 1.5, 0.05}},
       [](auto& v){ return (v["u"] * v["f"]) / (v["u"] + v["f"]); }});
    A({"o_mag", "OPT", "m = v / u", "m = v / u", "m", "", "optics",
       {{"v", "Image distance", "m", 0.1, 10, 3, 0.05}, {"u", "Object distance", "m", 0.1, 10, 2, 0.05}},
       [](auto& v){ return v["v"] / v["u"]; }});
    A({"o_snell", "OPT", "n₁ sinθ₁ = n₂ sinθ₂", "theta2 = asin(n1 * sin(theta1*pi/180) / n2) * 180 / pi", "θ₂", "deg", "snell",
       {{"n1", "Index 1", "", 1, 2.5, 1, 0.05}, {"theta1", "Incident angle", "deg", 1, 89, 45, 1}, {"n2", "Index 2", "", 1, 2.5, 1.5, 0.05}},
       [](auto& v){ double s = v["n1"] * sin(v["theta1"] * M_PI / 180.0) / v["n2"]; return s <= 1.0 ? asin(s)*180.0/M_PI : std::numeric_limits<double>::quiet_NaN(); }});

    // 16. Modern Physics
    addCat("Modern Physics");
    A({"m_pe", "MOD", "hν = φ + KE_max", "KE = 6.626e-34*nu - phi", "KE_max", "J", "photon",
       {{"nu", "Frequency", "10^14 Hz", 1, 20, 8, 0.1}, {"phi", "Work function", "eV", 0.5, 5, 2.0, 0.05}},
       [](auto& v){ return (6.626e-34 * v["nu"] * 1e14) - (v["phi"] * 1.602e-19); }});
    A({"m_db", "MOD", "λ = h / p", "lam = 6.626e-34 / p", "λ", "m", "wave3d",
       {{"p", "Momentum", "10^-24 kgm/s", 0.1, 10, 2, 0.1}},
       [](auto& v){ return 6.626e-34 / (v["p"] * 1e-24); }});
    A({"m_emc2", "MOD", "E = m c²", "E = m * 299792458^2", "E", "J", "photon",
       {{"m", "Mass", "kg", 1e-3, 10, 1, 0.01}},
       [](auto& v){ return v["m"] * 299792458.0 * 299792458.0; }});
    A({"m_decay", "MOD", "N = N₀ e^(−λt)", "N = N0 * exp(-lam * t)", "N", "atoms", "decay",
       {{"N0", "Initial quantity", "atoms", 100, 10000, 1000, 10}, {"lam", "Decay constant", "1/s", 0.001, 0.1, 0.01, 0.001}, {"t", "Time", "s", 0, 500, 100, 1}},
       [](auto& v){ return v["N0"] * exp(-v["lam"] * v["t"]); }});

    if(!g_categories.empty()) g_categories[0].expanded = true;
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
    g_predictChoice = -1;
    g_predictAnswered = false;
    g_vals.clear();
    for(auto& v:g_custom.vars) g_vals[v.sym]=v.val;
    setDefaultCamera(g_custom.viz);
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
   STEM CONCEPT EXPLORER DATABASE & DRAW PANEL
───────────────────────────────────────────────────────── */
struct EqDetail {
    std::string theory;
    std::vector<std::string> var_exps;
    std::string real_world;
    std::string challenge;
};

static std::map<std::string, EqDetail> g_details;

void initEqDetails() {
    g_details["apf_fcc"] = {
        "Face-Centered Cubic (FCC) is a crystal structure where atoms are located at the corners and the centers of all cube faces. The packing factor measures the fraction of volume occupied by atoms in the unit cell.",
        {"r: Atomic radius (distance from center to edge of atom)", "a: Lattice constant (cube edge length, a = 2*sqrt(2)*r)", "Total atoms in cell: 4 (8 corners * 1/8 + 6 faces * 1/2)"},
        "Metals like Copper (Cu), Gold (Au), Aluminum (Al), and Lead (Pb) crystallize in FCC. It represents the closest packing of spheres with 74% density.",
        "Drag the atomic radius slider. Notice that the Atomic Packing Factor (APF) stays exactly at 0.74 (74%). Why? Because the unit cell size (a) automatically resizes with the atomic radius (r)!"
    };

    g_details["apf_bcc"] = {
        "Body-Centered Cubic (BCC) is a crystal structure with atoms at all 8 corners and one single atom at the geometrical center of the cube. It is less dense than FCC.",
        {"r: Atomic radius", "a: Lattice constant (cube edge length, a = 4*r / sqrt(3))", "Total atoms in cell: 2 (8 corners * 1/8 + 1 center)"},
        "Metals like Iron (Fe), Chromium (Cr), Sodium (Na), and Tungsten (W) form BCC lattices at room temperature. BCC has a packing efficiency of 68%.",
        "Observe the 3D cell and note the body-diagonal contact. Compare it with FCC and see why BCC leaves more empty space (32% void volume vs 26% for FCC)."
    };

    g_details["dspacing"] = {
        "Interplanar spacing (d) is the perpendicular distance between adjacent parallel planes of atoms in a crystal lattice. The plane is defined by Miller indices (h, k, l).",
        {"a: Lattice parameter (width of the cubic cell)", "h, k, l: Miller Indices (integer notation representing crystal planes)", "d: Spacing (distance between parallel planes)"},
        "Used in X-ray Diffraction (XRD) and Bragg's Law to identify materials, analyze crystal quality, and measure atomic-level strain.",
        "Increase the Miller indices h, k, and l. Notice how the spacing (d) decreases and the planes get closer together. High-index planes are densely packed but narrowly spaced."
    };

    g_details["einstein_ab"] = {
        "Einstein coefficients quantify the probability of electronic transitions: A represents spontaneous emission, and B represents absorption or stimulated emission. Their ratio relates to photon density.",
        {"nu: Transition frequency of the photon", "h: Planck's constant (6.626e-34 J s)", "c: Speed of light (3e8 m/s)", "A/B: Ratio of spontaneous to stimulated rate"},
        "Explains the physics of light sources. In lasers, we need B (stimulated) to dominate. Since A/B scales with frequency cubed, ultraviolet and X-ray lasers are extremely hard to build.",
        "Increase the frequency. Notice how rapidly the A/B ratio climbs. This shows why spontaneous emission dominates at high frequencies, making lasers difficult in high-energy bands."
    };

    g_details["co2_laser"] = {
        "Quantifies the exponential amplification of light (gain) as it travels through a laser gain medium (excited CO2 molecules) inside a resonant cavity of length L.",
        {"g: Gain coefficient (amplification per unit length)", "L: Cavity length (distance between mirrors)", "I/I0: Output intensity ratio (amplification factor)"},
        "CO2 lasers are high-power gas lasers used extensively in industrial manufacturing for cutting, welding, and laser engraving.",
        "Increase both the cavity length L and the gain coefficient g. Watch the intensity ratio explode exponentially as the optical path length increases."
    };

    g_details["na"] = {
        "Numerical Aperture (NA) is a dimensionless number that characterizes the range of angles over which the optical fiber can accept or emit light. It represents the light-gathering capacity.",
        {"n1: Core refractive index (inner high-index medium)", "n2: Cladding refractive index (outer lower-index medium)", "NA: Light-gathering capacity"},
        "High NA fibers are preferred in short-distance links and medical endoscopes to capture maximum light. Low NA is used in telecommunications to reduce modal dispersion.",
        "Change the refractive indices. See how NA increases when the difference between core and cladding index grows. If n2 becomes greater than n1, NA falls to 0. Why?"
    };

    g_details["acceptance"] = {
        "The Acceptance Angle is the maximum angle (relative to the fiber axis) at which light entering the fiber will undergo Total Internal Reflection (TIR) and propagate down the core.",
        {"n1: Core refractive index", "n2: Cladding refractive index", "theta_a: Half-angle of the acceptance cone"},
        "Essential for optical fiber couplers, splicing, and aligning laser/LED light sources to fiber optic communication cables.",
        "Align n2 closer to n1. Notice the acceptance cone shrink to a narrow line. Total Internal Reflection requires a clear drop in refractive index from core to cladding."
    };

    g_details["cantilever"] = {
        "Relates the bending or vertical depression (delta) at the free end of a clamped beam (cantilever) under an applied load (W) to the material's elastic stiffness (Young's Modulus, Y).",
        {"W: Applied load or force at the free end", "l: Length of the cantilever beam", "b: Breadth (width) of the beam cross-section", "d: Thickness (depth) of the beam cross-section", "delta: Depression at the free end"},
        "Crucial for micro-electromechanical systems (MEMS) sensors, atomic force microscope (AFM) probes, and civil structures like balconies and bridges.",
        "Increase the beam thickness d. Notice how drastically it reduces the beam depression. Because depth is cubed (d^3) in the formula, a minor thickness change yields huge stiffness."
    };

    g_details["torsion"] = {
        "Describes the angular harmonic oscillation of a suspended disk (torsional pendulum). The time period is determined by the disk's moment of inertia and the wire's torsional stiffness.",
        {"I: Moment of inertia of the rotating mass", "C: Torsional rigidity constant of the suspending wire", "T: Time period of one complete oscillation"},
        "Used in mechanical clocks (the balance wheel serves as a torsional oscillator) and to measure the shear modulus of structural metal fibers.",
        "Double the moment of inertia I and watch the pendulum swing slower (period increases). Increase the torsional constant C and watch it snap back rapidly (period decreases)."
    };

    g_details["compton"] = {
        "Compton scattering occurs when an X-ray or gamma photon collides with an electron, transferring momentum. The scattered photon has lower energy and a longer wavelength (wavelength shift).",
        {"theta: Scattering angle of the photon", "dL: Wavelength shift (Compton shift)", "h / (me * c): Compton wavelength (approx 2.43 pm)"},
        "A cornerstone experiment proving that electromagnetic radiation behaves as particles (photons) carrying momentum, not just classical waves.",
        "Drag the scattering angle theta. At 180 degrees (backscattering), the photon rebounds directly back, transferring maximum kinetic energy and maximizing the wavelength shift."
    };

    g_details["debroglie"] = {
        "The de-Broglie wavelength describes the wave-particle duality of matter. It states that any moving particle has an associated wavelength inversely proportional to its momentum.",
        {"m: Mass of the particle (e.g. electron mass is 9.11e-31 kg)", "v: Velocity of the moving particle", "lambda: Matter wavelength"},
        "Enables electron microscopy. Because high-speed electrons have extremely short de-Broglie wavelengths, they resolve details 100,000x smaller than visible light.",
        "Reduce the mass m and velocity v. Watch the wave frequency and spacing shift. Subatomic particles at lower speeds exhibit wave properties on macroscopic scales."
    };

    g_details["quantum_box"] = {
        "A fundamental quantum model where a particle is confined in an infinitely deep potential box. The particle cannot escape, and its energy states are restricted to discrete levels.",
        {"n: Quantum state energy level (integer 1, 2, 3...)", "a: Width of the 1D quantum box (nanometer scale)", "m: Mass of the particle", "En: Quantized energy of the state"},
        "Explains quantum dots (nanocrystals emitting colors based on size, used in QLED TVs), semiconductor band gaps, and molecular organic dyes.",
        "Shrink the box size a. Notice how the energy levels shoot up dramatically. This demonstrates quantum confinement: smaller boxes force higher kinetic energies."
    };

    g_details["planck"] = {
        "Planck's radiation law describes the spectral distribution of electromagnetic energy emitted by a blackbody in thermal equilibrium. The peak wavelength is inversely proportional to temperature.",
        {"T: Absolute temperature of the blackbody", "lambda_peak: Wavelength of peak emission intensity", "b: Wien's constant (approx 2.898 mm K)"},
        "Used to measure the temperature of stars (stellar spectroscopy), detect cosmic microwave background radiation, and design non-contact infrared thermometers.",
        "Slide the temperature up. Watch the emission curve peak shift from the infrared region into the visible spectrum, going from red hot to yellow, and then white hot."
    };

    g_details["fourier"] = {
        "Fourier's Law of Heat Conduction states that the rate of heat transfer through a material is proportional to the negative temperature gradient and the perpendicular area.",
        {"k: Thermal conductivity (material-dependent rate)", "A: Heat transfer surface area", "dT: Temperature difference across the material", "dx: Thickness of the material barrier", "Q/t: Heat transfer rate (Power)"},
        "Used to design insulation for houses, thermal interface materials for CPU heatsinks, and heat shields for space capsules re-entering Earth's atmosphere.",
        "Increase the thickness dx and watch the heat flow rate decline. Increase the conductivity k (e.g. copper vs wood) to speed up heat dissipation."
    };

    g_details["snell"] = {
        "Snell's Law describes the relationship between the angles of incidence and refraction when light passes through a boundary between two isotropic media with different refractive indices.",
        {"n1: Refractive index of medium 1", "t1: Angle of incidence (degrees)", "n2: Refractive index of medium 2", "theta2: Angle of refraction"},
        "Used in optical design for eyeglasses, cameras, microscopes, telescopes, and understanding atmospheric refraction effects like mirages.",
        "Set index n1 greater than n2. Gradually increase the angle of incidence t1. Watch for the angle where refraction stops and Total Internal Reflection begins!"
    };

    /* ── Details for Class 11 & 12 equations  (Ages 15–17) ── */
    g_details["newton2"] = {
        "Newton's Second Law of Motion states that the net force acting on a body is equal to the rate of change of its momentum. For constant mass, F = ma. This single equation governs the motion of everything from billiard balls to rockets.",
        {"F: Net force applied on the object (Newtons)", "m: Mass of the object (kilograms)", "a: Resulting acceleration (m/s^2)"},
        "Every vehicle acceleration, rocket thrust, and satellite orbit is calculated using F = ma. Engineers use it to size motors and predict trajectories.",
        "Increase the Force F while keeping Mass constant. Watch the block speed up. Then increase Mass with the same Force — it slows down. That tradeoff IS Newton's 2nd Law."
    };

    g_details["kinematics_v"] = {
        "The first kinematic equation v = u + at relates the final velocity of an object to its initial velocity, constant acceleration, and elapsed time. It assumes uniform (constant) acceleration throughout the motion.",
        {"u: Initial velocity at t=0", "a: Constant acceleration (can be negative for deceleration)", "t: Elapsed time", "v: Final velocity"},
        "Used to calculate braking distances for cars, runway lengths for aircraft, and the speed of a ball at any point during its fall.",
        "Set initial velocity u to 0 and watch the ball roll from rest. Change acceleration a to negative and see the object slow to a stop. The v-t graph shows the linear relationship."
    };

    g_details["projectile"] = {
        "Projectile motion combines horizontal constant velocity with vertical gravitational acceleration. The range formula R = u^2 sin(2theta)/g gives the horizontal distance traveled when the projectile lands at the same height it was launched.",
        {"u: Launch speed (m/s)", "ang: Launch angle above horizontal (degrees)", "R: Horizontal range", "g: Gravitational acceleration (9.8 m/s^2)"},
        "Ballistics, sports science (long jump, javelin), and orbital insertion all apply projectile motion principles. The 45 degree angle always gives maximum range on flat ground.",
        "Change the launch angle. Notice range is maximum at 45 degrees. Also notice that angles equidistant from 45 degrees give the same range — e.g. 30 and 60 degrees land at the same spot!"
    };

    g_details["shm_period"] = {
        "Simple Harmonic Motion (SHM) occurs when the restoring force is proportional to displacement. A mass on a spring undergoes SHM with period T = 2 * pi * sqrt(m/k). The period is independent of amplitude — a key property of SHM.",
        {"k: Spring stiffness constant (N/m) — how stiff the spring is", "m: Mass attached to the spring", "A: Amplitude (how far it stretches)", "T: Time for one full oscillation"},
        "SHM underpins quartz crystal oscillators in clocks, suspension systems in vehicles, and seismographs for earthquake detection.",
        "Change the amplitude A — notice the period DOES NOT change! Only mass m and spring constant k affect the period. This amplitude-independence is the defining feature of SHM."
    };

    g_details["pendulum"] = {
        "A simple pendulum of length l swings with period T = 2 * pi * sqrt(l/g) for small angles. The period depends only on the length and local gravity — NOT the mass of the bob or how wide it swings (for small angles).",
        {"l: Length of the pendulum string (metres)", "g: Local gravitational acceleration", "A: Amplitude of swing (radians)", "T: Period of oscillation"},
        "Grandfather clocks use pendulums for precise timekeeping. Foucault's pendulum famously demonstrated Earth's rotation in 1851.",
        "Double the length l. Notice the period increases by sqrt(2), not by 2. Try swinging at different amplitudes — the period barely changes. This is why pendulums make great clocks!"
    };

    g_details["ohm"] = {
        "Ohm's Law states that the current through a conductor between two points is directly proportional to the voltage across the two points and inversely proportional to the resistance. V = I * R (or equivalently I = V/R).",
        {"V: Voltage (potential difference across the circuit, in Volts)", "R: Resistance of the conductor (Ohms)", "I: Current flowing through the circuit (Amperes)"},
        "Every electronic circuit — your phone charger, LED lights, computer — is designed using Ohm's Law. It determines resistor values, current limits, and heat dissipation.",
        "Increase the voltage V. Watch more electrons flow (brighter dots, faster). Increase the resistance R. Fewer electrons flow for the same voltage. This is Ohm's Law in action."
    };

    g_details["coulomb"] = {
        "Coulomb's Law describes the electrostatic force between two point charges. The force is proportional to the product of the charges and inversely proportional to the square of the distance between them.",
        {"q1, q2: Electric charges (Coulombs) — same sign = repulsion, opposite = attraction", "r: Separation distance between the charges", "k: Coulomb's constant (8.99 x 10^9 Nm^2/C^2)", "F: Electrostatic force"},
        "Explains how atoms are held together (nucleus attracting electrons), how ions bond in salts, and why charged particles deflect in CRT displays and mass spectrometers.",
        "Make q2 negative while q1 is positive. The arrows reverse — attraction! Then slowly increase the separation r and watch the force drop rapidly. Doubling r reduces force by 4x (inverse square law)."
    };

    g_details["magforce"] = {
        "A current-carrying conductor placed in a magnetic field experiences a force perpendicular to both the current direction and the magnetic field. F = BIL sin(theta). This is the motor principle.",
        {"B: Magnetic field strength (Tesla)", "I: Current through the wire (Amperes)", "L: Length of wire inside the magnetic field", "F: Force on the wire (Newtons)"},
        "Electric motors in every fan, pump, electric car, and industrial machine work on this principle. Loudspeakers also use magnetic force on current-carrying voice coils.",
        "Increase the current I or field B and watch the wire deflect more forcefully. This is exactly how an electric motor converts electrical energy into mechanical rotation."
    };

    g_details["wave_speed"] = {
        "The wave speed equation v = f * lambda relates the speed of a wave to its frequency (how many cycles per second) and wavelength (length of one complete cycle). This applies to all waves: sound, light, water, and seismic.",
        {"f: Frequency of the wave (Hertz, cycles per second)", "lam: Wavelength (metres per cycle)", "v: Wave speed (m/s) — fixed for a given medium"},
        "Radio stations broadcast at specific frequencies. Mobile networks use this to assign channels. The visible spectrum is different frequencies of the same electromagnetic wave (light).",
        "Change frequency while the simulation runs. Notice shorter wavelengths correspond to higher frequencies. In a given medium, wave speed is fixed — only frequency and wavelength trade off against each other."
    };

    g_details["grav_pe"] = {
        "Gravitational Potential Energy is the energy stored in an object due to its height above a reference point. PE = mgh. When an object falls, this energy converts to kinetic energy (KE = 0.5mv^2).",
        {"m: Mass of the object (kg)", "g: Gravitational field strength (9.8 m/s^2 on Earth's surface)", "h: Height above the reference level (metres)", "PE: Gravitational potential energy (Joules)"},
        "Hydroelectric dams store gravitational PE in elevated water and convert it to electrical energy. Roller coasters trade height for speed using this exact energy conversion.",
        "Raise the height h and watch the red bar (PE) grow while the green bar (KE on landing) grows too. Drop the ball — all PE converts to KE. Total mechanical energy is always conserved!"
    };
}

static void addProfessionalLearningDetails() {
    g_details["f_pressure"] = {
        "Pressure measures how concentrated a force is over an area. The same force can feel gentle on a large plate and intense on a small point because P = F/A.",
        {"F: Normal force pushing on the surface", "A: Contact area receiving the force", "P: Pressure, or force per square metre"},
        "Hydraulic lifts, snow shoes, cutting tools, tyres, and medical syringes all depend on controlling pressure by changing force or area.",
        "Keep force fixed and reduce area. The plate compresses more because the force is concentrated into a smaller contact patch."
    };
    g_details["w_work"] = {
        "Work is energy transferred when a force moves an object through a displacement. Only the component of force along the motion contributes: W = F s cos(theta).",
        {"F: Applied force", "s: Displacement along the ground", "theta: Angle between force and displacement", "W: Energy transferred as work"},
        "Used in machines, engines, sports biomechanics, and energy accounting for every moving system.",
        "Turn theta toward 90 degrees. The force arrow still exists, but work falls because less of the force points along the displacement."
    };
    g_details["w_powt"] = {
        "Power tells us how quickly work is done. Two machines may perform the same work, but the one doing it in less time has greater power.",
        {"W: Work or energy transferred", "t: Time interval", "P: Rate of doing work"},
        "Motor ratings, electrical appliances, turbines, and vehicle engines are compared using power.",
        "Increase work or reduce time. The power meter rises because energy is being delivered faster."
    };
    g_details["w_beta"] = {
        "Fringe width in double-slit interference is beta = lambda D / d. Larger wavelength or screen distance spreads fringes apart; wider slit separation packs them closer.",
        {"lambda: Wavelength of light", "D: Distance to screen", "d: Separation between slits", "beta: Distance between adjacent bright bands"},
        "Interference patterns are used to measure tiny wavelengths, test optical quality, and demonstrate wave behavior of light.",
        "Increase lambda or D, then increase d. Watch the bright bands expand and contract on the screen."
    };
    g_details["v_res"] = {
        "The resultant vector is the single vector that has the same effect as two vectors acting together. The angle controls how much the vectors help or oppose one another.",
        {"A: First vector magnitude", "B: Second vector magnitude", "theta: Angle between vectors", "R: Combined resultant magnitude"},
        "Navigation, forces on structures, robotics, and aircraft motion all rely on vector addition.",
        "Move theta from 0 to 180 degrees. The resultant shrinks as the vectors stop pointing in the same direction."
    };
    g_details["v_dot"] = {
        "The dot product measures how much one vector points along another: A dot B = AB cos(theta). It is largest when vectors align and zero when perpendicular.",
        {"A: First vector magnitude", "B: Second vector magnitude", "theta: Angle between vectors", "A dot B: Parallel overlap"},
        "Work done by a force, light intensity on a surface, and projections in computer graphics use dot products.",
        "Set theta to 90 degrees. The vectors still have size, but their dot product becomes zero."
    };
    g_details["v_cross"] = {
        "The cross product measures the perpendicular effect of two vectors: |A x B| = AB sin(theta). It is zero when vectors are parallel and largest at 90 degrees.",
        {"A: First vector magnitude", "B: Second vector magnitude", "theta: Angle between vectors", "A x B: Perpendicular turning effect"},
        "Torque, angular momentum, magnetic force, and 3D geometry all use cross products.",
        "Drag theta to 90 degrees to maximize the perpendicular effect, then to 0 degrees to make it vanish."
    };
    g_details["e_cap"] = {
        "Capacitance describes how much charge a device stores for a given voltage. A capacitor stores separated positive and negative charges on two plates.",
        {"Q: Stored charge", "V: Voltage between plates", "C: Charge stored per volt"},
        "Capacitors smooth power supplies, tune radios, flash cameras, and store quick bursts of electrical energy.",
        "Increase charge while voltage stays low. The visual charge density grows, showing higher storage capacity."
    };
    g_details["c_series"] = {
        "Increase either resistor. The flow dots slow because the single path becomes harder to pass through."
    };

    g_details["c_parallel"] = {
        "In parallel, current has multiple paths. Adding a branch gives current another route, so the equivalent resistance becomes smaller than either branch alone.",
        {"R1: First branch resistance", "R2: Second branch resistance", "Req: Combined resistance of the branches"},
        "House wiring, circuit protection, and electronic loads use parallel branches so devices can operate independently.",
        "Lower one branch resistance and watch the circuit behave as though it has an easier path."
    };

    g_details["ac_z"] = {
        "AC impedance combines resistance with inductive and capacitive reactance. When XL and XC differ, voltage and current shift phase.",
        {"R: Resistance", "XL: Inductive reactance", "XC: Capacitive reactance", "Z: Total impedance"},
        "Power grids, speakers, filters, motors, and radio circuits all depend on AC impedance and phase.",
        "Make XL equal XC. The phase shift shrinks because the inductive and capacitive effects cancel."
    };
    g_details["ac_vrms"] = g_details["ac_z"];
    g_details["ac_irms"] = g_details["ac_z"];
    g_details["o_lens"] = {
        "The lens formula connects object distance, focal length, and image distance. Moving the object changes where the image forms and whether it is magnified.",
        {"u: Object distance", "f: Focal length", "v: Image distance", "m: Magnification when comparing image and object size"},
        "Cameras, microscopes, spectacles, projectors, and telescopes are designed around this relationship.",
        "Move the object closer to the focal point. The image position moves dramatically, which is why focusing lenses is so sensitive."
    };
    g_details["o_mirror"] = g_details["o_lens"];
    g_details["o_mag"] = g_details["o_lens"];
    g_details["m_decay"] = {
        "Exponential decay means a fixed fraction disappears in each equal time interval. The curve drops quickly at first, then slowly approaches zero.",
        {"N0: Initial number of atoms", "lambda: Decay constant", "t: Elapsed time", "N: Remaining quantity"},
        "Radioactive dating, medical tracers, nuclear safety, and drug concentration models use exponential decay.",
        "Increase lambda. The moving point falls faster because each second removes a larger fraction of the remaining amount."
    };
}

std::string getEqTheory(const std::string& id) {
    if (g_details.count(id)) return g_details[id].theory;
    Equation& eq = curEq();
    return "The equation " + eq.name + " is given by the mathematical expression: " + eq.formula + ". It represents the relationship for " + eq.result + " (measured in " + (eq.unit.empty() ? "units" : eq.unit) + ") based on its corresponding variables. Adjust the sliders below to explore its behavior live.";
}

std::vector<std::string> getEqVarExps(const std::string& id) {
    if (g_details.count(id)) return g_details[id].var_exps;
    std::vector<std::string> exps;
    Equation& eq = curEq();
    for (const auto& v : eq.vars) {
        exps.push_back(v.sym + ": " + v.name + (v.unit.empty() ? "" : " (" + v.unit + ")"));
    }
    return exps;
}

std::string getEqRealWorld(const std::string& id) {
    if (g_details.count(id)) return g_details[id].real_world;
    Equation& eq = curEq();
    return "This equation is widely used in scientific research, physical modeling, and structural/electrical engineering to predict and calculate " + eq.name + ".";
}

std::string getEqChallenge(const std::string& id) {
    if (g_details.count(id)) return g_details[id].challenge;
    return "Observe how the calculated output scales as you drag each variable slider in the bottom controls panel. Which variables have a linear, quadratic, or inverse-square relationship with the output?";
}

// Build context-aware system prompt for the AI tutor chatbot
std::string buildSystemPrompt() {
    Equation& eq = curEq();
    std::string prompt = "You are a friendly, encouraging physics tutor for students aged 15-17. "
        "You are currently helping with the topic: \"" + eq.name + "\". "
        "The equation is: " + eq.formula + ". "
        "The result variable is: " + eq.result + " (" + eq.unit + "). ";

    std::string theory = getEqTheory(eq.id);
    if (!theory.empty()) {
        prompt += "Theory: " + theory + " ";
    }

    prompt += "Variables: ";
    for (size_t i = 0; i < eq.vars.size(); i++) {
        if (i > 0) prompt += ", ";
        prompt += eq.vars[i].sym + " (" + eq.vars[i].name;
        if (!eq.vars[i].unit.empty()) prompt += ", " + eq.vars[i].unit;
        prompt += ")";
    }
    prompt += ". ";

    prompt += "Rules: "
        "1) Keep answers concise (2-4 sentences for simple questions, more for complex ones). "
        "2) Use simple, clear language suitable for high school students. "
        "3) Relate answers to the current simulation when possible. "
        "4) If asked about unrelated topics, gently redirect to physics. "
        "5) Use examples and analogies to explain concepts. "
        "6) Encourage experimentation with the sliders in the simulation.";

    return prompt;
}

struct PredictionCard {
    std::string prompt;
    std::string choices[3];
    int correct = 0;
    std::string explanation;
};

PredictionCard makePredictionCard() {
    PredictionCard pc;
    Equation& eq = curEq();
    pc.choices[0] = "Result increases";
    pc.choices[1] = "Result decreases";
    pc.choices[2] = "Almost no change";

    if (eq.vars.empty()) {
        pc.prompt = "Predict what will change when you run this simulation.";
        pc.correct = 2;
        pc.explanation = "This scene has no adjustable variables, so observation is the experiment.";
        return pc;
    }

    Var v0 = eq.vars[0];
    std::map<std::string,double> base = g_vals;
    std::map<std::string,double> changed = g_vals;
    double span = std::max(1e-9, v0.mx - v0.mn);
    double bump = std::max(v0.step, span * 0.20);
    changed[v0.sym] = std::min(v0.mx, std::max(v0.mn, base[v0.sym] + bump));
    if (fabs(changed[v0.sym] - base[v0.sym]) < 1e-12)
        changed[v0.sym] = std::max(v0.mn, base[v0.sym] - bump);

    double a = std::numeric_limits<double>::quiet_NaN();
    double b = std::numeric_limits<double>::quiet_NaN();
    try { a = eq.calc(base); b = eq.calc(changed); } catch(...) {}
    double delta = b - a;
    double scale = std::max(1.0, fabs(a));
    if (std::isnan(delta) || std::isinf(delta) || fabs(delta) < scale * 0.02) pc.correct = 2;
    else pc.correct = delta > 0 ? 0 : 1;

    pc.prompt = "Prediction: if " + v0.sym + " increases, what happens to " + eq.result + "?";
    pc.explanation = v0.sym + " changed from " + fmt(base[v0.sym]) + " to " + fmt(changed[v0.sym]) +
                     ". The result moved from " + fmt(a) + " to " + fmt(b) + ".";
    return pc;
}

std::string causeEffectLine() {
    Equation& eq = curEq();
    if (eq.vars.empty()) return "Observe -> compare -> explain";
    std::string s;
    for (int i=0; i<(int)eq.vars.size() && i<3; i++) {
        if (i) s += " + ";
        s += eq.vars[i].sym;
    }
    return s + " -> " + eq.result + " -> visible motion";
}

void drawInfoPanel() {
    if (g_rightCollapsed) return;

    float ix = (float)(WIN_W - INFO_W);
    float iy = (float)TB_H;
    float iw = (float)INFO_W;
    float ih = (float)(WIN_H - TB_H);

    // Draw background (dark gray-navy)
    glColor4f(BG1.r, BG1.g, BG1.b, 0.98f);
    glBegin(GL_QUADS);
    glVertex2f(ix, iy); glVertex2f(ix + iw, iy); glVertex2f(ix + iw, iy + ih); glVertex2f(ix, iy + ih);
    glEnd();

    // Border line on the left of the Info Panel
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.22f);
    glBegin(GL_LINES);
    glVertex2f(ix, iy); glVertex2f(ix, iy + ih);
    glEnd();

    auto& eq = curEq();
    int eqIdx = g_selIdx; // -1 for custom

    // Get the details
    std::string theory = "";
    std::vector<std::string> var_exps;
    std::string real_world = "";
    std::string challenge = "";

    if (eqIdx == -1) {
        theory = "This is a custom physical equation parsed dynamically in real-time. Define any formula like Result = expression (e.g. F = m * a). The system automatically generates sliders and renders a 3D vector field scaled to the result.";
        var_exps.push_back("Custom variables are auto-generated below.");
        real_world = "Used for prototyping new physical concepts, visualizing custom math surfaces, and interactive testing.";
        challenge = "Try typing: y = A * sin(k * x - w * t) and press enter to visualize!";
    } else {
        theory = getEqTheory(eq.id);
        var_exps = getEqVarExps(eq.id);
        real_world = getEqRealWorld(eq.id);
        challenge = getEqChallenge(eq.id);
    }

    const float P  = 16.f;          // horizontal padding
    const float SW = iw - P * 2.f;  // usable text width
    const int   CW = 9;             // Consolas char width at sz=15
    const int   LH = 21;            // line height (15px + 6px leading)
    float cy = iy + 8.f;

    // ── Panel Title bar ──────────────────────────────────────────
    glColor4f(BG2.r, BG2.g, BG2.b, 0.95f);
    glBegin(GL_QUADS);
    glVertex2f(ix, cy); glVertex2f(ix + iw, cy);
    glVertex2f(ix + iw, cy + 38); glVertex2f(ix, cy + 38);
    glEnd();
    gc(ACCENT, 0.9f);
    glBegin(GL_QUADS);
    glVertex2f(ix, cy); glVertex2f(ix + 3, cy);
    glVertex2f(ix + 3, cy + 38); glVertex2f(ix, cy + 38);
    glEnd();
    txt(ix + P, cy + 26, "STEM CONCEPT EXPLORER", 18, ACCENT3, 1.0f);
    cy += 46;

    // Thin accent divider
    gc(ACCENT, 0.25f);
    glBegin(GL_LINES);
    glVertex2f(ix + P, cy); glVertex2f(ix + iw - P, cy);
    glEnd();
    cy += 10;

    // ── Helper: draw a section header pill, return cy past the pill ──
    // pill: 28px tall, label baseline at pill_top+20, then 10px gap to content
    auto sectionHeader = [&](const char* label, C3 col) {
        float pill_top = cy;
        float pill_h   = 26.f;
        // background pill
        glColor4f(BG2.r, BG2.g, BG2.b, 0.8f);
        glBegin(GL_QUADS);
        glVertex2f(ix + P - 4, pill_top);
        glVertex2f(ix + iw - P + 4, pill_top);
        glVertex2f(ix + iw - P + 4, pill_top + pill_h);
        glVertex2f(ix + P - 4, pill_top + pill_h);
        glEnd();
        // left colour bar
        gc(col, 0.7f);
        glBegin(GL_QUADS);
        glVertex2f(ix + P - 4, pill_top);
        glVertex2f(ix + P - 1, pill_top);
        glVertex2f(ix + P - 1, pill_top + pill_h);
        glVertex2f(ix + P - 4, pill_top + pill_h);
        glEnd();
        // label text — baseline = pill_top + 18
        txt(ix + P + 4, pill_top + 18, label, 16, col, 1.0f);
        cy = pill_top + pill_h + 8; // gap after pill before content
    };

    // ── Helper: estimate wrapped line count ──────────────────────
    auto lineCount = [&](const std::string& s) -> int {
        int maxCPL = std::max(1, (int)(SW / CW));
        // rough word-wrap count
        int words = 1; for (char c : s) if (c == ' ') words++;
        int chars = (int)s.size();
        return std::max(1, (chars + maxCPL - 1) / maxCPL);
    };


    PredictionCard pred = makePredictionCard();
    glColor4f(BG0.r, BG0.g, BG0.b, 0.72f);
    glBegin(GL_QUADS);
    glVertex2f(ix + P - 4, cy);
    glVertex2f(ix + iw - P + 4, cy);
    glVertex2f(ix + iw - P + 4, cy + 146);
    glVertex2f(ix + P - 4, cy + 146);
    glEnd();
    txt(ix + P + 2, cy + 18, "PREDICT FIRST", 16, CYELLOW);
    txt(ix + iw - P - 62, cy + 18, "XP " + fmt(g_learningXP), 16, ACCENT2, 0.9f);
    drawWrappedTextBody(ix + P + 2, cy + 42, pred.prompt, (int)(SW - 4), 15, TEXT0, 0.95f);
    for (int i=0; i<3; i++) {
        float by = cy + 58 + i * 27;
        bool picked = g_predictChoice == i;
        C3 col = !g_predictAnswered ? ACCENT2 : (i == pred.correct ? CGREEN : (picked ? HOT : TEXT2));
        glColor4f(BG2.r, BG2.g, BG2.b, picked ? 0.95f : 0.62f);
        glBegin(GL_QUADS);
        glVertex2f(ix + P + 2, by); glVertex2f(ix + iw - P - 2, by);
        glVertex2f(ix + iw - P - 2, by + 22); glVertex2f(ix + P + 2, by + 22);
        glEnd();
        txt(ix + P + 10, by + 16, pred.choices[i], 15, col, picked || g_predictAnswered ? 1.0f : 0.78f);
    }
    txt(ix + P + 2, cy + 137, g_predictAnswered ? (g_predictChoice == pred.correct ? "Discovery: correct prediction." : "Discovery: test it with sliders.") : "Choose an answer, then experiment.", 15, g_predictAnswered && g_predictChoice == pred.correct ? CGREEN : TEXT1, 0.9f);
    cy += 158;

    sectionHeader("CAUSE & EFFECT", ACCENT2);
    drawWrappedTextBody(ix + P, cy, causeEffectLine(), (int)SW, 15, ACCENT2, 0.95f);
    cy += LH + 12;
    // ── SECTION 1: Physics Theory ────────────────────────────────
    sectionHeader("PHYSICS THEORY", ACCENT);
    drawWrappedTextBody(ix + P, cy, theory, (int)SW, 15, TEXT0, 0.92f);
    cy += lineCount(theory) * LH + 16;

    // divider
    glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.3f);
    glBegin(GL_LINES); glVertex2f(ix + P, cy); glVertex2f(ix + iw - P, cy); glEnd();
    cy += 10;

    // ── SECTION 2: Variable Breakdown ────────────────────────────
    sectionHeader("VARIABLE BREAKDOWN", ACCENT2);
    for (const auto& ve : var_exps) {
        drawWrappedTextBody(ix + P + 6, cy, "  " + ve, (int)(SW - 6), 15, TEXT1, 0.92f);
        cy += lineCount(ve) * LH + 6;
    }
    cy += 10;

    // divider
    glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.3f);
    glBegin(GL_LINES); glVertex2f(ix + P, cy); glVertex2f(ix + iw - P, cy); glEnd();
    cy += 10;

    // ── SECTION 3: Real-World Application ───────────────────────
    sectionHeader("REAL-WORLD APPLICATION", CYELLOW);
    drawWrappedTextBody(ix + P, cy, real_world, (int)SW, 15, CYELLOW, 0.88f);
    cy += lineCount(real_world) * LH + 16;

    // divider
    glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.3f);
    glBegin(GL_LINES); glVertex2f(ix + P, cy); glVertex2f(ix + iw - P, cy); glEnd();
    cy += 10;

    // ── SECTION 4: Learning Challenge ────────────────────────────
    sectionHeader("LEARNING CHALLENGE", HOT);
    drawWrappedTextBody(ix + P, cy, challenge, (int)SW, 15, HOT, 0.92f);
    cy += lineCount(challenge) * LH + 16;

    // divider
    glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.3f);
    glBegin(GL_LINES); glVertex2f(ix + P, cy); glVertex2f(ix + iw - P, cy); glEnd();
    cy += 10;

    // -- SECTION 5: AI Tutor Chatbot --
    sectionHeader("AI TUTOR", CGREEN);

    // Connection status indicator
    {
        float dotX = ix + iw - P - 8;
        float dotY = cy - 20;
        C3 statusCol = g_ollamaAvailable ? CGREEN : CRED;
        float pulse = g_chatLoading.load() ? (0.5f + 0.5f * sinf(T() * 6.f)) : 1.0f;
        gc(statusCol, pulse * 0.9f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(dotX, dotY);
        for (int i = 0; i <= 12; i++) {
            float a = 2.f * (float)M_PI * i / 12.f;
            glVertex2f(dotX + 4.f * cosf(a), dotY + 4.f * sinf(a));
        }
        glEnd();
    }

    // Chat area background
    float chatAreaTop = cy;
    float chatAreaH = ih - cy + iy - 85.f;
    if (chatAreaH < 80.f) chatAreaH = 80.f;
    float chatAreaBot = chatAreaTop + chatAreaH;

    glColor4f(BG0.r, BG0.g, BG0.b, 0.65f);
    glBegin(GL_QUADS);
    glVertex2f(ix + P - 4, chatAreaTop);
    glVertex2f(ix + iw - P + 4, chatAreaTop);
    glVertex2f(ix + iw - P + 4, chatAreaBot);
    glVertex2f(ix + P - 4, chatAreaBot);
    glEnd();

    // Scissor for chat scroll
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)(ix + P - 4), (int)(WIN_H - chatAreaBot), (int)(iw - 2*P + 8), (int)chatAreaH);

    // Render chat messages
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        float msgY = chatAreaTop + 6.f - g_infoPanelScroll;

        if (g_chatMessages.empty()) {
            drawWrappedTextBody(ix + P + 2, msgY + 4, "Ask me anything about this topic! I am your AI physics tutor.", (int)(SW - 8), 13, TEXT2, 0.7f);
            msgY += 40;
        }

        for (size_t mi = 0; mi < g_chatMessages.size(); mi++) {
            const ChatMessage& msg = g_chatMessages[mi];
            int msgLines = std::max(1, (int)(msg.text.size() * CW) / (int)(SW - 28) + 1);
            float msgH = (float)(msgLines * 19 + 10);

            if (msgY + msgH < chatAreaTop - 20) { msgY += msgH + 6; continue; }
            if (msgY > chatAreaBot + 20) break;

            if (msg.isUser) {
                glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.15f);
                glBegin(GL_QUADS);
                glVertex2f(ix + P + 20, msgY);
                glVertex2f(ix + iw - P, msgY);
                glVertex2f(ix + iw - P, msgY + msgH);
                glVertex2f(ix + P + 20, msgY + msgH);
                glEnd();
                gc(ACCENT, 0.6f);
                glBegin(GL_QUADS);
                glVertex2f(ix + iw - P - 3, msgY + 2);
                glVertex2f(ix + iw - P, msgY + 2);
                glVertex2f(ix + iw - P, msgY + msgH - 2);
                glVertex2f(ix + iw - P - 3, msgY + msgH - 2);
                glEnd();
                drawWrappedTextBody(ix + P + 24, msgY + 15, msg.text, (int)(SW - 32), 13, ACCENT2, 0.95f);
            } else {
                glColor4f(BG2.r, BG2.g, BG2.b, 0.85f);
                glBegin(GL_QUADS);
                glVertex2f(ix + P, msgY);
                glVertex2f(ix + iw - P - 20, msgY);
                glVertex2f(ix + iw - P - 20, msgY + msgH);
                glVertex2f(ix + P, msgY + msgH);
                glEnd();
                gc(CGREEN, 0.5f);
                glBegin(GL_QUADS);
                glVertex2f(ix + P, msgY + 2);
                glVertex2f(ix + P + 3, msgY + 2);
                glVertex2f(ix + P + 3, msgY + msgH - 2);
                glVertex2f(ix + P, msgY + msgH - 2);
                glEnd();
                drawWrappedTextBody(ix + P + 6, msgY + 15, msg.text, (int)(SW - 32), 13, TEXT0, 0.9f);
            }
            msgY += msgH + 6;
        }

        if (g_chatLoading.load()) {
            std::string dots = "Thinking";
            int dotCount = ((int)(g_time * 3)) % 4;
            for (int d = 0; d < dotCount; d++) dots += ".";
            drawWrappedTextBody(ix + P + 4, msgY + 12, dots, (int)(SW - 8), 13, CGREEN, 0.7f + 0.3f * sinf(T() * 4.f));
        }
    }

    glDisable(GL_SCISSOR_TEST);

    // Chat input area
    float inputY = chatAreaBot + 6;
    float inputH = 30.f;
    float sendBtnW = 52.f;

    C3 inputBorder = g_chatInputActive ? CGREEN : BORDER;
    glColor4f(BG2.r, BG2.g, BG2.b, 1.f);
    glBegin(GL_QUADS);
    glVertex2f(ix + P - 2, inputY);
    glVertex2f(ix + iw - P - sendBtnW - 6, inputY);
    glVertex2f(ix + iw - P - sendBtnW - 6, inputY + inputH);
    glVertex2f(ix + P - 2, inputY + inputH);
    glEnd();
    gc(inputBorder, g_chatInputActive ? 0.8f : 0.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(ix + P - 2, inputY);
    glVertex2f(ix + iw - P - sendBtnW - 6, inputY);
    glVertex2f(ix + iw - P - sendBtnW - 6, inputY + inputH);
    glVertex2f(ix + P - 2, inputY + inputH);
    glEnd();

    std::string chatDisp = g_chatInput;
    int maxChatChars = (int)((SW - sendBtnW - 16) / 8);
    if ((int)chatDisp.size() > maxChatChars)
        chatDisp = chatDisp.substr(chatDisp.size() - maxChatChars);
    if (g_chatInputActive && fmod(g_time, 1.0) < 0.5)
        chatDisp += "|";
    if (chatDisp.empty() && !g_chatInputActive)
        txtBody(ix + P + 4, inputY + 20, "Ask a doubt...", 13, TEXT2, 0.5f);
    else
        txtBody(ix + P + 4, inputY + 20, chatDisp, 13, g_chatInputActive ? TEXT0 : TEXT1, 0.9f);

    // Send button
    double cmx, cmy;
    glfwGetCursorPos(g_win, &cmx, &cmy);
    bool sendHovered = (cmx >= ix + iw - P - sendBtnW - 2 && cmx <= ix + iw - P + 2
                     && cmy >= inputY && cmy <= inputY + inputH);
    bool canSend = !g_chatInput.empty() && !g_chatLoading.load();
    C3 sendCol = canSend ? CGREEN : TEXT2;
    glColor4f(sendCol.r, sendCol.g, sendCol.b, sendHovered && canSend ? 0.95f : 0.7f);
    glBegin(GL_QUADS);
    glVertex2f(ix + iw - P - sendBtnW - 2, inputY);
    glVertex2f(ix + iw - P + 2, inputY);
    glVertex2f(ix + iw - P + 2, inputY + inputH);
    glVertex2f(ix + iw - P - sendBtnW - 2, inputY + inputH);
    glEnd();
    txtBody(ix + iw - P - sendBtnW + 8, inputY + 20, "Send", 13, TEXT0, canSend ? 1.f : 0.5f);

    txtBody(ix + P, inputY + inputH + 14, "Powered by Ollama (" + g_ollamaModel + ")", 13, TEXT2, 0.35f);
}

/* ─────────────────────────────────────────────────────────
   2D UI  (OpenGL ortho overlay)
───────────────────────────────────────────────────────── */
static std::string g_inp;
static bool        g_inpActive=false;
static std::string g_msg;
static bool        g_msgOk=true;
static double      g_msgT=0;
static int         g_camPreset = 0; // 0=Free, 1=Front, 2=Top, 3=Side, 4=Isometric

void drawUI(){
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0,WIN_W,WIN_H,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    double mx, my;
    glfwGetCursorPos(g_win, &mx, &my);

    // 1. COLLAPSIBLE TOGGLE BUTTONS
    // Left Toggle Button
    float L_toggle_x = g_leftCollapsed ? 0.f : (float)SB_W - 30.f;
    bool L_hover = (mx >= L_toggle_x && mx <= L_toggle_x + 30.f && my >= 76.f && my <= 116.f);
    if (L_hover) {
        glColor4f(BG3.r, BG3.g, BG3.b, 0.9f);
    } else {
        glColor4f(BG1.r, BG1.g, BG1.b, 0.8f);
    }
    glBegin(GL_QUADS);
    glVertex2f(L_toggle_x, 76.f); glVertex2f(L_toggle_x + 30.f, 76.f);
    glVertex2f(L_toggle_x + 30.f, 116.f); glVertex2f(L_toggle_x, 116.f);
    glEnd();
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, L_hover ? 0.8f : 0.3f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(L_toggle_x, 76.f); glVertex2f(L_toggle_x + 30.f, 76.f);
    glVertex2f(L_toggle_x + 30.f, 116.f); glVertex2f(L_toggle_x, 116.f);
    glEnd();
    txt(L_toggle_x + 10.f, 102.f, g_leftCollapsed ? "▶" : "◀", 18, ACCENT);

    // Right Toggle Button
    float R_toggle_x = g_rightCollapsed ? (float)WIN_W - 30.f : (float)WIN_W - INFO_W;
    bool R_hover = (mx >= R_toggle_x && mx <= R_toggle_x + 30.f && my >= 76.f && my <= 116.f);
    if (R_hover) {
        glColor4f(BG3.r, BG3.g, BG3.b, 0.9f);
    } else {
        glColor4f(BG1.r, BG1.g, BG1.b, 0.8f);
    }
    glBegin(GL_QUADS);
    glVertex2f(R_toggle_x, 76.f); glVertex2f(R_toggle_x + 30.f, 76.f);
    glVertex2f(R_toggle_x + 30.f, 116.f); glVertex2f(R_toggle_x, 116.f);
    glEnd();
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, R_hover ? 0.8f : 0.3f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(R_toggle_x, 76.f); glVertex2f(R_toggle_x + 30.f, 76.f);
    glVertex2f(R_toggle_x + 30.f, 116.f); glVertex2f(R_toggle_x, 116.f);
    glEnd();
    txt(R_toggle_x + 10.f, 102.f, g_rightCollapsed ? "◀" : "▶", 18, ACCENT);

    // 2. LEFT SIDEBAR CONTENT
    if (!g_leftCollapsed) {
        // Background
        glColor4f(BG1.r, BG1.g, BG1.b, 0.98f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(SB_W, 0); glVertex2f(SB_W, WIN_H); glVertex2f(0, WIN_H);
        glEnd();
        glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.22f);
        glBegin(GL_LINES); glVertex2f(SB_W, 0); glVertex2f(SB_W, WIN_H); glEnd();

        // Branding / Logo (no institution name)
        txt(14, 38, "Pixel", 28, TEXT0);
        txt(96, 38, "Factory", 28, ACCENT);
        txt(14, 58, "Invisible Forces -> Interactive Experience", 14, ACCENT2, 0.60f);
        glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.5f);
        glBegin(GL_LINES); glVertex2f(8, 70); glVertex2f(SB_W - 8, 70); glEnd();

        // Render concept categories
        float iy = 80;   // tighter start — more room for equations
        int ih = 48;     // item height — slightly tighter
        const int PAD = 8; // horizontal padding

        auto item = [&](bool active, int eqIdx, const std::string& name, const std::string& co, const std::string& formula) {
            if (iy > WIN_H - 148) return;
            bool hovered = (mx >= PAD && mx <= SB_W - PAD && my >= iy && my < iy + ih - 2);
            if (active) {
                glColor4f(BG3.r, BG3.g, BG3.b, 0.92f);
                glBegin(GL_QUADS);
                glVertex2f((float)PAD, iy); glVertex2f(SB_W - (float)PAD, iy);
                glVertex2f(SB_W - (float)PAD, iy + ih - 2); glVertex2f((float)PAD, iy + ih - 2);
                glEnd();
                gc(ACCENT);
                glBegin(GL_QUADS);
                glVertex2f((float)PAD, iy); glVertex2f((float)PAD + 3.f, iy);
                glVertex2f((float)PAD + 3.f, iy + ih - 2); glVertex2f((float)PAD, iy + ih - 2);
                glEnd();
            } else if (hovered) {
                glColor4f(BG2.r, BG2.g, BG2.b, 0.45f);
                glBegin(GL_QUADS);
                glVertex2f((float)PAD, iy); glVertex2f(SB_W - (float)PAD, iy);
                glVertex2f(SB_W - (float)PAD, iy + ih - 2); glVertex2f((float)PAD, iy + ih - 2);
                glEnd();
                gc(ACCENT, 0.35f);
                glBegin(GL_LINE_LOOP);
                glVertex2f((float)PAD, iy); glVertex2f(SB_W - (float)PAD, iy);
                glVertex2f(SB_W - (float)PAD, iy + ih - 2); glVertex2f((float)PAD, iy + ih - 2);
                glEnd();
            }
            // Equation name — bold, full width
            txt((float)PAD + 6, iy + 18, clip(name, 21), 16, active ? ACCENT2 : TEXT0);
            // Tag badge right-aligned
            txt(SB_W - 52, iy + 18, co, 16, active ? ACCENT2 : ACCENT, 0.85f);
            // Formula — smaller, muted, with extra left indent
            txt((float)PAD + 6, iy + 34, clip(formula, 27), 16, TEXT2, active ? 0.80f : 0.55f);
            iy += (float)ih + 3.f;
        };

        for (int c = 0; c < (int)g_categories.size(); c++) {
            auto& cat = g_categories[c];
            if (iy > WIN_H - 148) break;

            // Category Header — accent-tinted pill
            bool catHovered = (mx >= 8 && mx <= SB_W - 8 && my >= iy && my < iy + 30);
            if (catHovered) {
                glColor4f(BG3.r, BG3.g, BG3.b, 0.9f);
            } else {
                glColor4f(BG0.r, BG0.g, BG0.b, 0.7f);
            }
            glBegin(GL_QUADS);
            glVertex2f(8, iy); glVertex2f(SB_W - 8, iy); glVertex2f(SB_W - 8, iy + 28); glVertex2f(8, iy + 28);
            glEnd();
            // Left accent line when expanded
            if (cat.expanded) {
                gc(ACCENT, 0.7f);
                glBegin(GL_QUADS);
                glVertex2f(8, iy); glVertex2f(11, iy); glVertex2f(11, iy + 28); glVertex2f(8, iy + 28);
                glEnd();
            } else {
                glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.35f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(8, iy); glVertex2f(SB_W - 8, iy); glVertex2f(SB_W - 8, iy + 28); glVertex2f(8, iy + 28);
                glEnd();
            }

            txt(16, iy + 19, cat.name, 16, cat.expanded ? ACCENT2 : TEXT1);
            txt(SB_W - 26, iy + 19, cat.expanded ? "\xe2\x96\xb2" : "\xe2\x96\xbc", 16, ACCENT, 0.8f);
            iy += 32;

            if (cat.expanded) {
                for (int idx : cat.eqIndices) {
                    item(g_selIdx == idx, idx, g_eqs[idx].name, g_eqs[idx].co, g_eqs[idx].formula);
                }
            }
        }

        // Render Custom Category Header at the end if custom exists
        if (g_hasCustom && iy < WIN_H - 145) {
            bool catHovered = (mx >= 4 && mx <= SB_W - 4 && my >= iy && my < iy + 34);
            if (catHovered) {
                glColor4f(BG2.r, BG2.g, BG2.b, 0.8f);
            } else {
                glColor4f(BG0.r, BG0.g, BG0.b, 0.5f);
            }
            glBegin(GL_QUADS);
            glVertex2f(4, iy); glVertex2f(SB_W - 4, iy); glVertex2f(SB_W - 4, iy + 32); glVertex2f(4, iy + 32);
            glEnd();
            glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(4, iy); glVertex2f(SB_W - 4, iy); glVertex2f(SB_W - 4, iy + 32); glVertex2f(4, iy + 32);
            glEnd();

            txt(12, iy + 22, "Custom Equation", 16, g_selIdx == -1 ? ACCENT2 : TEXT0);
            txt(SB_W - 30, iy + 22, "★", 16, ACCENT);
            iy += 38;

            if (g_selIdx == -1) {
                item(true, -1, g_custom.name, "CUST", g_custom.formula);
            }
        }

        // Bottom Custom Equation Input Field (Sticky at bottom of left sidebar)
        glColor4f(BG0.r, BG0.g, BG0.b, 0.98f);
        glBegin(GL_QUADS);
        glVertex2f(0, WIN_H - 130); glVertex2f(SB_W, WIN_H - 130);
        glVertex2f(SB_W, WIN_H); glVertex2f(0, WIN_H);
        glEnd();
        glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.7f);
        glBegin(GL_LINES); glVertex2f(0, WIN_H - 130); glVertex2f(SB_W, WIN_H - 130); glEnd();

        txt(10, WIN_H - 110, "EQUATION PROMPT", 16, ACCENT3, 0.8f);

        // Input Box
        C3 bc = g_inpActive ? ACCENT : BORDER;
        glColor4f(BG2.r, BG2.g, BG2.b, 1);
        glBegin(GL_QUADS);
        glVertex2f(8, WIN_H - 95); glVertex2f(SB_W - 72, WIN_H - 95);
        glVertex2f(SB_W - 72, WIN_H - 63); glVertex2f(8, WIN_H - 63);
        glEnd();
        glColor4f(bc.r, bc.g, bc.b, 1);
        glBegin(GL_LINE_LOOP);
        glVertex2f(8, WIN_H - 95); glVertex2f(SB_W - 72, WIN_H - 95);
        glVertex2f(SB_W - 72, WIN_H - 63); glVertex2f(8, WIN_H - 63);
        glEnd();

        std::string disp = g_inp.size() > 22 ? g_inp.substr(g_inp.size() - 22) : g_inp;
        if (g_inpActive && fmod(g_time, 1.0) < 0.5) disp += '|';
        txt(14, WIN_H - 74, disp, 16, g_inpActive ? ACCENT2 : TEXT2);

        // RUN Button
        bool runHovered = (mx >= SB_W - 66 && mx <= SB_W - 6 && my >= WIN_H - 95 && my <= WIN_H - 63);
        glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, runHovered ? 0.9f : 1.f);
        glBegin(GL_QUADS);
        glVertex2f(SB_W - 66, WIN_H - 95); glVertex2f(SB_W - 6, WIN_H - 95);
        glVertex2f(SB_W - 6, WIN_H - 63); glVertex2f(SB_W - 66, WIN_H - 63);
        glEnd();
        txt(SB_W - 52, WIN_H - 74, "RUN", 16, TEXT0);

        // Message overlay
        if (!g_msg.empty() && g_time - g_msgT < 5.0) {
            float al = (float)std::min(1.0, 5.0 - (g_time - g_msgT));
            C3 mc = g_msgOk ? ACCENT3 : HOT;
            txt(10, WIN_H - 40, clip(g_msg, 28), 16, mc, al);
        } else {
            txt(10, WIN_H - 40, "e.g. F = m * a  or  E = m * c^2", 16, TEXT2, 0.5f);
        }
    }

    // 3. TOP BAR
    float top_x = g_leftCollapsed ? 0.f : (float)SB_W;
    float top_w = (float)WIN_W - top_x;
    glColor4f(BG1.r, BG1.g, BG1.b, 1);
    glBegin(GL_QUADS);
    glVertex2f(top_x, 0); glVertex2f(WIN_W, 0); glVertex2f(WIN_W, TB_H); glVertex2f(top_x, TB_H);
    glEnd();
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.22f);
    glBegin(GL_LINES); glVertex2f(top_x, TB_H); glVertex2f(WIN_W, TB_H); glEnd();

    auto& ceq = curEq();
    glColor4f(BG2.r, BG2.g, BG2.b, 1);
    glBegin(GL_QUADS);
    glVertex2f(top_x + 8, 6); glVertex2f(top_x + 52, 6); glVertex2f(top_x + 52, 30); glVertex2f(top_x + 8, 30);
    glEnd();
    txt(top_x + 13, 22, ceq.co, 16, ACCENT2);
    txt(top_x + 60, 23, clip(ceq.name, 32), 18, TEXT0);

    float canvasRight = (float)(WIN_W - (g_rightCollapsed ? 0 : INFO_W));
    txt(canvasRight - 295, 23, clip(ceq.formula, 44), 16, ACCENT2, 0.85f);

    // 4. CAMERA PRESETS BUTTONS (horizontal row in canvas space)
    std::string presets[] = {"Free", "Front", "Top", "Side", "Isometric"};
    for (int i = 0; i < 5; i++) {
        float bx = (float)CANVAS_X + 15.f + i * 95.f;
        float by = (float)TB_H + 8.f;
        float bw = 90.f;
        float bh = 28.f;

        bool btnHovered = (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh);
        bool active = (g_camPreset == i);

        if (active) {
            gc(ACCENT, 1.f);
        } else if (btnHovered) {
            glColor4f(BG3.r, BG3.g, BG3.b, 0.9f);
        } else {
            glColor4f(BG2.r, BG2.g, BG2.b, 0.6f);
        }
        glBegin(GL_QUADS);
        glVertex2f(bx, by); glVertex2f(bx + bw, by); glVertex2f(bx + bw, by + bh); glVertex2f(bx, by + bh);
        glEnd();
        glColor4f(BORDER.r, BORDER.g, BORDER.b, 0.4f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(bx, by); glVertex2f(bx + bw, by); glVertex2f(bx + bw, by + bh); glVertex2f(bx, by + bh);
        glEnd();

        txt(bx + 12.f, by + 19.f, presets[i], 16, active ? TEXT0 : TEXT1);
    }

    // 5. RESULT HUD (glassmorphism panel)
    float hx = canvasRight - 180.f - 12.f;
    float hy = (float)TB_H + 12.f;
    glColor4f(BG0.r, BG0.g, BG0.b, 0.93f);
    glBegin(GL_QUADS);
    glVertex2f(hx, hy); glVertex2f(hx + 180.f, hy); glVertex2f(hx + 180.f, hy + 105.f); glVertex2f(hx, hy + 105.f);
    glEnd();
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.35f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(hx, hy); glVertex2f(hx + 180.f, hy); glVertex2f(hx + 180.f, hy + 105.f); glVertex2f(hx, hy + 105.f);
    glEnd();
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.45f);
    glBegin(GL_LINES); glVertex2f(hx, hy); glVertex2f(hx + 180.f, hy); glEnd();

    txt(hx + 8.f, hy + 20.f, ceq.result, 16, TEXT2);
    double res = std::numeric_limits<double>::quiet_NaN();
    try { res = ceq.calc(g_vals); } catch (...) {}
    txt(hx + 8.f, hy + 58.f, fmt(res), 24, (!std::isnan(res) && !std::isinf(res)) ? CYELLOW : HOT);
    txt(hx + 8.f, hy + 76.f, ceq.unit, 16, TEXT1);
    txt(hx + 8.f, hy + 94.f, clip(ceq.name, 18), 16, TEXT2, 0.65f);

    // 6. CONTROLS PANEL
    float ctrl_x = g_leftCollapsed ? 0.f : (float)SB_W;
    float ctrl_w = (float)WIN_W - ctrl_x - (g_rightCollapsed ? 0.f : (float)INFO_W);
    glColor4f(BG1.r, BG1.g, BG1.b, 1);
    glBegin(GL_QUADS);
    glVertex2f(ctrl_x, WIN_H - CT_H); glVertex2f(ctrl_x + ctrl_w, WIN_H - CT_H);
    glVertex2f(ctrl_x + ctrl_w, WIN_H); glVertex2f(ctrl_x, WIN_H);
    glEnd();
    glColor4f(ACCENT.r, ACCENT.g, ACCENT.b, 0.22f);
    glBegin(GL_LINES); glVertex2f(ctrl_x, WIN_H - CT_H); glVertex2f(ctrl_x + ctrl_w, WIN_H - CT_H); glEnd();
    txt(ctrl_x + 8.f, WIN_H - CT_H + 18, "VARIABLES -- drag sliders (animation never stops)", 16, TEXT2, 0.6f);

    for (auto& sl : g_sliders) {
        sl.draw2D();
        Var& v = ceq.vars[sl.vi];
        txt(sl.x, sl.y + 17, v.sym, 16, ACCENT2);
        std::string vs = fmt(v.val) + (v.unit.empty() ? "" : " " + v.unit);
        txt(sl.x + sl.w - (float)vs.size() * 9.f, sl.y + 17, vs, 16, ACCENT);
        txt(sl.x, sl.y + 29, clip(v.name, 18), 16, TEXT2, 0.85f);
    }

    txt(ctrl_x + 8.f, WIN_H - 5, "PixelFactory  |  C++ OpenGL  |  drag to orbit  |  scroll to zoom  |  IEEE Future Tech Challenge", 16, TEXT2, 0.35f);
    
    drawInfoPanel(); // Render docked educational panel

    glEnable(GL_LIGHTING); glEnable(GL_DEPTH_TEST);
}

/* ─────────────────────────────────────────────────────────
   GLFW CALLBACKS
───────────────────────────────────────────────────────── */

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
    if(g_chatInputActive){
        if(key==GLFW_KEY_ENTER||key==GLFW_KEY_KP_ENTER) { sendChatMessage(); return; }
        else if(key==GLFW_KEY_BACKSPACE&&!g_chatInput.empty()) g_chatInput.pop_back();
        else if(key==GLFW_KEY_ESCAPE) g_chatInputActive=false;
        return;
    }
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
    if(g_chatInputActive&&cp!='\r'&&cp!='\n'&&g_chatInput.size()<200){
        g_chatInput+=(char)cp;
        return;
    }
    if(g_inpActive&&cp!='\r'&&cp!='\n'&&g_inp.size()<80)
        g_inp+=(char)cp;
}

void cbMBtn(GLFWwindow*,int btn,int action,int){
    if(btn!=GLFW_MOUSE_BUTTON_LEFT) return;
    double mx,my; glfwGetCursorPos(g_win,&mx,&my);
    if(action==GLFW_PRESS){
        // Left Toggle Button
        float L_toggle_x = g_leftCollapsed ? 0.f : (float)SB_W - 30.f;
        if (mx >= L_toggle_x && mx <= L_toggle_x + 30.f && my >= 76.f && my <= 116.f) {
            g_leftCollapsed = !g_leftCollapsed;
            rebuildSliders();
            return;
        }

        // Right Toggle Button
        float R_toggle_x = g_rightCollapsed ? (float)WIN_W - 30.f : (float)WIN_W - INFO_W;
        if (mx >= R_toggle_x && mx <= R_toggle_x + 30.f && my >= 76.f && my <= 116.f) {
            g_rightCollapsed = !g_rightCollapsed;
            rebuildSliders();
            return;
        }

        if (!g_rightCollapsed && mx >= WIN_W - INFO_W) {
            float ix = (float)(WIN_W - INFO_W);
            float iw_click = (float)INFO_W;
            float iy_click = (float)TB_H;
            float ih_click = (float)(WIN_H - TB_H);
            float P_click = 16.f;
            float sendBtnW_click = 52.f;

            // Chat input area click detection (at bottom of info panel)
            float chatInputY = iy_click + ih_click - 85.f + 6.f;
            float chatInputH = 30.f;
            // Chat input box
            if (mx >= ix + P_click - 2 && mx <= ix + iw_click - P_click - sendBtnW_click - 6
                && my >= chatInputY && my <= chatInputY + chatInputH) {
                g_chatInputActive = true;
                g_inpActive = false;
                return;
            }
            // Send button
            if (mx >= ix + iw_click - P_click - sendBtnW_click - 2 && mx <= ix + iw_click - P_click + 2
                && my >= chatInputY && my <= chatInputY + chatInputH) {
                sendChatMessage();
                return;
            }
            g_chatInputActive = false;

            float pX0 = ix + 18.f;
            float pX1 = ix + INFO_W - 18.f;
            float pY = (float)TB_H + 64.f;
            for (int i=0; i<3; i++) {
                float by = pY + 58.f + i * 27.f;
                if (mx >= pX0 && mx <= pX1 && my >= by && my <= by + 22.f) {
                    PredictionCard pred = makePredictionCard();
                    bool firstTry = !g_predictAnswered;
                    g_predictChoice = i;
                    g_predictAnswered = true;
                    if (firstTry && i == pred.correct) g_learningXP += 10;
                    return;
                }
            }
        }

        // Camera Presets
        for (int i = 0; i < 5; i++) {
            float bx = (float)CANVAS_X + 15.f + i * 95.f;
            float by = (float)TB_H + 8.f;
            float bw = 90.f;
            float bh = 28.f;
            if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                g_camPreset = i;
                if (i == 1) { g_camThetaT = 0.0; g_camPhiT = 0.0; g_camDistT = 11.0; }
                else if (i == 2) { g_camThetaT = 0.0; g_camPhiT = 1.55; g_camDistT = 11.0; }
                else if (i == 3) { g_camThetaT = 1.57; g_camPhiT = 0.0; g_camDistT = 11.0; }
                else if (i == 4) { g_camThetaT = 0.785; g_camPhiT = 0.615; g_camDistT = 11.0; }
                g_orbitX = g_camThetaT; g_orbitY = g_camPhiT;
                return;
            }
        }

        // Left Sidebar Clicking
        if (!g_leftCollapsed && mx < SB_W) {
            // Input Box
            if (mx >= 8 && mx <= SB_W - 72 && my >= WIN_H - 95 && my <= WIN_H - 63) {
                g_inpActive = true;
                g_chatInputActive = false;
                return;
            }
            // RUN Button
            if (mx >= SB_W - 66 && mx <= SB_W - 6 && my >= WIN_H - 95 && my <= WIN_H - 63) {
                tryParse();
                g_inpActive = false;
                return;
            }
            
            g_inpActive = false;

            // Categories and equations — must match draw geometry exactly
            float iy = 80.f;   // same start as draw
            int ih = 48;       // same item height as draw
            const int PAD = 8;
            for (int c = 0; c < (int)g_categories.size(); c++) {
                auto& cat = g_categories[c];
                if (iy > WIN_H - 148) break;

                // Category Header Click (height = 28)
                if (mx >= 8 && mx <= SB_W - 8 && my >= iy && my < iy + 28) {
                    cat.expanded = !cat.expanded;
                    return;
                }
                iy += 32;  // header height 28 + 4 gap

                if (cat.expanded) {
                    for (int idx : cat.eqIndices) {
                        if (iy > WIN_H - 148) break;
                        if (mx >= PAD && mx <= SB_W - PAD && my >= iy && my < iy + ih - 2) {
                            loadEq(idx);
                            rebuildSliders();
                            return;
                        }
                        iy += ih + 3;
                    }
                }
            }

            // Custom Equation header click
            if (g_hasCustom && iy < WIN_H - 148) {
                if (mx >= 8 && mx <= SB_W - 8 && my >= iy && my < iy + 28) {
                    g_selIdx = -1;
                    g_vals.clear();
                    for (auto& v : g_custom.vars) g_vals[v.sym] = v.val;
                    rebuildSliders();
                    return;
                }
                iy += 32;
                if (g_selIdx == -1) {
                    if (mx >= PAD && mx <= SB_W - PAD && my >= iy && my < iy + ih - 2) {
                        g_selIdx = -1;
                        g_vals.clear();
                        for (auto& v : g_custom.vars) g_vals[v.sym] = v.val;
                        rebuildSliders();
                        return;
                    }
                    iy += ih + 3;
                }
            }
            return;
        }

        g_inpActive = false;
        
        // Sliders clicking
        for(auto& sl : g_sliders) {
            if(sl.hit((float)mx, (float)my)) {
                sl.drag = true;
                return;
            }
        }

        // Viewport drag (only in canvas area)
        float canvasRight = (float)(WIN_W - (g_rightCollapsed ? 0 : INFO_W));
        if(mx > CANVAS_X && mx < canvasRight && my > TB_H && my < WIN_H - CT_H) {
            g_drag = true;
            g_lmx = mx;
            g_lmy = my;
        }
    }
    if(action==GLFW_RELEASE){
        g_drag = false;
        for(auto& sl : g_sliders) sl.drag = false;
    }
}

void cbMMove(GLFWwindow*,double mx,double my){
    if(g_drag){
        double dx = mx - g_lmx;
        double dy = my - g_lmy;
        g_orbitX -= dx * 0.007;
        g_orbitY  = std::max(-1.45, std::min(1.45, g_orbitY - dy * 0.007));
        g_camTheta  = g_orbitX;
        g_camPhi    = g_orbitY;
        g_camThetaT = g_orbitX;
        g_camPhiT   = g_orbitY;
        g_camPreset = 0; // Set camera mode to Free
        g_lmx = mx; g_lmy = my;
    }
    for(auto& sl : g_sliders) if(sl.drag) sl.onDrag((float)mx);
}

void cbScroll(GLFWwindow*,double,double dy){
    double mx,my; glfwGetCursorPos(g_win,&mx,&my);
    // Right panel scroll for chat
    if (!g_rightCollapsed && mx >= WIN_W - INFO_W) {
        g_infoPanelScroll = std::max(0.f, g_infoPanelScroll - (float)dy * 25.f);
        return;
    }
    float canvasRight = (float)(WIN_W - (g_rightCollapsed ? 0 : INFO_W));
    if(mx > CANVAS_X && mx < canvasRight) {
        g_camDist = std::max(2.0, std::min(28.0, g_camDist - dy * 0.5));
        g_camDistT = g_camDist;
        g_camPreset = 0; // Set camera mode to Free
    }
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

    g_win=glfwCreateWindow(WIN_W,WIN_H,"PixelFactory  --  Turning Invisible Forces Into Interactive Experience",NULL,NULL);
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
    initEqDetails();
    addProfessionalLearningDetails();
    loadEq(0);
    rebuildSliders();

    printf("PixelFactory started  --  Turning Invisible Forces Into Interactive Experience\n");
    printf("  Sri Sairam Institute of Technology  |  IEEE Future Tech Explorers Challenge\n");
    printf("  Mouse drag (3D)  : orbit in any direction (full sphere)\n");
    printf("  Scroll           : zoom in / out\n");
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

        // Camera target interpolation for smooth animated gliding
        if (!g_drag) {
            g_camTheta += (g_camThetaT - g_camTheta) * 0.15;
            g_camPhi   += (g_camPhiT - g_camPhi) * 0.15;
            g_camDist  += (g_camDistT - g_camDist) * 0.15;
        }

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

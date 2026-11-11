// SynergyBridge - OMSI 2 plugin: posila stav Synergy OCC pres UDP a prijima stisky triggeru.
// Stav  -> UDP 127.0.0.1:38600 (JSON, ~10x za sekundu)
// Prikaz <- UDP 127.0.0.1:38601 (text: nazev triggeru)
// Build (32-bit!): i686-w64-mingw32-g++ -O2 -shared -static -o SynergyBridge.dll SynergyBridge.cpp -lws2_32 -Wl,--kill-at

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstring>

#include "names.h"   // VARS[], STRVARS[], TRIGGERS[] - stejne poradi jako v SynergyBridge.opl

static const int NV = sizeof(VARS) / sizeof(VARS[0]);
static const int NS = sizeof(STRVARS) / sizeof(STRVARS[0]);
static const int NT = sizeof(TRIGGERS) / sizeof(TRIGGERS[0]);

static CRITICAL_SECTION g_cs;
static float g_vals[NV];
static std::string g_strs[NS];
static int g_press[NT];            // pocet snimku, po ktere drzet trigger aktivni
static int g_release[NT];          // 1 = v dalsim snimku poslat false
static std::vector<std::pair<int, int>> g_queue;   // (trigger, zpozdeni ve snimcich) - pro *_off
static volatile bool g_run = false;
static HANDLE g_thread = NULL;

// OMSI cte texty ze skriptu jako Latin-1, ale jsou v CP1250 ("Kè" misto "Kč") -> prevest zpet
static wchar_t g_cp1250[128];
static void initCp1250() {
    for (int i = 0; i < 128; i++) {
        char c = (char)(0x80 + i); wchar_t w = 0;
        if (MultiByteToWideChar(1250, MB_ERR_INVALID_CHARS, &c, 1, &w, 1) != 1) w = (wchar_t)(0x80 + i);
        g_cp1250[i] = w;
    }
}

static std::string utf8(const wchar_t* src) {
    if (!src) return "";
    std::wstring tmp(src);
    for (auto& ch : tmp) if (ch >= 0x80 && ch <= 0xFF) ch = g_cp1250[ch - 0x80];
    const wchar_t* w = tmp.c_str();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return "";
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
}

static void jsonEsc(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char b[8]; wsprintfA(b, "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    out += '"';
}

static int findTrigger(const std::string& name) {
    for (int i = 0; i < NT; i++) if (name == TRIGGERS[i]) return i;
    return -1;
}

static DWORD WINAPI netThread(LPVOID) {
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET tx = socket(AF_INET, SOCK_DGRAM, 0);
    SOCKET rx = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst = {}; dst.sin_family = AF_INET; dst.sin_port = htons(38600); dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    sockaddr_in me = {};  me.sin_family = AF_INET;  me.sin_port = htons(38601);  me.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(rx, (sockaddr*)&me, sizeof(me));
    DWORD to = 100; setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));

    char buf[512];
    DWORD lastSend = 0;
    while (g_run) {
        int n = recv(rx, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string name(buf);
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
            int i = findTrigger(name);
            if (i >= 0) {
                EnterCriticalSection(&g_cs);
                g_press[i] = 3;   // 3 snimky "stisknuto", pak uvolnit
                int off = findTrigger(name + "_off");
                if (off >= 0) g_queue.push_back({off, 6});
                LeaveCriticalSection(&g_cs);
            }
        }
        DWORD now = GetTickCount();
        if (now - lastSend >= 100) {
            lastSend = now;
            std::string j = "{\"v\":{";
            EnterCriticalSection(&g_cs);
            for (int i = 0; i < NV; i++) {
                char b[64]; sprintf(b, "%s\"%s\":%g", i ? "," : "", VARS[i], (double)g_vals[i]);
                j += b;
            }
            j += "},\"s\":{";
            for (int i = 0; i < NS; i++) {
                if (i) j += ',';
                j += '"'; j += STRVARS[i]; j += "\":";
                jsonEsc(j, g_strs[i]);
            }
            LeaveCriticalSection(&g_cs);
            j += "}}";
            sendto(tx, j.data(), (int)j.size(), 0, (sockaddr*)&dst, sizeof(dst));
        }
    }
    closesocket(tx); closesocket(rx); WSACleanup();
    return 0;
}

extern "C" {

__declspec(dllexport) void __stdcall PluginStart(void* /*owner*/) {
    InitializeCriticalSection(&g_cs);
    initCp1250();
    for (int i = 0; i < NV; i++) g_vals[i] = -1;
    g_run = true;
    g_thread = CreateThread(NULL, 0, netThread, NULL, 0, NULL);
}

__declspec(dllexport) void __stdcall PluginFinalize() {
    g_run = false;
    if (g_thread) { WaitForSingleObject(g_thread, 1000); CloseHandle(g_thread); g_thread = NULL; }
    DeleteCriticalSection(&g_cs);
}

__declspec(dllexport) void __stdcall AccessVariable(unsigned short idx, float* value, bool* write) {
    if (write) *write = false;
    if (idx < NV && value) { EnterCriticalSection(&g_cs); g_vals[idx] = *value; LeaveCriticalSection(&g_cs); }
}

__declspec(dllexport) void __stdcall AccessStringVariable(unsigned short idx, wchar_t* value, bool* write) {
    if (write) *write = false;
    if (idx < NS && value) {
        std::string s = utf8(value);
        if (s.size() > 2000) s.resize(2000);
        EnterCriticalSection(&g_cs); g_strs[idx] = s; LeaveCriticalSection(&g_cs);
    }
}

__declspec(dllexport) void __stdcall AccessSystemVariable(unsigned short, float*, bool* write) {
    if (write) *write = false;
}

__declspec(dllexport) void __stdcall AccessTrigger(unsigned short idx, bool* active) {
    if (idx >= NT || !active) return;
    EnterCriticalSection(&g_cs);
    // zpozdene *_off triggery (uvolneni tlacitka)
    if (idx == 0) {
        for (size_t k = 0; k < g_queue.size();) {
            if (--g_queue[k].second <= 0) { g_press[g_queue[k].first] = 3; g_queue.erase(g_queue.begin() + k); }
            else k++;
        }
    }
    if (g_press[idx] > 0) { *active = true; g_press[idx]--; if (g_press[idx] == 0) g_release[idx] = 1; }
    else if (g_release[idx]) { *active = false; g_release[idx] = 0; }
    LeaveCriticalSection(&g_cs);
}

}

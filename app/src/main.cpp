// Synergy OCC Tablet - jedna aplikace: instalace pluginu do OMSI 2 + server pro tablet + jednoduche okno.
// Build: viz build.sh

#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../third/qrcodegen.h"
}
#include "resource.h"

#define APP_TITLE L"Synergy OCC Tablet"
#define APP_VERSION L"1.3"

static const int STATE_PORT = 38600;   // plugin -> aplikace (mimo dynamicky rozsah Windows 49152+)
static const int CMD_PORT = 38601;     // aplikace -> plugin
static const int DEFAULT_HTTP_PORT = 8765;

// ---------------------------------------------------------------------------
// sdileny stav
// ---------------------------------------------------------------------------
static CRITICAL_SECTION g_cs;
static std::string g_stateJson;          // posledni JSON z pluginu
static DWORD g_lastRx = 0;               // GetTickCount posledniho UDP paketu
static DWORD g_lastTablet = 0;           // posledni dotaz tabletu
static std::wstring g_omsiDir;
static int g_httpPort = 0;
static std::string g_ip = "127.0.0.1";
static std::string g_url;
static SOCKET g_cmdSock = INVALID_SOCKET;
static std::wstring g_contentDir;        // content folder openOMSI (z game.log), muze byt prazdny
static std::wstring g_texRoot;           // nalezena slozka SynergyOCC (cache)
static std::wstring g_missingTex;        // posledni textura, ktera nesla najit
static bool g_udpError = false;          // nepovedlo se otevrit port pro data z pluginu
static DWORD g_startTick = 0;
struct NetAddr { std::string ip; std::wstring name; bool gateway; };
static std::vector<NetAddr> g_addrs;
static HWND g_wnd = NULL;

// ---------------------------------------------------------------------------
// pomocne funkce
// ---------------------------------------------------------------------------
static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool dirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool readFile(const std::wstring& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], (DWORD)out.size(), &rd, NULL);
    CloseHandle(h);
    return ok && rd == out.size();
}
static DWORD writeFile(const std::wstring& p, const void* data, DWORD len) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data, len, &wr, NULL);
    DWORD err = ok && wr == len ? 0 : GetLastError();
    CloseHandle(h);
    return err;
}
static bool resData(int id, const void** data, DWORD* len) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(NULL, r);
    if (!g) return false;
    *data = LockResource(g);
    *len = SizeofResource(NULL, r);
    return *data != NULL;
}

// ---------------------------------------------------------------------------
// nastaveni (%APPDATA%\SynergyTablet\config.ini)
// ---------------------------------------------------------------------------
static std::wstring configPath() {
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, buf) != S_OK) return L"";
    std::wstring dir = std::wstring(buf) + L"\\SynergyTablet";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\config.ini";
}
static std::wstring cfgGet(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"app", key, def, buf, 1024, configPath().c_str());
    return buf;
}
static void cfgSet(const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(L"app", key, val.c_str(), configPath().c_str());
}

// ---------------------------------------------------------------------------
// hledani OMSI 2
// ---------------------------------------------------------------------------
static bool isOmsiDir(const std::wstring& d) {
    return !d.empty() && fileExists(d + L"\\Omsi.exe");
}
static std::wstring regString(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[1024]; DWORD sz = sizeof(buf);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, NULL, buf, &sz) == ERROR_SUCCESS) return buf;
    return L"";
}
static std::wstring findOmsi() {
    std::vector<std::wstring> steamDirs;
    std::wstring s = regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (!s.empty()) steamDirs.push_back(s);
    s = regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (!s.empty()) steamDirs.push_back(s);
    steamDirs.push_back(L"C:\\Program Files (x86)\\Steam");

    std::vector<std::wstring> libs;
    for (auto d : steamDirs) {
        for (auto& c : d) if (c == L'/') c = L'\\';
        libs.push_back(d);
        // dalsi knihovny Steamu
        std::string vdf;
        if (readFile(d + L"\\steamapps\\libraryfolders.vdf", vdf)) {
            size_t pos = 0;
            while ((pos = vdf.find("\"path\"", pos)) != std::string::npos) {
                size_t a = vdf.find('"', pos + 6); if (a == std::string::npos) break;
                size_t b = vdf.find('"', a + 1); if (b == std::string::npos) break;
                std::string p = vdf.substr(a + 1, b - a - 1);
                std::string clean;
                for (size_t i = 0; i < p.size(); i++) { if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') i++; clean += p[i]; }
                libs.push_back(widen(clean));
                pos = b + 1;
            }
        }
    }
    for (auto& l : libs) {
        std::wstring c = l + L"\\steamapps\\common\\OMSI 2";
        if (isOmsiDir(c)) return c;
    }
    return L"";
}

// ---------------------------------------------------------------------------
// plugin
// ---------------------------------------------------------------------------
enum PluginState { PS_NO_OMSI, PS_MISSING, PS_OLD, PS_OK };
static PluginState pluginState() {
    if (!isOmsiDir(g_omsiDir)) return PS_NO_OMSI;
    std::wstring dir = g_omsiDir + L"\\plugins\\";
    std::string dll, opl;
    if (!readFile(dir + L"SynergyBridge.dll", dll) || !readFile(dir + L"SynergyBridge.opl", opl)) return PS_MISSING;
    const void* d; DWORD dl; const void* o; DWORD ol;
    if (!resData(IDR_PLUGIN_DLL, &d, &dl) || !resData(IDR_PLUGIN_OPL, &o, &ol)) return PS_MISSING;
    if (dll.size() != dl || memcmp(dll.data(), d, dl) || opl.size() != ol || memcmp(opl.data(), o, ol)) return PS_OLD;
    return PS_OK;
}
// vraci 0 = OK, jinak kod chyby Windows
static DWORD installPluginFiles(const std::wstring& omsi) {
    std::wstring dir = omsi + L"\\plugins";
    CreateDirectoryW(dir.c_str(), NULL);
    const void* d; DWORD dl; const void* o; DWORD ol;
    if (!resData(IDR_PLUGIN_DLL, &d, &dl) || !resData(IDR_PLUGIN_OPL, &o, &ol)) return ERROR_RESOURCE_DATA_NOT_FOUND;
    DWORD e = writeFile(dir + L"\\SynergyBridge.dll", d, dl);
    if (e) return e;
    return writeFile(dir + L"\\SynergyBridge.opl", o, ol);
}
static bool omsiRunning() {
    HWND w = FindWindowW(NULL, L"OMSI 2");
    if (w) return true;
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"OMSI2");
    if (m) { CloseHandle(m); return true; }
    return false;
}
static void installPluginInteractive(HWND owner) {
    if (!isOmsiDir(g_omsiDir)) {
        MessageBoxW(owner, L"Nejdřív vyber složku OMSI 2.", APP_TITLE, MB_ICONWARNING);
        return;
    }
    DWORD e = installPluginFiles(g_omsiDir);
    if (e == ERROR_ACCESS_DENIED) {
        // Program Files -> potrebujeme prava spravce
        wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
        std::wstring args = L"--install \"" + g_omsiDir + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = owner; sei.lpVerb = L"runas"; sei.lpFile = self; sei.lpParameters = args.c_str(); sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei)) return;   // uzivatel odmitl UAC
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD code = 1; GetExitCodeProcess(sei.hProcess, &code); CloseHandle(sei.hProcess);
        e = code;
    }
    if (e == 0) {
        MessageBoxW(owner, L"Plugin je nainstalovaný.\n\nPokud OMSI běží, restartuj ho - plugin se načítá jen při startu hry.", APP_TITLE, MB_ICONINFORMATION);
    } else if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_USER_MAPPED_FILE) {
        MessageBoxW(owner, L"Plugin teď používá běžící OMSI.\nZavři OMSI a zkus to znovu.", APP_TITLE, MB_ICONWARNING);
    } else {
        wchar_t msg[256]; swprintf(msg, 256, L"Instalace pluginu se nepovedla (chyba %lu).", e);
        MessageBoxW(owner, msg, APP_TITLE, MB_ICONERROR);
    }
}

// ---------------------------------------------------------------------------
// klavesy (Shift+T = drobne) - jde do aktivniho okna, tedy do OMSI
// ---------------------------------------------------------------------------
static DWORD WINAPI sendShiftT(LPVOID) {
    WORD codes[2] = { 0x2A, 0x14 };
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    for (int i = 0; i < 2; i++) { in.ki.wScan = codes[i]; in.ki.dwFlags = KEYEVENTF_SCANCODE; SendInput(1, &in, sizeof(in)); Sleep(30); }
    Sleep(80);
    for (int i = 1; i >= 0; i--) { in.ki.wScan = codes[i]; in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP; SendInput(1, &in, sizeof(in)); Sleep(30); }
    return 0;
}

// ---------------------------------------------------------------------------
// UDP od pluginu
// ---------------------------------------------------------------------------
static DWORD WINAPI udpThread(LPVOID) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(STATE_PORT); a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) { g_udpError = true; return 1; }
    std::vector<char> buf(65536);
    for (;;) {
        int n = recv(s, buf.data(), (int)buf.size(), 0);
        if (n <= 1 || buf[0] != '{') continue;
        EnterCriticalSection(&g_cs);
        g_stateJson.assign(buf.data(), n);
        g_lastRx = GetTickCount();
        LeaveCriticalSection(&g_cs);
    }
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
static void sendAll(SOCKET c, const char* d, size_t n) {
    while (n > 0) { int w = send(c, d, (int)(n > 65536 ? 65536 : n), 0); if (w <= 0) return; d += w; n -= w; }
}
static void respond(SOCKET c, const char* status, const char* type, const std::string& body, const char* extra = "") {
    char h[512];
    int n = snprintf(h, sizeof(h), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sConnection: close\r\n\r\n",
                     status, type, (unsigned)body.size(), extra);
    sendAll(c, h, n);
    sendAll(c, body.data(), body.size());
}
static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), NULL, 16); i += 2; }
        else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}
static std::string queryParam(const std::string& q, const std::string& key) {
    size_t p = 0;
    while (p < q.size()) {
        size_t e = q.find('&', p); if (e == std::string::npos) e = q.size();
        std::string kv = q.substr(p, e - p);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return urlDecode(kv.substr(eq + 1));
        p = e + 1;
    }
    return "";
}
static bool validName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n) if (!(isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}

// slozky, kde muze byt SynergyOCC: OMSI 2\Vehicles, content folder openOMSI a jedna uroven podslozek
static std::vector<std::wstring> texCandidates() {
    std::vector<std::wstring> roots, out;
    EnterCriticalSection(&g_cs);
    if (!g_omsiDir.empty()) roots.push_back(g_omsiDir + L"\\Vehicles");
    if (!g_contentDir.empty()) roots.push_back(g_contentDir + L"\\Vehicles");
    LeaveCriticalSection(&g_cs);
    for (auto& r : roots) {
        out.push_back(r + L"\\SynergyOCC");
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((r + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
                out.push_back(r + L"\\" + fd.cFileName + L"\\SynergyOCC");
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}

static void handleTex(SOCKET c, const std::string& query) {
    std::wstring p = widen(queryParam(query, "p"));
    for (auto& ch : p) if (ch == L'/') ch = L'\\';
    std::wstring low = p; CharLowerW(&low[0]);
    size_t i = low.find(L"synergyocc\\");
    if (i == std::wstring::npos || p.find(L"..", i) != std::wstring::npos) { respond(c, "404 Not Found", "text/plain", "not found"); return; }
    std::wstring rel = p.substr(i + 11);
    std::string data;
    EnterCriticalSection(&g_cs); std::wstring root = g_texRoot; LeaveCriticalSection(&g_cs);
    bool ok = !root.empty() && readFile(root + L"\\" + rel, data);
    if (!ok) {
        for (auto& cand : texCandidates()) {
            if (readFile(cand + L"\\" + rel, data)) { ok = true; EnterCriticalSection(&g_cs); g_texRoot = cand; LeaveCriticalSection(&g_cs); break; }
        }
    }
    EnterCriticalSection(&g_cs); g_missingTex = ok ? L"" : rel; LeaveCriticalSection(&g_cs);
    if (!ok) { respond(c, "404 Not Found", "text/plain", "not found"); return; }
    const char* type = "application/octet-stream";
    std::wstring ext = low.substr(low.find_last_of(L'.') + 1);
    if (ext == L"png") type = "image/png"; else if (ext == L"jpg" || ext == L"jpeg") type = "image/jpeg"; else if (ext == L"bmp") type = "image/bmp";
    respond(c, "200 OK", type, data, "Cache-Control: max-age=3600\r\n");
}

static DWORD WINAPI clientThread(LPVOID arg) {
    SOCKET c = (SOCKET)(UINT_PTR)arg;
    DWORD to = 5000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
    std::string req; char buf[4096];
    size_t hdrEnd = std::string::npos;
    while (hdrEnd == std::string::npos && req.size() < 16384) {
        int n = recv(c, buf, sizeof(buf), 0); if (n <= 0) break;
        req.append(buf, n); hdrEnd = req.find("\r\n\r\n");
    }
    if (hdrEnd == std::string::npos) { closesocket(c); return 0; }
    // telo (POST /press)
    size_t cl = 0;
    { std::string low = req.substr(0, hdrEnd); for (auto& ch : low) ch = (char)tolower((unsigned char)ch);
      size_t p = low.find("content-length:"); if (p != std::string::npos) cl = strtoul(low.c_str() + p + 15, NULL, 10); }
    if (cl > 4096) cl = 4096;
    while (req.size() < hdrEnd + 4 + cl) { int n = recv(c, buf, sizeof(buf), 0); if (n <= 0) break; req.append(buf, n); }
    std::string body = req.size() > hdrEnd + 4 ? req.substr(hdrEnd + 4) : "";

    size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    std::string method = req.substr(0, sp1), target = req.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = target, query;
    size_t qm = target.find('?'); if (qm != std::string::npos) { path = target.substr(0, qm); query = target.substr(qm + 1); }

    if (path == "/" || path == "/index.html") {
        const void* d; DWORD l;
        if (resData(IDR_INDEX_HTML, &d, &l)) respond(c, "200 OK", "text/html; charset=utf-8", std::string((const char*)d, l), "Cache-Control: no-store\r\n");
        else respond(c, "500 Error", "text/plain", "missing");
    } else if (path == "/state") {
        std::string j;
        EnterCriticalSection(&g_cs);
        g_lastTablet = GetTickCount();
        if (g_stateJson.empty()) j = "{\"age\":null,\"v\":{},\"s\":{}}";
        else { char a[32]; snprintf(a, sizeof(a), "{\"age\":%.2f,", (GetTickCount() - g_lastRx) / 1000.0); j = a + g_stateJson.substr(1); }
        LeaveCriticalSection(&g_cs);
        respond(c, "200 OK", "application/json", j, "Cache-Control: no-store\r\n");
    } else if (path == "/press" && method == "POST") {
        std::string name = queryParam(query, "t");
        if (name.empty()) { size_t p = body.find("\"t\""); if (p != std::string::npos) { size_t a = body.find('"', body.find(':', p) + 1); size_t b = body.find('"', a + 1); if (a != std::string::npos && b != std::string::npos) name = body.substr(a + 1, b - a - 1); } }
        if (!validName(name)) respond(c, "400 Bad Request", "text/plain", "bad");
        else {
            if (name == "key_shift_t") CloseHandle(CreateThread(NULL, 0, sendShiftT, NULL, 0, NULL));
            else {
                sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(CMD_PORT); a.sin_addr.s_addr = inet_addr("127.0.0.1");
                sendto(g_cmdSock, name.data(), (int)name.size(), 0, (sockaddr*)&a, sizeof(a));
            }
            respond(c, "200 OK", "text/plain", "ok");
        }
    } else if (path == "/tex") {
        handleTex(c, query);
    } else {
        respond(c, "404 Not Found", "text/plain", "not found");
    }
    shutdown(c, SD_SEND);
    closesocket(c);
    return 0;
}

static SOCKET g_listen = INVALID_SOCKET;
static bool startHttp(int firstPort) {
    for (int port = firstPort; port < firstPort + 20; port++) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (sockaddr*)&a, sizeof(a)) == 0 && listen(s, 32) == 0) { g_listen = s; g_httpPort = port; return true; }
        closesocket(s);
    }
    return false;
}
static DWORD WINAPI httpThread(LPVOID) {
    for (;;) {
        SOCKET c = accept(g_listen, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        HANDLE t = CreateThread(NULL, 0, clientThread, (LPVOID)(UINT_PTR)c, 0, NULL);
        if (t) CloseHandle(t); else closesocket(c);
    }
}
static bool isPrivate(const std::string& ip) {
    return ip.rfind("192.168.", 0) == 0 || ip.rfind("10.", 0) == 0 ||
           (ip.rfind("172.", 0) == 0 && [&] { int b = atoi(ip.c_str() + 4); return b >= 16 && b <= 31; }());
}
// vsechny IPv4 adresy aktivnich sitovek; nahore ty s vychozi branou v soukromem rozsahu (domaci Wi-Fi / LAN)
static void listAddrs() {
    g_addrs.clear();
    ULONG len = 32768; std::vector<BYTE> buf(len);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, NULL, aa, &len) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(len); aa = (IP_ADAPTER_ADDRESSES*)buf.data();
        if (GetAdaptersAddresses(AF_INET, flags, NULL, aa, &len) != NO_ERROR) aa = NULL;
    }
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
            std::string ip = inet_ntoa(sin->sin_addr);
            if (ip.rfind("169.254.", 0) == 0) continue;
            g_addrs.push_back({ ip, a->FriendlyName ? a->FriendlyName : L"", a->FirstGatewayAddress != NULL });
        }
    }
    std::stable_sort(g_addrs.begin(), g_addrs.end(), [](const NetAddr& x, const NetAddr& y) {
        int sx = (x.gateway ? 2 : 0) + (isPrivate(x.ip) ? 1 : 0), sy = (y.gateway ? 2 : 0) + (isPrivate(y.ip) ? 1 : 0);
        return sx > sy;
    });
    if (g_addrs.empty()) g_addrs.push_back({ "127.0.0.1", L"jen tento počítač", false });
}

// ---------------------------------------------------------------------------
// openOMSI: diagnostika z jeho logu (%USERPROFILE%\.openomsi\game.log)
// ---------------------------------------------------------------------------
static std::wstring g_omsiDiag;   // text pro okno, prazdny = vse OK / nic nevime
static void checkOpenOmsi() {
    wchar_t prof[MAX_PATH];
    if (!GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) return;
    std::string log;
    if (!readFile(std::wstring(prof) + L"\\.openomsi\\game.log", log)) return;
    size_t st = log.rfind("openomsi_game] openOMSI ");
    if (st == std::string::npos) return;
    std::string run = log.substr(st);
    std::wstring diag, content;
    size_t cf = run.find("content folder (mods): ");
    if (cf != std::string::npos) {
        size_t e = run.find_first_of("\r\n", cf);
        content = widen(run.substr(cf + 23, e - cf - 23));
    }
    bool loaded = false, failed = false, hostMissing = false;
    size_t pos = 0;
    while ((pos = run.find("SynergyBridge", pos)) != std::string::npos) {
        size_t ls = run.rfind('\n', pos); ls = ls == std::string::npos ? 0 : ls + 1;
        size_t le = run.find('\n', pos);
        std::string line = run.substr(ls, le - ls);
        if (line.find("loaded") != std::string::npos && line.find("Could not") == std::string::npos) loaded = true;
        if (line.find("Could not load plugin") != std::string::npos) { failed = true; if (line.find("omsi-plugin-host32") != std::string::npos) hostMissing = true; }
        pos = le == std::string::npos ? run.size() : le;
    }
    if (hostMissing) diag = L"openOMSI: chybí omsi-plugin-host32.exe ve složce hry (smazal ho antivirus?) - plugin se nenačte.";
    else if (failed && !loaded) diag = L"openOMSI plugin nenačetl - podrobnosti v %USERPROFILE%\\.openomsi\\game.log";
    // duplicitni plugin v content folderu
    if (!content.empty()) {
        std::wstring a = content, b = g_omsiDir; CharLowerW(&a[0]); if (!b.empty()) CharLowerW(&b[0]);
        if (a != b && fileExists(content + L"\\plugins\\SynergyBridge.opl") && isOmsiDir(g_omsiDir) && fileExists(g_omsiDir + L"\\plugins\\SynergyBridge.opl")) {
            if (!diag.empty()) diag += L"\n";
            diag += L"Plugin je nainstalovaný dvakrát - smaž SynergyBridge.dll a .opl z " + content + L"\\plugins";
        }
    }
    EnterCriticalSection(&g_cs); g_contentDir = content; LeaveCriticalSection(&g_cs);
    g_omsiDiag = diag;
}

// ---------------------------------------------------------------------------
// firewall: povolit prichozi spojeni pro tuto aplikaci (s pravy spravce)
// ---------------------------------------------------------------------------
static void allowFirewall(HWND owner) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
    std::wstring args = L"/c netsh advfirewall firewall delete rule name=\"Synergy OCC Tablet\" >nul 2>&1 & "
                        L"netsh advfirewall firewall add rule name=\"Synergy OCC Tablet\" dir=in action=allow enable=yes profile=any program=\"";
    args += self; args += L"\"";
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS; sei.hwnd = owner; sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe"; sei.lpParameters = args.c_str(); sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return;
    WaitForSingleObject(sei.hProcess, 20000);
    DWORD code = 1; GetExitCodeProcess(sei.hProcess, &code); CloseHandle(sei.hProcess);
    MessageBoxW(owner, code == 0 ? L"Pravidlo ve firewallu je přidané. Zkus tablet připojit znovu."
                                 : L"Pravidlo se nepodařilo přidat.", APP_TITLE, code == 0 ? MB_ICONINFORMATION : MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------
enum { ID_BROWSE = 100, ID_INSTALL, ID_OPEN, ID_COPY, ID_FIREWALL, ID_IPCOMBO, ID_TIMER };
static HWND h_omsi, h_plugin, h_url, h_stOmsi, h_stTablet, h_btnInstall, h_synergyWarn, h_ip, h_diag;
static HFONT f_ui, f_bold, f_title, f_url;
static float g_scale = 1.0f;
static int S(int v) { return (int)(v * g_scale + 0.5f); }
static RECT g_qrRect;
static uint8_t g_qr[qrcodegen_BUFFER_LEN_MAX];
static bool g_qrOk = false;
static int g_tick = 0;

static HWND label(HWND p, const wchar_t* t, int x, int y, int w, int h, HFONT f) {
    HWND c = CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, S(x), S(y), S(w), S(h), p, NULL, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}
static HWND button(HWND p, const wchar_t* t, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(x), S(y), S(w), S(h), p, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)f_ui, TRUE);
    return c;
}

static void setUrl(const std::string& ip) {
    g_ip = ip;
    g_url = "http://" + g_ip + ":" + std::to_string(g_httpPort);
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    g_qrOk = qrcodegen_encodeText(g_url.c_str(), tmp, g_qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    if (h_url) SetWindowTextW(h_url, widen(g_url).c_str());
    if (g_wnd) InvalidateRect(g_wnd, &g_qrRect, TRUE);
}

static void refreshStatic() {
    SetWindowTextW(h_omsi, isOmsiDir(g_omsiDir) ? g_omsiDir.c_str() : L"OMSI 2 nenalezeno - vyber složku ručně");
    bool syn = isOmsiDir(g_omsiDir) && dirExists(g_omsiDir + L"\\Vehicles\\SynergyOCC");
    SetWindowTextW(h_synergyWarn, (!isOmsiDir(g_omsiDir) || syn) ? L"" : L"Pozor: ve Vehicles chybí složka SynergyOCC");
    switch (pluginState()) {
        case PS_NO_OMSI: SetWindowTextW(h_plugin, L"-"); SetWindowTextW(h_btnInstall, L"Nainstalovat"); break;
        case PS_MISSING: SetWindowTextW(h_plugin, L"Není nainstalovaný"); SetWindowTextW(h_btnInstall, L"Nainstalovat"); break;
        case PS_OLD: SetWindowTextW(h_plugin, L"Starší verze - aktualizuj"); SetWindowTextW(h_btnInstall, L"Aktualizovat"); break;
        case PS_OK: SetWindowTextW(h_plugin, L"Nainstalovaný, aktuální verze"); SetWindowTextW(h_btnInstall, L"Přeinstalovat"); break;
    }
}
static void refreshLive() {
    EnterCriticalSection(&g_cs);
    DWORD now = GetTickCount();
    bool omsi = g_lastRx && now - g_lastRx < 2000;
    bool tab = g_lastTablet && now - g_lastTablet < 3000;
    bool tabEver = g_lastTablet != 0;
    std::wstring missing = g_missingTex;
    LeaveCriticalSection(&g_cs);
    SetWindowTextW(h_stOmsi, omsi ? L"●  OMSI posílá data" : L"○  Čekám na OMSI (spusť hru a sedni do vozu se Synergy)");
    if (tab) SetWindowTextW(h_stTablet, L"●  Tablet připojený");
    else if (!tabEver && now - g_startTick > 90000) SetWindowTextW(h_stTablet, L"○  Tablet se nepřipojil - stejná Wi-Fi? správná adresa? zkus „Povolit ve firewallu“");
    else SetWindowTextW(h_stTablet, L"○  Tablet zatím nepřipojený");

    std::wstring diag;
    if (g_udpError) diag = L"Nelze otevřít port " + std::to_wstring(STATE_PORT) + L" pro data z pluginu - nepoužívá ho jiný program?";
    if (!omsi && !g_omsiDiag.empty()) { if (!diag.empty()) diag += L"\n"; diag += g_omsiDiag; }
    if (!missing.empty()) { if (!diag.empty()) diag += L"\n"; diag += L"Nenalezen obrázek kasy: SynergyOCC\\" + missing; }
    wchar_t cur[1024]; GetWindowTextW(h_diag, cur, 1024);
    if (diag != cur) SetWindowTextW(h_diag, diag.c_str());
}

static void browseOmsi(HWND w) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = w; bi.lpszTitle = L"Vyber složku OMSI 2 (obsahuje Omsi.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, path)) {
        if (isOmsiDir(path)) {
            EnterCriticalSection(&g_cs); g_omsiDir = path; g_texRoot.clear(); LeaveCriticalSection(&g_cs);
            cfgSet(L"omsi_dir", g_omsiDir); refreshStatic();
        }
        else MessageBoxW(w, L"V téhle složce není Omsi.exe.", APP_TITLE, MB_ICONWARNING);
    }
    CoTaskMemFree(pidl);
}

static void copyUrl(HWND w) {
    std::wstring u = widen(g_url);
    if (!OpenClipboard(w)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (u.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(g), u.c_str(), (u.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g);
    SetClipboardData(CF_UNICODETEXT, g);
    CloseClipboard();
}

static void paintQr(HDC dc) {
    if (!g_qrOk) return;
    int size = qrcodegen_getSize(g_qr);
    int total = size + 8;   // ticha zona 4 moduly
    int box = g_qrRect.right - g_qrRect.left;
    int m = box / total; if (m < 1) m = 1;
    int off = (box - m * total) / 2;
    HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH), black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(dc, &g_qrRect, GetSysColorBrush(COLOR_WINDOW));
    RECT r = { g_qrRect.left + off, g_qrRect.top + off, g_qrRect.left + off + m * total, g_qrRect.top + off + m * total };
    FillRect(dc, &r, white);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) if (qrcodegen_getModule(g_qr, x, y)) {
        RECT q = { r.left + (x + 4) * m, r.top + (y + 4) * m, r.left + (x + 5) * m, r.top + (y + 5) * m };
        FillRect(dc, &q, black);
    }
}

static void fillIpCombo() {
    SendMessageW(h_ip, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    std::string saved = narrow(cfgGet(L"ip", L""));
    for (size_t i = 0; i < g_addrs.size(); i++) {
        std::wstring t = widen(g_addrs[i].ip) + L"   (" + g_addrs[i].name + L")";
        SendMessageW(h_ip, CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (g_addrs[i].ip == saved) sel = (int)i;
    }
    SendMessageW(h_ip, CB_SETCURSEL, sel, 0);
    setUrl(g_addrs[sel].ip);
}

static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = w;
        int y = 16;
        label(w, APP_TITLE, 20, y, 360, 30, f_title);
        label(w, L"verze " APP_VERSION, 430, y + 10, 90, 20, f_ui);
        y += 46;
        label(w, L"1.  Složka OMSI 2", 20, y, 300, 20, f_bold);
        y += 24;
        h_omsi = label(w, L"", 36, y, 360, 20, f_ui);
        button(w, L"Změnit…", ID_BROWSE, 410, y - 4, 110, 28);
        y += 22;
        h_synergyWarn = label(w, L"", 36, y, 380, 20, f_ui);
        y += 28;
        label(w, L"2.  Plugin do OMSI", 20, y, 300, 20, f_bold);
        y += 24;
        h_plugin = label(w, L"", 36, y, 360, 20, f_ui);
        h_btnInstall = button(w, L"Nainstalovat", ID_INSTALL, 410, y - 4, 110, 28);
        y += 40;
        label(w, L"3.  Na tabletu otevři v Safari / Chrome", 20, y, 380, 20, f_bold);
        y += 26;
        h_url = label(w, L"", 36, y, 290, 30, f_url);
        g_qrRect = { S(330), S(y - 6), S(330 + 190), S(y - 6 + 190) };
        y += 36;
        label(w, L"Síť:", 36, y + 4, 30, 20, f_ui);
        h_ip = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                             S(68), S(y), S(250), S(200), w, (HMENU)(INT_PTR)ID_IPCOMBO, NULL, NULL);
        SendMessageW(h_ip, WM_SETFONT, (WPARAM)f_ui, TRUE);
        y += 36;
        button(w, L"Otevřít zde", ID_OPEN, 36, y, 136, 28);
        button(w, L"Kopírovat adresu", ID_COPY, 182, y, 136, 28);
        y += 36;
        button(w, L"Povolit ve firewallu", ID_FIREWALL, 36, y, 282, 28);
        y += 38;
        label(w, L"Na iPadu: Sdílet → Přidat na plochu = celá obrazovka", 36, y, 290, 40, f_ui);
        y += 50;
        h_stOmsi = label(w, L"", 20, y, 500, 20, f_ui);
        y += 24;
        h_stTablet = label(w, L"", 20, y, 500, 20, f_ui);
        y += 28;
        h_diag = label(w, L"", 20, y, 500, 62, f_ui);
        y += 70;
        label(w, L"by Arteyo258 · se svolením P3ta (Synergy OCC: P3ta, Jack Black, Swix)", 20, y, 500, 20, f_ui);
        fillIpCombo();
        refreshStatic(); refreshLive();
        SetTimer(w, ID_TIMER, 500, NULL);
        return 0;
    }
    case WM_TIMER:
        if (++g_tick % 10 == 1) checkOpenOmsi();   // kazdych 5 s
        refreshLive();
        return 0;
    case WM_ACTIVATE: if (LOWORD(wp) != WA_INACTIVE) refreshStatic(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
            case ID_BROWSE: browseOmsi(w); break;
            case ID_INSTALL: installPluginInteractive(w); refreshStatic(); break;
            case ID_OPEN: ShellExecuteW(w, L"open", widen(g_url).c_str(), NULL, NULL, SW_SHOWNORMAL); break;
            case ID_COPY: copyUrl(w); break;
            case ID_FIREWALL: allowFirewall(w); break;
            case ID_IPCOMBO:
                if (HIWORD(wp) == CBN_SELCHANGE) {
                    int i = (int)SendMessageW(h_ip, CB_GETCURSEL, 0, 0);
                    if (i >= 0 && i < (int)g_addrs.size()) { setUrl(g_addrs[i].ip); cfgSet(L"ip", widen(g_addrs[i].ip)); }
                }
                break;
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT);
        HWND c = (HWND)lp;
        if (c == h_synergyWarn || c == h_diag) SetTextColor(dc, RGB(200, 50, 0));
        if (c == h_stOmsi || c == h_stTablet) {
            wchar_t t[8]; GetWindowTextW(c, t, 8);
            SetTextColor(dc, t[0] == L'●' ? RGB(0, 140, 0) : RGB(120, 120, 120));
        }
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps); paintQr(dc); EndPaint(w, &ps); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static HFONT mkFont(int pt, int weight) {
    HDC dc = GetDC(NULL);
    int h = -MulDiv(pt, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, dc);
    return CreateFontW(h, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // rezim spusteny s pravy spravce jen pro zkopirovani pluginu
    if (argc >= 3 && wcscmp(argv[1], L"--install") == 0) return (int)installPluginFiles(argv[2]);

    HANDLE mtx = CreateMutexW(NULL, TRUE, L"SynergyOccTabletApp");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"SynergyTabletWnd", NULL);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    (void)mtx;

    SetProcessDPIAware();
    { HDC dc = GetDC(NULL); g_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f; ReleaseDC(NULL, dc); }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);
    InitializeCriticalSection(&g_cs);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    g_cmdSock = socket(AF_INET, SOCK_DGRAM, 0);
    g_startTick = GetTickCount();

    g_omsiDir = cfgGet(L"omsi_dir", L"");
    if (!isOmsiDir(g_omsiDir)) { g_omsiDir = findOmsi(); if (!g_omsiDir.empty()) cfgSet(L"omsi_dir", g_omsiDir); }
    checkOpenOmsi();

    int port = _wtoi(cfgGet(L"port", L"8765").c_str()); if (port <= 0) port = DEFAULT_HTTP_PORT;
    if (!startHttp(port)) { MessageBoxW(NULL, L"Nepodařilo se spustit server (všechny porty obsazené).", APP_TITLE, MB_ICONERROR); return 1; }
    listAddrs();

    CloseHandle(CreateThread(NULL, 0, udpThread, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, httpThread, NULL, 0, NULL));

    f_ui = mkFont(9, FW_NORMAL); f_bold = mkFont(10, FW_SEMIBOLD); f_title = mkFont(15, FW_BOLD); f_url = mkFont(13, FW_SEMIBOLD);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"SynergyTabletWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);
    RECT r = { 0, 0, S(540), S(600) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    CreateWindowW(L"SynergyTabletWnd", APP_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(g_wnd, show);

    // prvni spusteni: nabidnout instalaci pluginu
    PluginState ps = pluginState();
    if (ps == PS_MISSING || ps == PS_OLD) {
        if (MessageBoxW(g_wnd, ps == PS_MISSING ? L"Plugin do OMSI ještě není nainstalovaný. Nainstalovat teď?"
                                                : L"V OMSI je starší verze pluginu. Aktualizovat teď?", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            installPluginInteractive(g_wnd); refreshStatic();
        }
    }

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(g_wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return 0;
}

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include <algorithm>
#include "AtgConsole.h"
#include "AtgInput.h"
#include "AtgUtil.h"
#include "god_engine.h"
#include "archive_io.h"

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

typedef struct _XBOX_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} XBOX_STRING, *PXBOX_STRING;

extern "C" LONG WINAPI ObCreateSymbolicLink(PXBOX_STRING SymbolicLinkName, PXBOX_STRING DeviceName);

namespace ATG { D3DDevice* g_pd3dDevice = NULL; }
static ATG::Console g_con;
extern std::vector<std::string> g_needSaveTitles;
IDirect3DDevice9 *g_pSharedD3DDevice = NULL;
static bool g_clearNext = false;

static void LogPrintf(const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    buf[sizeof(buf) - 1] = '\0';
    va_end(args);
    FILE *lf = fopen("game:\\omniconvert_log.txt", "a"); if (lf) { fputs(buf, lf); fclose(lf); }
    if (g_clearNext) { g_con.Clear(); g_clearNext = false; }
    g_con.Format("%s", buf); g_con.Render();
}

static void ClearScreen() { g_clearNext = true; }

static void MountDrives() {
    struct { const char *link; const char *dev; } mounts[] = {
        { "\\??\\Hdd1:", "\\Device\\Harddisk0\\Partition1" },
        { "\\??\\Usb0:", "\\Device\\Mass0" },
        { "\\??\\Usb1:", "\\Device\\Mass1" },
    };
    for (int i = 0; i < 3; ++i) {
        XBOX_STRING sLink = { (USHORT)strlen(mounts[i].link), (USHORT)strlen(mounts[i].link) + 1, (PCHAR)mounts[i].link };
        XBOX_STRING sDev  = { (USHORT)strlen(mounts[i].dev),  (USHORT)strlen(mounts[i].dev) + 1,  (PCHAR)mounts[i].dev };
        ObCreateSymbolicLink(&sLink, &sDev);
    }
}

static void WaitButtonRelease() {
    XINPUT_STATE st;
    while (XInputGetState(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons != 0) Sleep(30);
}

static bool PromptYesNo(const char *title, const char *question) {
    WaitButtonRelease();
    ClearScreen();
    LogPrintf("%s\n\n%s\n\n  A: Yes\n  B: No\n", title, question);
    XINPUT_STATE st;
    while (true) {
        if (XInputGetState(0, &st) == ERROR_SUCCESS) {
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A) { WaitButtonRelease(); return true; }
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_B) { WaitButtonRelease(); return false; }
        }
        Sleep(40);
    }
}

struct QueueItem {
    std::string sourcePath;
    std::string displayName;
    bool deleteAfter;
};
#include "queue_persist.h"

static bool RunAuroraTargetIfPresent() {
    FILE *f = fopen("game:\\target.txt", "r");
    if (!f) return false;

    char srcPath[512] = {0};
    char dstRoot[512] = "Hdd1:\\Content\\0000000000000000";
    int delFlag = 0;
    if (fgets(srcPath, sizeof(srcPath), f)) srcPath[strcspn(srcPath, "\r\n")] = '\0';
    if (fgets(dstRoot, sizeof(dstRoot), f)) dstRoot[strcspn(dstRoot, "\r\n")] = '\0';
    fscanf(f, "%d", &delFlag);
    fclose(f);
    DeleteFileA("game:\\target.txt");

    if (!srcPath[0]) return false;
    ClearScreen();
    LogPrintf("ISO2GOD-360 (Aurora Launch Mode)\nSource: %s\nTarget: %s\n\n", srcPath, dstRoot);
    const char *base = strrchr(srcPath, '\\');
    int rc = ConvertToGodStandalone(srcPath, dstRoot, base ? base + 1 : srcPath, LogPrintf);
    if (rc == 0 && delFlag == 1) {
        LogPrintf("Cleaning up source files...\n");
        DeletePathRecursive(srcPath);
    }
    LogPrintf("\n%s! Press A or B to exit.\n", rc == 0 ? "Conversion Complete" : "Conversion Failed");
    WaitButtonRelease();
    XINPUT_STATE st;
    while (true) {
        if (XInputGetState(0, &st) == ERROR_SUCCESS && (st.Gamepad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B))) break;
        Sleep(50);
    }
    return true;
}

static void InitConsoleWithEmbeddedFont() {
    PVOID pFontData = NULL;
    DWORD cbFontData = 0;
    if (XGetModuleSection(GetModuleHandle(NULL), "font", &pFontData, &cbFontData) && pFontData && cbFontData > 0) {
        HANDLE hf = CreateFileA("game:\\font.xpr", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hf, pFontData, cbFontData, &written, NULL);
            CloseHandle(hf);
        }
    }
     g_con.Create("game:\\font.xpr", 0xFF000000, 0xFFFFFFFF); g_pSharedD3DDevice = g_con.GetDevice(); 
}

static std::string ReadGodDisplayName(const unsigned char *hdr, size_t len, const char *fallbackId) {
    std::string name;
    if (len >= 0x1712) {
        for (int o = 0; o < 2 && name.empty(); ++o) {
            int baseOff = (o == 0) ? 0x412 : 0x1692;
            bool isLE = (hdr[baseOff] != 0 && hdr[baseOff + 1] == 0);
            for (int k = 0; k < 64; ++k) {
                unsigned short ch = isLE
                    ? ((unsigned short)hdr[baseOff + k * 2] | ((unsigned short)hdr[baseOff + k * 2 + 1] << 8))
                    : (((unsigned short)hdr[baseOff + k * 2] << 8) | (unsigned short)hdr[baseOff + k * 2 + 1]);
                if (ch == 0) break;
                if (ch >= 32 && ch < 127) name += (char)ch;
            }
            while (!name.empty() && name[name.size() - 1] == ' ') name.resize(name.size() - 1);
        }
    }
    return name.empty() ? std::string(fallbackId) : name;
}

static void ScanInstalledGodTitles(const char *driveRoot, std::vector<std::string> &labels, std::vector<std::string> &paths, std::vector<bool> &isDir) {
    struct TitleEntry { std::string label; std::string path; };
    std::vector<TitleEntry> found;
    std::string base = std::string(driveRoot) + "Content\\0000000000000000\\";
    WIN32_FIND_DATAA fdTid;
    HANDLE hTid = FindFirstFileA((base + "*").c_str(), &fdTid);
    if (hTid == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fdTid.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fdTid.cFileName[0] == '.') continue;
        const char *subTypes[] = { "00005000", "00007000" };
        for (int t = 0; t < 2; ++t) {
            std::string subDir = base + fdTid.cFileName + "\\" + subTypes[t] + "\\";
            WIN32_FIND_DATAA fdCon;
            HANDLE hCon = FindFirstFileA((subDir + "*").c_str(), &fdCon);
            if (hCon == INVALID_HANDLE_VALUE) continue;
            do {
                if (fdCon.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (fdCon.nFileSizeHigh != 0 || fdCon.nFileSizeLow != 0xB000) continue;
                std::string hdrPath = subDir + fdCon.cFileName;
                if (GetFileAttributesA((hdrPath + ".data").c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                FILE *f = fopen(hdrPath.c_str(), "rb");
                if (!f) continue;
                std::vector<unsigned char> hdr(0x1800, 0);
                size_t rd = fread(&hdr.front(), 1, 0x1800, f);
                fclose(f);
                if (rd < 0x1712 || (memcmp(&hdr.front(), "LIVE", 4) != 0 && memcmp(&hdr.front(), "CON ", 4) != 0 && memcmp(&hdr.front(), "PIRS", 4) != 0)) continue;
                TitleEntry te;
                te.label = std::string(t == 0 ? "[OG Xbox] " : "[Xbox 360] ") + ReadGodDisplayName(&hdr.front(), rd, fdTid.cFileName);
                te.path = hdrPath;
                found.push_back(te);
            } while (FindNextFileA(hCon, &fdCon));
            FindClose(hCon);
        }
    } while (FindNextFileA(hTid, &fdTid));
    FindClose(hTid);
    for (size_t i = 0; i < found.size(); ++i) {
        for (size_t j = i + 1; j < found.size(); ++j) {
            if (_stricmp(found[j].label.c_str(), found[i].label.c_str()) < 0) { TitleEntry tmp = found[i]; found[i] = found[j]; found[j] = tmp; }
        }
    }
    for (size_t i = 0; i < found.size(); ++i) { labels.push_back(found[i].label); paths.push_back(found[i].path); isDir.push_back(false); }
}

static int g_saverStyle = 0; // 0=Confetti->Matrix, 1=Confetti, 2=Matrix, 3=Starfield
static int g_saverColor = 0; // 0=Rainbow, 1=Green, 2=Cyan, 3=Gold, 4=Red, 5=White
static void LoadSaverSettings() { FILE *f = fopen("game:\\screensaver.ini", "r"); if (f) { fscanf(f, "%d %d", &g_saverStyle, &g_saverColor); fclose(f); } }
static void SaveSaverSettings() { FILE *f = fopen("game:\\screensaver.ini", "w"); if (f) { fprintf(f, "%d %d\n", g_saverStyle, g_saverColor); fclose(f); } }
static char GetSaverCol(int t, int idx, DWORD s) {
    const char *rb = "\x81\x90\x8D\x8F\x9D";
    if (t == 1) return idx == 0 ? '\x9D' : '\x81';
    if (t == 2) return idx == 0 ? '\x9D' : '\x8F';
    if (t == 3) return idx == 0 ? '\x9D' : '\x90';
    if (t == 4) return idx == 0 ? '\x9D' : '\x8D';
    if (t == 5) return '\x9D';
    return rb[(idx + (int)(s & 7)) % 5];
}
static void ShowSaveDataNeededAlert() {
    if (g_needSaveTitles.empty()) return;
    FILE *nf = fopen("game:\\needs_savedata.txt", "w");
    std::string scr = "\n  \x90==========================================================\x9D\n";
    scr += "  \x8D[ACTION NEEDED] MISSING NXE ICONS / TITLES:\x9D\n";
    scr += "  \x90==========================================================\x9D\n\n";
    for (size_t k = 0; k < g_needSaveTitles.size(); ++k) {
        if (nf) fprintf(nf, "%s\n", g_needSaveTitles[k].c_str());
        if (k < 12) { scr += "    - "; scr += g_needSaveTitles[k]; scr += "\n"; }
    }
    if (nf) fclose(nf);
    if (g_needSaveTitles.size() > 12) scr += "    ... (full list saved to game:\\needs_savedata.txt)\n";
    scr += "\n  \x81Launch each game once to create save data, then re-run Patch!\x9D\n";
    scr += "\n  \x8FPress A to continue...\x9D\n";
    g_needSaveTitles.clear();
    WaitButtonRelease();
    g_con.ClearNoRender(); g_con.Display(scr.c_str());
    XINPUT_STATE st;
    for (;;) {
        ZeroMemory(&st, sizeof(st));
        if (XInputGetState(0, &st) == ERROR_SUCCESS && (st.Gamepad.wButtons & XINPUT_GAMEPAD_A)) { WaitButtonRelease(); break; }
        Sleep(50);
    }
}
static void ShowRainbowCelebration(const char *headline, const char *detail, bool forceMinConfetti = true) {
    WaitButtonRelease();
    const int W = 58, H = 16; DWORD startTick = GetTickCount(), seed = GetTickCount();
    const char *cGlyphs = "*+o.#~*", *mGlyphs = "0123456789ABCDEFXYZ#$%@*+=:<>";
    int numMG = (int)strlen(mGlyphs), burst = 0;
    struct Pt { int x, y, vx, vy, c, g, l; }; std::vector<Pt> pts(36);
    for (size_t i = 0; i < pts.size(); ++i) { seed = seed * 1664525U + 1013904223U; pts[i].x = (int)((seed >> 8) % W); pts[i].y = (int)((seed >> 16) % H); pts[i].vx = (i % 5 == 0) ? 1 : (i % 5 == 1 ? -1 : 0); pts[i].vy = 1; pts[i].c = (int)(i % 5); pts[i].g = (int)(i % 7); pts[i].l = 12; }
    std::vector<int> hy(W, 0), tl(W, 4), sp(W, 1), tk(W, 0);
    for (int c = 0; c < W; ++c) { seed = seed * 1664525U + 1013904223U; hy[c] = (int)((seed >> 8) % H); tl[c] = 3 + (int)((seed >> 16) % 7); sp[c] = 1 + (int)((seed >> 24) % 2); }
    XINPUT_STATE st;
    for (;;) {
        ZeroMemory(&st, sizeof(st));
        if (XInputGetState(0, &st) == ERROR_SUCCESS && (st.Gamepad.wButtons != 0 || abs(st.Gamepad.sThumbLX) > 16000 || abs(st.Gamepad.sThumbLY) > 16000)) { WaitButtonRelease(); return; }
        bool inRainbowIntro = forceMinConfetti && ((GetTickCount() - startTick) < 60000U);
        int stNow = inRainbowIntro ? 0 : g_saverStyle;
        int colNow = inRainbowIntro ? 0 : g_saverColor;
        std::vector<std::string> grid(H, std::string(W, ' ')), cg(H, std::string(W, '\x9D'));
        if (stNow == 0) {
            if (++burst % 14 == 0) { seed = seed * 1664525U + 1013904223U; int bx = 6 + (int)((seed >> 8) % (W - 12)), by = (burst % 28 == 0) ? 2 : (H - 3); int dx[] = { -2,0,2,-2,2,-1,0,1 }, dy[] = { -1,-1,-1,0,0,1,1,1 }; for (int k = 0; k < 8; ++k) { size_t idx = (size_t)((burst + k) % pts.size()); pts[idx].x = bx; pts[idx].y = by; pts[idx].vx = dx[k]; pts[idx].vy = dy[k]; pts[idx].c = (burst + k) % 5; pts[idx].g = k % 7; pts[idx].l = 8; } }
            for (size_t i = 0; i < pts.size(); ++i) { if (pts[i].y >= 0 && pts[i].y < H && pts[i].x >= 0 && pts[i].x < W) { grid[pts[i].y][pts[i].x] = cGlyphs[pts[i].g]; cg[pts[i].y][pts[i].x] = GetSaverCol(colNow, pts[i].c, (DWORD)i); } pts[i].x = (pts[i].x + pts[i].vx + W) % W; pts[i].y += pts[i].vy; if (--pts[i].l <= 0 || pts[i].y >= H || pts[i].y < 0) { seed = seed * 1664525U + 1013904223U; pts[i].x = (int)((seed >> 8) % W); pts[i].y = 0; pts[i].vx = (i % 3 == 0) ? 1 : (i % 3 == 1 ? -1 : 0); pts[i].vy = 1; pts[i].c = (pts[i].c + 1) % 5; pts[i].l = 18; } }
        } else if (stNow == 1) {
            for (int c = 0; c < W; ++c) { for (int r = 0; r < H; ++r) { int d = hy[c] - r; if (d < 0) d += H; if (d == 0) { seed = seed * 1664525U + 1013904223U; grid[r][c] = mGlyphs[(seed >> 12) % numMG]; cg[r][c] = GetSaverCol(colNow, 0, c); } else if (d < tl[c]) { grid[r][c] = mGlyphs[(c * 7 + r * 13 + hy[c]) % numMG]; cg[r][c] = GetSaverCol(colNow, 1, c + r); } } if (++tk[c] >= sp[c]) { tk[c] = 0; hy[c] = (hy[c] + 1) % H; } }
        } else {
            for (size_t i = 0; i < pts.size(); ++i) { int cx = W / 2, cy = H / 2, dx = pts[i].x - cx, dy = pts[i].y - cy; if (!dx && !dy) dx = 1; pts[i].x += (dx > 0 ? 1 : (dx < 0 ? -1 : 0)) * (abs(dx) > 14 ? 2 : 1); pts[i].y += (dy > 0 ? 1 : (dy < 0 ? -1 : 0)); if (pts[i].x < 0 || pts[i].x >= W || pts[i].y < 0 || pts[i].y >= H) { seed = seed * 1664525U + 1013904223U; pts[i].x = cx - 4 + (int)((seed >> 8) % 9); pts[i].y = cy - 2 + (int)((seed >> 16) % 5); } grid[pts[i].y][pts[i].x] = abs(pts[i].x - cx) > 16 ? '*' : '.'; cg[pts[i].y][pts[i].x] = GetSaverCol(colNow, (int)(i % 5), (DWORD)i); }
        }
        for (int r = 5; r <= 11; ++r) for (int c = 3; c <= W - 4; ++c) grid[r][c] = ' ';
        std::string frame; frame.reserve(1200);
        for (int r = 0; r < H; ++r) {
            if (r == 5 || r == 11) { frame += "   \x90==================================================\x9D\n"; continue; }
            if (r == 6) { char b[96]; _snprintf(b, sizeof(b), "   \x81  %-46.46s  \x9D\n", headline); frame += b; continue; }
            if (r == 8) { char b[96]; _snprintf(b, sizeof(b), "   \x9D  %-46.46s  \x9D\n", detail); frame += b; continue; }
            if (r == 10) { frame += "   \x8F  Press any button or move stick to continue... \x9D\n"; continue; }
            char lc = '\x9D';
            for (int c = 0; c < W; ++c) { if (grid[r][c] != ' ' && cg[r][c] != lc) { frame += cg[r][c]; lc = cg[r][c]; } frame += grid[r][c]; }
            if (lc != '\x9D') frame += "\x9D";
            frame += "\n";
        }
        if (frame.size() > 1250) frame.resize(1250);
        g_con.ClearNoRender(); g_con.Display(frame.c_str()); Sleep(95);
    }
}

static void ShowScreensaverSettingsMenu() {
    const char *sNames[] = { "Confetti", "Matrix Digital Rain", "Starfield Warp" };
    const char *cNames[] = { "Rainbow", "Matrix Green", "Cyber Cyan", "Solar Gold", "Crimson Red", "Pure White" };
    WaitButtonRelease(); WORD prev = 0; XINPUT_STATE st;
    for (;;) {
        char buf[512]; _snprintf(buf, sizeof(buf), "OmniConvert360 - Screensaver Settings\n=====================================\n(Success screen always shows 1 min Rainbow Confetti first)\n\n  \x90" "Style [LB/RB]:\x9D  %s\n  \x90" "Color [Y]:\x9D      %s\n\n  \x8F" "[X]\x9D Preview Selected Screensaver\n  \x81" "[A/B]\x9D Save & Return\n", sNames[g_saverStyle % 3], cNames[g_saverColor % 6]);
        g_con.ClearNoRender(); g_con.Display(buf);
        ZeroMemory(&st, sizeof(st)); WORD btn = (XInputGetState(0, &st) == ERROR_SUCCESS) ? st.Gamepad.wButtons : 0; WORD prs = btn & ~prev; prev = btn;
        if (prs & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_LEFT)) g_saverStyle = (g_saverStyle + 2) % 3;
        if (prs & (XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_DPAD_RIGHT)) g_saverStyle = (g_saverStyle + 1) % 3;
        if (prs & (XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) g_saverColor = (g_saverColor + 1) % 6;
        if (prs & XINPUT_GAMEPAD_X) { ShowRainbowCelebration("SCREENSAVER PREVIEW", sNames[g_saverStyle % 3], false); prev = 0; }
        if (prs & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B)) { SaveSaverSettings(); WaitButtonRelease(); return; }
        Sleep(40);
    }
}
int main() {
    MountDrives();
    InitConsoleWithEmbeddedFont();
    if (RunAuroraTargetIfPresent()) return 0;

    AppFolders cfg;
    LoadAppFolders(cfg);
    {
        std::vector<QueueItem> savedQ; std::vector<bool> savedDone; std::string savedDrive; int savedMode = 0;
        if (LoadSavedQueue(savedQ, savedDone, savedDrive, savedMode)) {
            size_t doneCnt = 0; for (size_t i = 0; i < savedDone.size(); ++i) if (savedDone[i]) doneCnt++;
            std::vector<char> msg(256, 0); _snprintf(&msg.front(), 256, "Interrupted queue (%u/%u done). Resume?", (unsigned int)doneCnt, (unsigned int)savedQ.size());
            if (PromptYesNo("Resume Previous Queue?", &msg.front())) {
                ExecuteConversionBatch(savedQ, savedDone, savedDrive.c_str(), savedMode, cfg);
                ShowSaveDataNeededAlert(); ShowRainbowCelebration("RESUMED QUEUE FINISHED!", "Press any button to return to OmniConvert360.");
            } else DeleteFileA(QUEUE_STATE_FILE);
        }
    }

    std::vector<std::string> pathStack;
    std::vector<QueueItem> queue;
    int destDriveIdx = 0, outMode = 0, presetIdx = 0;
    const char *destDrives[] = { "Hdd1:\\", "Usb0:\\" };
    const char *modeNames[] = { "GOD (00007000 / 00005000)", "Extracted Folder (GAMES/XGAMES)", "XISO (.iso)", "Multi-Part ZIP (.zip.001)", "Fix GOD Names & Icons (1-Sec Patch)" };
    int selected = 0; WORD prevButtons = 0;
    std::vector<std::string> labels, paths; std::vector<bool> isDir;
    bool dirDirty = true;

    while (true) {
        if (dirDirty) {
            labels.clear(); paths.clear(); isDir.clear();
            if (outMode == 4) {
                ScanInstalledGodTitles(destDrives[destDriveIdx], labels, paths, isDir);
            } else if (pathStack.empty()) {
                MountOpticalDrive();
                const char *roots[] = { "Hdd1:\\", "Usb0:\\", "Usb1:\\", "Cdrom0:\\" };
                for (int r = 0; r < 4; ++r) if (GetFileAttributesA(roots[r]) != INVALID_FILE_ATTRIBUTES) { labels.push_back(std::string("[Drive] ") + roots[r]); paths.push_back(roots[r]); isDir.push_back(true); }
            } else {
                std::string cur = pathStack.back();
                std::string pat = cur + (cur[cur.size() - 1] == '\\' ? "*" : "\\*");
                WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat.c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                        std::string child = cur + (cur[cur.size() - 1] == '\\' ? "" : "\\") + fd.cFileName;
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            std::string dirLabel = std::string("[DIR] ") + fd.cFileName;
                            const char *subs[] = { "\\00005000\\*", "\\00007000\\*" };
                            for (int s = 0; s < 2; ++s) {
                                WIN32_FIND_DATAA fdc; HANDLE hc = FindFirstFileA((child + subs[s]).c_str(), &fdc);
                                if (hc != INVALID_HANDLE_VALUE) {
                                    do {
                                        if (!(fdc.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fdc.nFileSizeLow == 0xB000) {
                                            std::string hp = child + (s == 0 ? "\\00005000\\" : "\\00007000\\") + fdc.cFileName;
                                            FILE *hf = fopen(hp.c_str(), "rb");
                                            if (hf) { std::vector<unsigned char> hb(0x1800, 0); size_t hr = fread(&hb.front(), 1, 0x1800, hf); fclose(hf); if (hr >= 0x1712) { dirLabel = std::string(s == 0 ? "[OG Xbox] " : "[Xbox 360] ") + ReadGodDisplayName(&hb.front(), hr, fd.cFileName); break; } }
                                        }
                                    } while (FindNextFileA(hc, &fdc)); FindClose(hc);
                                }
                            }
                            labels.push_back(dirLabel); paths.push_back(child); isDir.push_back(true);
                        }
                        else {
                            size_t len = strlen(fd.cFileName);
                            bool isIso = (len > 4 && _stricmp(fd.cFileName + len - 4, ".iso") == 0) || (len > 8 && _stricmp(fd.cFileName + len - 8, ".iso.001") == 0);
                            bool isGod = (fd.nFileSizeHigh == 0 && fd.nFileSizeLow == 0xB000 && GetFileAttributesA((child + ".data").c_str()) != INVALID_FILE_ATTRIBUTES);
                            if (isIso) { labels.push_back(std::string("[ISO] ") + fd.cFileName); paths.push_back(child); isDir.push_back(false); }
                            else if (IsSupportedArchiveFile(fd.cFileName)) { labels.push_back(std::string("[ARC] ") + fd.cFileName); paths.push_back(child); isDir.push_back(false); }
                            else if (isGod) { labels.push_back(std::string("[GOD] ") + fd.cFileName); paths.push_back(child); isDir.push_back(false); }
                        }
                    } while (FindNextFileA(h, &fd)); FindClose(h);
                }
            }
            dirDirty = false;
        }
        if (selected >= (int)labels.size()) selected = labels.empty() ? 0 : (int)labels.size() - 1;
        if (selected < 0) selected = 0;

        char hdrBuf[1024];
        _snprintf(hdrBuf, sizeof(hdrBuf), "OmniConvert360 All-in-One Converter | Queued: %u\nMode [LB/RB]: %s\n\x90" "Dest [Y]:\x9D" " %s\nBrowse: %s\n\n",
                  (unsigned int)queue.size(), modeNames[outMode], destDrives[destDriveIdx], pathStack.empty() ? "Select Source Drive" : pathStack.back().c_str());
        std::string frame = hdrBuf;
        if (labels.empty()) frame += "  (No folders, .iso, or GOD headers here)\n";
        else {
            int start = selected > 9 ? selected - 9 : 0, end = std::min<int>((int)labels.size(), start + 13);
            for (int i = start; i < end; ++i) { if (i == selected) { frame += "\x8F> "; frame += labels[i]; frame += "\x9D\n"; } else { frame += "  "; frame += labels[i]; frame += "\n"; } }
        }
        char ftrBuf[512];
        _snprintf(ftrBuf, sizeof(ftrBuf), "\n\x81" "A: Open/Queue\x9D" "  \x8F" "X: Folder\x9D" "  \x81" "START: Go (%u)\x9D" "  \x90" "BACK: Saver\x9D" "  \x8D" "B: Back\x9D" "\n", (unsigned int)queue.size());
        frame += ftrBuf;
        g_con.ClearNoRender();
        g_con.Display(frame.c_str());

        XINPUT_STATE st; DWORD idleStart = GetTickCount();
        while (true) {
            ZeroMemory(&st, sizeof(st));
            WORD btn = (XInputGetState(0, &st) == ERROR_SUCCESS) ? st.Gamepad.wButtons : 0;
            WORD prs = btn & ~prevButtons; prevButtons = btn;
            if (prs & XINPUT_GAMEPAD_DPAD_UP && !labels.empty()) { selected = (selected - 1 + (int)labels.size()) % (int)labels.size(); break; }
            if (prs & XINPUT_GAMEPAD_DPAD_DOWN && !labels.empty()) { selected = (selected + 1) % (int)labels.size(); break; }
            if (st.Gamepad.bLeftTrigger > 120 && !labels.empty()) { selected = (selected >= 10) ? (selected - 10) : 0; Sleep(110); break; }
            if (st.Gamepad.bRightTrigger > 120 && !labels.empty()) { selected = (selected + 10 < (int)labels.size()) ? (selected + 10) : ((int)labels.size() - 1); Sleep(110); break; }
            if (prs & XINPUT_GAMEPAD_LEFT_SHOULDER) { outMode = (outMode + 4) % 5; dirDirty = true; break; }
            if (prs & XINPUT_GAMEPAD_RIGHT_SHOULDER) { outMode = (outMode + 1) % 5; dirDirty = true; break; }
            if (prs & XINPUT_GAMEPAD_Y) { destDriveIdx = 1 - destDriveIdx; dirDirty = true; break; }
            if (prs & XINPUT_GAMEPAD_BACK) { ShowScreensaverSettingsMenu(); dirDirty = true; break; }
            if (btn != 0 || abs(st.Gamepad.sThumbLX) > 16000 || abs(st.Gamepad.sThumbLY) > 16000 || st.Gamepad.bLeftTrigger > 60 || st.Gamepad.bRightTrigger > 60) idleStart = GetTickCount();
            else if ((GetTickCount() - idleStart) > 60000U) { ShowRainbowCelebration("OMNICONVERT360 IDLE", "Move stick or press any button to wake.", false); dirDirty = true; break; }

            if (prs & XINPUT_GAMEPAD_B) { if (pathStack.empty()) return 0; pathStack.pop_back(); selected = 0; dirDirty = true; break; }
            if (!labels.empty() && ((prs & XINPUT_GAMEPAD_A) || (!pathStack.empty() && isDir[selected] && (prs & XINPUT_GAMEPAD_X)))) {
                bool queueFolder = (prs & XINPUT_GAMEPAD_X) != 0;
                bool isCdrom = (_strnicmp(paths[selected].c_str(), "Cdrom0", 6) == 0);
                if (isDir[selected] && !queueFolder && !isCdrom) { pathStack.push_back(paths[selected]); selected = 0; dirDirty = true; break; }
                const char *nm = strrchr(paths[selected].c_str(), '\\');
                std::string disp = isCdrom ? "Xbox Optical Disc (Cdrom0)" : (nm && *(nm + 1) ? (nm + 1) : paths[selected]);
                QueueItem qi = { paths[selected], disp, false };
                qi.deleteAfter = (outMode == 4 || isCdrom) ? false : PromptYesNo(qi.displayName.c_str(), "Delete original source after converting?");
                queue.push_back(qi); break;
            }
            if ((prs & XINPUT_GAMEPAD_START) && !queue.empty()) {
                std::vector<bool> completed(queue.size(), false);
                ExecuteConversionBatch(queue, completed, destDrives[destDriveIdx], outMode, cfg);
                queue.clear();
                ShowSaveDataNeededAlert();
                ShowRainbowCelebration("ALL QUEUED ITEMS FINISHED!", "Press any button to return to OmniConvert360.");
                dirDirty = true; break;
                while (true) { if (XInputGetState(0, &st) == ERROR_SUCCESS) { if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A) break; if (st.Gamepad.wButtons & XINPUT_GAMEPAD_B) return 0; } Sleep(40); }
                WaitButtonRelease(); break;
            }
            Sleep(40);
        }
    }
}




















#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <algorithm>
#include <string>
#include <vector>
#include "archive_io.h"
extern "C" {
#include "miniz/miniz.h"
}

extern "C" {
#include "lzma/7z.h"
#include "lzma/7zAlloc.h"
#include "lzma/7zBuf.h"
#include "lzma/7zCrc.h"
#include "lzma/7zFile.h"

typedef struct _STRING { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING;
LONG __stdcall ObCreateSymbolicLink(PSTRING SymbolicLinkName, PSTRING DeviceName);
}

static const uint64_t FATX_SPLIT_LIMIT = 0xF0000000ULL;

bool MountOpticalDrive() {
    CHAR linkBuf[] = "\\??\\Cdrom0:";
    CHAR devBuf[]  = "\\Device\\Cdrom0";
    STRING sLink = { (USHORT)strlen(linkBuf), (USHORT)(strlen(linkBuf) + 1), linkBuf };
    STRING sDev  = { (USHORT)strlen(devBuf),  (USHORT)(strlen(devBuf) + 1),  devBuf };
    ObCreateSymbolicLink(&sLink, &sDev);
    return GetFileAttributesA("Cdrom0:\\") != INVALID_FILE_ATTRIBUTES;
}

static void EnsureDirTree(const std::string &dirPath) {
    std::string cur;
    for (size_t i = 0; i < dirPath.size(); ++i) {
        char c = (dirPath[i] == '/') ? '\\' : dirPath[i];
        cur += c;
        if (c == '\\' && cur.size() > 3 && cur[cur.size() - 2] != ':') CreateDirectoryA(cur.c_str(), NULL);
    }
    if (!cur.empty() && cur[cur.size() - 1] != '\\') CreateDirectoryA(cur.c_str(), NULL);
}

static uint32_t UpdateCrc32(uint32_t crc, const uint8_t *buf, size_t len) {
    static std::vector<uint32_t> tbl;
    if (tbl.empty()) {
        tbl.resize(256);
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
    }
    crc ^= 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) crc = tbl[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static void PutLE16(uint8_t *p, uint16_t v) { *(p + 0) = (uint8_t)v; *(p + 1) = (uint8_t)(v >> 8); }
static void PutLE32(uint8_t *p, uint32_t v) { *(p + 0) = (uint8_t)v; *(p + 1) = (uint8_t)(v >> 8); *(p + 2) = (uint8_t)(v >> 16); *(p + 3) = (uint8_t)(v >> 24); }
static void PutLE64(uint8_t *p, uint64_t v) { PutLE32(p, (uint32_t)v); PutLE32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t GetLE16(const uint8_t *p) { return (uint16_t)(*p) | ((uint16_t)*(p + 1) << 8); }
static uint32_t GetLE32(const uint8_t *p) { return (uint32_t)(*p) | ((uint32_t)*(p + 1) << 8) | ((uint32_t)*(p + 2) << 16) | ((uint32_t)*(p + 3) << 24); }
static uint64_t GetLE64(const uint8_t *p) { return (uint64_t)GetLE32(p) | ((uint64_t)GetLE32(p + 4) << 32); }

class SplitZipWriter {
public:
    std::string basePrefix; int partIdx; FILE *fp; uint64_t partBytes, totalBytes;
    SplitZipWriter(const std::string &p) : basePrefix(p), partIdx(1), fp(NULL), partBytes(0), totalBytes(0) {}
    ~SplitZipWriter() { if (fp) fclose(fp); }
    bool OpenNextPart() {
        if (fp) { fclose(fp); fp = NULL; }
        std::vector<char> nm(512, 0);
        _snprintf(&nm.front(), 512, "%s.zip.%03d", basePrefix.c_str(), partIdx++);
        fp = fopen(&nm.front(), "wb"); partBytes = 0; return fp != NULL;
    }
    bool Write(const void *data, size_t len) {
        const uint8_t *src = (const uint8_t *)data;
        while (len > 0) {
            if (!fp || partBytes >= FATX_SPLIT_LIMIT) { if (!OpenNextPart()) return false; }
            size_t room = (size_t)std::min<uint64_t>((uint64_t)len, FATX_SPLIT_LIMIT - partBytes);
            if (fwrite(src, 1, room, fp) != room) return false;
            src += room; len -= room; partBytes += room; totalBytes += room;
        }
        return true;
    }
};

struct ZipCdEntry { std::string name; uint32_t crc; uint64_t size, localOff; };

static void CollectFilesRec(const std::string &root, const std::string &rel, std::vector<std::pair<std::string, std::string> > &out) {
    std::string pat = root + (rel.empty() ? "" : "\\" + rel);
    if (!pat.empty() && pat[pat.size() - 1] != '\\') pat += "\\";
    pat += "*";
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        std::string childRel = rel.empty() ? fd.cFileName : (rel + "/" + fd.cFileName);
        std::string childFull = root + (root[root.size() - 1] == '\\' ? "" : "\\") + childRel;
        for (size_t i = 0; i < childFull.size(); ++i) if (childFull[i] == '/') childFull[i] = '\\';
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CollectFilesRec(root, childRel, out);
        else out.push_back(std::make_pair(childFull, childRel));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

bool CreateMultipartZipFromPath(const char *sourcePath, const char *destDir, const char *baseName, ArcLogFn logFn) {
    EnsureDirTree(destDir);
    std::string prefix = std::string(destDir) + "\\" + baseName;
    std::vector<std::pair<std::string, std::string> > files;
    DWORD attr = GetFileAttributesA(sourcePath);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) CollectFilesRec(sourcePath, "", files);
    else { const char *nm = strrchr(sourcePath, '\\'); files.push_back(std::make_pair(std::string(sourcePath), std::string(nm ? nm + 1 : sourcePath))); }
    if (files.empty()) return false;

    SplitZipWriter zw(prefix); std::vector<ZipCdEntry> cd; std::vector<uint8_t> ioBuf(262144);
    DWORD lastTick = GetTickCount();
    for (size_t i = 0; i < files.size(); ++i) {
        FILE *in = fopen(files[i].first.c_str(), "rb");
        if (!in) continue;
        _fseeki64(in, 0, SEEK_END); uint64_t fsz = (uint64_t)_ftelli64(in); _fseeki64(in, 0, SEEK_SET);
        uint32_t crc = 0; uint64_t hashed = 0;
        while (hashed < fsz) { size_t r = fread(&ioBuf.front(), 1, ioBuf.size(), in); if (r == 0) break; crc = UpdateCrc32(crc, &ioBuf.front(), r); hashed += r; }
        _fseeki64(in, 0, SEEK_SET);
        uint64_t localOff = zw.totalBytes; uint16_t nlen = (uint16_t)files[i].second.size();
        std::vector<uint8_t> lhdr(30, 0), z64(20, 0);
        PutLE32(&lhdr.front(), 0x04034B50U); PutLE16(&lhdr.front() + 4, 45); PutLE32(&lhdr.front() + 14, crc);
        PutLE32(&lhdr.front() + 18, 0xFFFFFFFFU); PutLE32(&lhdr.front() + 22, 0xFFFFFFFFU); PutLE16(&lhdr.front() + 26, nlen); PutLE16(&lhdr.front() + 28, 20);
        PutLE16(&z64.front(), 0x0001); PutLE16(&z64.front() + 2, 16); PutLE64(&z64.front() + 4, fsz); PutLE64(&z64.front() + 12, fsz);
        if (!zw.Write(&lhdr.front(), 30) || !zw.Write(files[i].second.data(), nlen) || !zw.Write(&z64.front(), 20)) { fclose(in); return false; }
        uint64_t written = 0;
        while (written < fsz) {
            size_t r = fread(&ioBuf.front(), 1, ioBuf.size(), in); if (r == 0) break;
            if (!zw.Write(&ioBuf.front(), r)) { fclose(in); return false; }
            written += r;
            if (logFn && GetTickCount() - lastTick >= 750) { lastTick = GetTickCount(); logFn("ZIP [%s]: File %u/%u (%.1f MB, Part %d)\n", baseName, (unsigned int)(i + 1), (unsigned int)files.size(), zw.totalBytes / (1024.0 * 1024.0), zw.partIdx - 1); }
        }
        fclose(in); ZipCdEntry e = { files[i].second, crc, fsz, localOff }; cd.push_back(e);
    }
    uint64_t cdStart = zw.totalBytes;
    for (size_t i = 0; i < cd.size(); ++i) {
        uint16_t nlen = (uint16_t)cd[i].name.size();
        std::vector<uint8_t> chdr(46, 0), z64cd(28, 0);
        PutLE32(&chdr.front(), 0x02014B50U); PutLE16(&chdr.front() + 4, 45); PutLE16(&chdr.front() + 6, 45); PutLE32(&chdr.front() + 16, cd[i].crc);
        PutLE32(&chdr.front() + 20, 0xFFFFFFFFU); PutLE32(&chdr.front() + 24, 0xFFFFFFFFU); PutLE16(&chdr.front() + 28, nlen); PutLE16(&chdr.front() + 30, 28); PutLE32(&chdr.front() + 42, 0xFFFFFFFFU);
        PutLE16(&z64cd.front(), 0x0001); PutLE16(&z64cd.front() + 2, 24); PutLE64(&z64cd.front() + 4, cd[i].size); PutLE64(&z64cd.front() + 12, cd[i].size); PutLE64(&z64cd.front() + 20, cd[i].localOff);
        if (!zw.Write(&chdr.front(), 46) || !zw.Write(cd[i].name.data(), nlen) || !zw.Write(&z64cd.front(), 28)) return false;
    }
    uint64_t cdEnd = zw.totalBytes;
    std::vector<uint8_t> z64eocd(56, 0), z64loc(20, 0), eocd(22, 0);
    PutLE32(&z64eocd.front(), 0x06064B50U); PutLE64(&z64eocd.front() + 4, 44); PutLE16(&z64eocd.front() + 12, 45); PutLE16(&z64eocd.front() + 14, 45);
    PutLE64(&z64eocd.front() + 24, cd.size()); PutLE64(&z64eocd.front() + 32, cd.size()); PutLE64(&z64eocd.front() + 40, cdEnd - cdStart); PutLE64(&z64eocd.front() + 48, cdStart);
    PutLE32(&z64loc.front(), 0x07064B50U); PutLE64(&z64loc.front() + 8, cdEnd); PutLE32(&z64loc.front() + 16, 1);
    PutLE32(&eocd.front(), 0x06054B50U); PutLE16(&eocd.front() + 8, 0xFFFF); PutLE16(&eocd.front() + 10, 0xFFFF); PutLE32(&eocd.front() + 12, 0xFFFFFFFFU); PutLE32(&eocd.front() + 16, 0xFFFFFFFFU);
    if (!zw.Write(&z64eocd.front(), 56) || !zw.Write(&z64loc.front(), 20) || !zw.Write(&eocd.front(), 22)) return false;
    if (logFn) logFn("\x8F" "Multi-part ZIP created: %s.zip.001 (%d parts)\x9D" "\n", prefix.c_str(), zw.partIdx - 1);
    return true;
}

static bool HasPartRarSuffix(const std::string &fn, std::string &prefix, int &digits, int &startNum) {
    size_t len = fn.size();
    if (len < 10 || _stricmp(fn.c_str() + len - 4, ".rar") != 0) return false;
    size_t pos = len - 4, numEnd = pos;
    while (pos > 0 && isdigit((unsigned char)fn[pos - 1])) pos--;
    digits = (int)(numEnd - pos);
    if (digits == 0 || pos < 5 || _strnicmp(fn.c_str() + pos - 5, ".part", 5) != 0) return false;
    startNum = atoi(fn.substr(pos, digits).c_str());
    prefix = fn.substr(0, pos);
    return startNum == 1;
}

bool IsSupportedArchiveFile(const char *fn) {
    if (!fn) return false;
    std::string s(fn); size_t len = s.size(); std::string pfx; int dig = 0, st = 0;
    if (HasPartRarSuffix(s, pfx, dig, st)) return true;
    if (len > 4 && (_stricmp(fn + len - 4, ".zip") == 0 || _stricmp(fn + len - 4, ".z01") == 0)) return true;
    if (len > 4 && _stricmp(fn + len - 4, ".rar") == 0) {
        std::string lo = s; for (size_t i = 0; i < lo.size(); ++i) lo[i] = (char)tolower((unsigned char)lo[i]);
        return lo.find(".part") == std::string::npos;
    }
    if (len > 3 && _stricmp(fn + len - 3, ".7z") == 0) return true;
    if (len > 8 && (_stricmp(fn + len - 8, ".zip.001") == 0 || _stricmp(fn + len - 8, ".rar.001") == 0)) return true;
    if (len > 7 && _stricmp(fn + len - 7, ".7z.001") == 0) return true;
    return false;
}

class MultiVolStream {
public:
    struct Vol { FILE *fp; uint64_t start, len; };
    std::vector<Vol> vols; uint64_t total, pos;
    MultiVolStream() : total(0), pos(0) {}
    ~MultiVolStream() { for (size_t i = 0; i < vols.size(); ++i) if (vols[i].fp) fclose(vols[i].fp); }
    void AddFile(const std::string &path) {
        FILE *f = fopen(path.c_str(), "rb"); if (!f) return;
        _fseeki64(f, 0, SEEK_END); uint64_t sz = (uint64_t)_ftelli64(f); _fseeki64(f, 0, SEEK_SET);
        if (sz > 0) { Vol v = { f, total, sz }; vols.push_back(v); total += sz; } else fclose(f);
    }
    bool OpenSeries(const std::string &firstPath) {
        size_t len = firstPath.size(); std::string pfx; int dig = 0, st = 0;
        if (HasPartRarSuffix(firstPath, pfx, dig, st)) {
            for (int i = 1; i <= 999; ++i) {
                std::vector<char> p(512, 0);
                if (dig == 3) _snprintf(&p.front(), 512, "%s%03d.rar", pfx.c_str(), i);
                else if (dig == 2) _snprintf(&p.front(), 512, "%s%02d.rar", pfx.c_str(), i);
                else _snprintf(&p.front(), 512, "%s%d.rar", pfx.c_str(), i);
                if (GetFileAttributesA(&p.front()) == INVALID_FILE_ATTRIBUTES) break;
                AddFile(&p.front());
            }
        } else if (len > 4 && _stricmp(firstPath.c_str() + len - 4, ".z01") == 0) {
            std::string base = firstPath.substr(0, len - 4);
            for (int i = 1; i <= 99; ++i) {
                std::vector<char> p(512, 0); _snprintf(&p.front(), 512, "%s.z%02d", base.c_str(), i);
                if (GetFileAttributesA(&p.front()) == INVALID_FILE_ATTRIBUTES) break;
                AddFile(&p.front());
            }
            AddFile(base + ".zip");
        } else if (len > 4 && _stricmp(firstPath.c_str() + len - 4, ".001") == 0) {
            std::string base = firstPath.substr(0, len - 3);
            for (int i = 1; i <= 999; ++i) {
                std::vector<char> p(512, 0); _snprintf(&p.front(), 512, "%s%03d", base.c_str(), i);
                if (GetFileAttributesA(&p.front()) == INVALID_FILE_ATTRIBUTES) break;
                AddFile(&p.front());
            }
        } else if (len > 4 && _stricmp(firstPath.c_str() + len - 4, ".rar") == 0) {
            AddFile(firstPath); std::string base = firstPath.substr(0, len - 4);
            for (int i = 0; i <= 99; ++i) {
                std::vector<char> p(512, 0); _snprintf(&p.front(), 512, "%s.r%02d", base.c_str(), i);
                if (GetFileAttributesA(&p.front()) == INVALID_FILE_ATTRIBUTES) break;
                AddFile(&p.front());
            }
        } else { AddFile(firstPath); }
        return !vols.empty() && total > 0;
    }
    size_t ReadAt(uint64_t off, void *dst, size_t bytes) {
        if (off >= total || bytes == 0) return 0;
        if (off + bytes > total) bytes = (size_t)(total - off);
        uint8_t *out = (uint8_t *)dst; size_t got = 0;
        for (size_t i = 0; i < vols.size() && got < bytes; ++i) {
            if (off >= vols[i].start + vols[i].len) continue;
            uint64_t rel = off - vols[i].start;
            size_t chunk = (size_t)std::min<uint64_t>((uint64_t)(bytes - got), vols[i].len - rel);
            _fseeki64(vols[i].fp, (long long)rel, SEEK_SET);
            size_t r = fread(out + got, 1, chunk, vols[i].fp);
            got += r; off += r; if (r < chunk) break;
        }
        pos = off; return got;
    }
};

struct LzmaVolAdapter { ISeekInStream vt; MultiVolStream *mv; };
static SRes LzmaRead(const ISeekInStream *p, void *buf, size_t *size) { LzmaVolAdapter *a = (LzmaVolAdapter *)p; *size = a->mv->ReadAt(a->mv->pos, buf, *size); return SZ_OK; }
static SRes LzmaSeek(const ISeekInStream *p, Int64 *pos, ESzSeek origin) {
    LzmaVolAdapter *a = (LzmaVolAdapter *)p; uint64_t np = a->mv->pos;
    if (origin == SZ_SEEK_SET) np = (uint64_t)(*pos);
    else if (origin == SZ_SEEK_CUR) np = (uint64_t)((int64_t)np + *pos);
    else if (origin == SZ_SEEK_END) np = (uint64_t)((int64_t)a->mv->total + *pos);
    a->mv->pos = np; *pos = (Int64)np; return SZ_OK;
}

static bool WriteSplitOutput(const std::string &fullDst, const uint8_t *data, uint64_t sz) {
    std::string dir = fullDst.substr(0, fullDst.find_last_of("\\/")); EnsureDirTree(dir);
    if (sz <= FATX_SPLIT_LIMIT) { FILE *f = fopen(fullDst.c_str(), "wb"); if (!f) return false; size_t w = fwrite(data, 1, (size_t)sz, f); fclose(f); return w == (size_t)sz; }
    uint64_t off = 0; int part = 1;
    while (off < sz) {
        std::vector<char> p(512, 0); _snprintf(&p.front(), 512, "%s.%03d", fullDst.c_str(), part++);
        size_t chunk = (size_t)std::min<uint64_t>(sz - off, FATX_SPLIT_LIMIT);
        FILE *f = fopen(&p.front(), "wb"); if (!f) return false; size_t w = fwrite(data + off, 1, chunk, f); fclose(f); if (w != chunk) return false;
        off += chunk;
    }
    return true;
}

bool ExtractArchiveToDir(const char *archivePath, const char *destDir, ArcLogFn logFn) {
    EnsureDirTree(destDir);
    MultiVolStream mv;
    if (!mv.OpenSeries(archivePath)) return false;
    std::vector<uint8_t> sig(8, 0); mv.ReadAt(0, &sig.front(), 8); mv.pos = 0;

    // 1. 7z / .7z.001 via public-domain Igor Pavlov LZMA SDK
    if (memcmp(&sig.front(), "7z\xBC\xAF", 4) == 0) {
        CrcGenerateTable();
        LzmaVolAdapter ad; ad.vt.Read = LzmaRead; ad.vt.Seek = LzmaSeek; ad.mv = &mv;
        CLookToRead2 look; LookToRead2_CreateVTable(&look, False);
        look.realStream = &ad.vt; look.bufSize = 262144; look.buf = (Byte *)malloc(look.bufSize);
        LookToRead2_INIT(&look);
        CSzArEx db; SzArEx_Init(&db);
        ISzAlloc allocImp = { SzAlloc, SzFree }, allocTemp = { SzAllocTemp, SzFreeTemp };
        if (SzArEx_Open(&db, &look.vt, &allocImp, &allocTemp) != SZ_OK) { free(look.buf); SzArEx_Free(&db, &allocImp); return false; }
        UInt32 blockIndex = 0xFFFFFFFFU; Byte *outBuf = NULL; size_t outBufSize = 0;
        for (UInt32 i = 0; i < db.NumFiles; ++i) {
            size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
            std::vector<UInt16> wname(len + 1, 0); SzArEx_GetFileNameUtf16(&db, i, &wname.front());
            std::string rel; for (size_t k = 0; k < len && wname[k]; ++k) rel += (wname[k] == '/' ? '\\' : (char)(wname[k] & 0xFF));
            std::string dst = std::string(destDir) + "\\" + rel;
            if (SzArEx_IsDir(&db, i)) { EnsureDirTree(dst); continue; }
            if (logFn) logFn("7z Extracting [%u/%u]: %s\n", i + 1, db.NumFiles, rel.c_str());
            size_t offset = 0, outSizeProcessed = 0;
            if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuf, &outBufSize, &offset, &outSizeProcessed, &allocImp, &allocTemp) != SZ_OK || !WriteSplitOutput(dst, outBuf + offset, outSizeProcessed)) {
                IAlloc_Free(&allocImp, outBuf); SzArEx_Free(&db, &allocImp); free(look.buf); return false;
            }
        }
        IAlloc_Free(&allocImp, outBuf); SzArEx_Free(&db, &allocImp); free(look.buf); return true;
    }

        // 2. Multi-part or single ZIP (Store method 0 & Deflate method 8 via miniz)
    if (memcmp(&sig.front(), "PK\x03\x04", 4) == 0) {
        uint64_t cur = 0; int count = 0;
        while (cur + 30 <= mv.total) {
            std::vector<uint8_t> lh(30, 0); mv.ReadAt(cur, &lh.front(), 30);
            if (GetLE32(&lh.front()) != 0x04034B50U) break;
            uint16_t flags = GetLE16(&lh.front() + 6), method = GetLE16(&lh.front() + 8);
            uint64_t compSz = GetLE32(&lh.front() + 18), uncompSz = GetLE32(&lh.front() + 22);
            uint16_t nlen = GetLE16(&lh.front() + 26), xlen = GetLE16(&lh.front() + 28);
            std::vector<char> nm(nlen + 1, 0); mv.ReadAt(cur + 30, &nm.front(), nlen);
            if (xlen > 0) {
                std::vector<uint8_t> ex(xlen, 0); mv.ReadAt(cur + 30 + nlen, &ex.front(), xlen);
                for (size_t p = 0; p + 4 <= xlen; ) {
                    uint16_t hid = GetLE16(&ex.front() + p), hsz = GetLE16(&ex.front() + p + 2);
                    if (hid == 0x0001 && hsz >= 16 && p + 4 + 16 <= xlen) { uncompSz = GetLE64(&ex.front() + p + 4); compSz = GetLE64(&ex.front() + p + 12); }
                    p += 4 + hsz;
                }
            }
            cur += 30 + nlen + xlen;
            std::string rel = &nm.front(); for (size_t k = 0; k < rel.size(); ++k) if (rel[k] == '/') rel[k] = '\\';
            std::string dst = std::string(destDir) + "\\" + rel;
            if (!rel.empty() && rel[rel.size() - 1] == '\\') { EnsureDirTree(dst); continue; }
            if (method != 0 && method != 8) { if (logFn) logFn("\x8D" "ZIP Error: Unsupported method %u\x9D" "\n", method); return false; }
            uint64_t finalSz = (uncompSz > 0) ? uncompSz : compSz;
            if (logFn) logFn("ZIP Extracting #%d (%s): %s (%.1f MB)\n", ++count, method == 8 ? "Deflate" : "Store", rel.c_str(), finalSz / (1024.0 * 1024.0));
            EnsureDirTree(dst.substr(0, dst.find_last_of("\\/")));
            uint64_t rem = compSz, outPartBytes = 0; int splitIdx = 1; FILE *outFp = NULL;
            std::vector<uint8_t> inBuf(262144), outBuf(262144);
            mz_stream zst; memset(&zst, 0, sizeof(zst));
            if (method == 8 && mz_inflateInit2(&zst, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
            while (rem > 0) {
                size_t chunk = (size_t)std::min<uint64_t>(rem, inBuf.size());
                size_t r = mv.ReadAt(cur, &inBuf.front(), chunk); if (r == 0) { if (outFp) fclose(outFp); if (method == 8) mz_inflateEnd(&zst); return false; }
                cur += r; rem -= r;
                if (method == 0) {
                    size_t wOff = 0;
                    while (wOff < r) {
                        if (!outFp || (finalSz > FATX_SPLIT_LIMIT && outPartBytes >= FATX_SPLIT_LIMIT)) {
                            if (outFp) fclose(outFp);
                            if (finalSz <= FATX_SPLIT_LIMIT) outFp = fopen(dst.c_str(), "wb");
                            else { std::vector<char> sp(512, 0); _snprintf(&sp.front(), 512, "%s.%03d", dst.c_str(), splitIdx++); outFp = fopen(&sp.front(), "wb"); }
                            if (!outFp) return false; outPartBytes = 0;
                        }
                        size_t take = r - wOff;
                        if (finalSz > FATX_SPLIT_LIMIT && outPartBytes + take > FATX_SPLIT_LIMIT) take = (size_t)(FATX_SPLIT_LIMIT - outPartBytes);
                        fwrite(&inBuf.front() + wOff, 1, take, outFp); wOff += take; outPartBytes += take;
                    }
                } else {
                    zst.next_in = &inBuf.front(); zst.avail_in = (unsigned int)r;
                    while (zst.avail_in > 0) {
                        zst.next_out = &outBuf.front(); zst.avail_out = (unsigned int)outBuf.size();
                        int zrc = mz_inflate(&zst, MZ_NO_FLUSH);
                        size_t produced = outBuf.size() - zst.avail_out; size_t wOff = 0;
                        while (wOff < produced) {
                            if (!outFp || (finalSz > FATX_SPLIT_LIMIT && outPartBytes >= FATX_SPLIT_LIMIT)) {
                                if (outFp) fclose(outFp);
                                if (finalSz <= FATX_SPLIT_LIMIT) outFp = fopen(dst.c_str(), "wb");
                                else { std::vector<char> sp(512, 0); _snprintf(&sp.front(), 512, "%s.%03d", dst.c_str(), splitIdx++); outFp = fopen(&sp.front(), "wb"); }
                                if (!outFp) { mz_inflateEnd(&zst); return false; } outPartBytes = 0;
                            }
                            size_t take = produced - wOff;
                            if (finalSz > FATX_SPLIT_LIMIT && outPartBytes + take > FATX_SPLIT_LIMIT) take = (size_t)(FATX_SPLIT_LIMIT - outPartBytes);
                            fwrite(&outBuf.front() + wOff, 1, take, outFp); wOff += take; outPartBytes += take;
                        }
                        if (zrc == MZ_STREAM_END) { rem = 0; break; }
                        if (zrc != MZ_OK) { if (outFp) fclose(outFp); mz_inflateEnd(&zst); return false; }
                    }
                }
            }
            if (method == 8) mz_inflateEnd(&zst);
            if (outFp) fclose(outFp);
            if ((flags & 0x0008) && cur + 16 <= mv.total) {
                std::vector<uint8_t> dd(4, 0); mv.ReadAt(cur, &dd.front(), 4);
                if (GetLE32(&dd.front()) == 0x08074B50U) cur += (finalSz > 0xFFFFFFFFULL ? 24 : 16);
            }
        }
        return count > 0;
    }

    // 3. Multi-part or single RAR4 & RAR5
    if (memcmp(&sig.front(), "Rar!\x1A\x07", 6) == 0) {
        bool isRar5 = (*(sig.begin() + 6) == 0x01);
        int count = 0;
        std::string curFile; uint64_t curUnpSz = 0, outPartBytes = 0; int splitIdx = 1; FILE *outFp = NULL;
        std::vector<uint8_t> buf(262144);
        for (size_t vIdx = 0; vIdx < mv.vols.size(); ++vIdx) {
            uint64_t vBase = mv.vols[vIdx].start, vEnd = vBase + mv.vols[vIdx].len;
            uint64_t cur = vBase + (isRar5 ? 8 : 7);
            while (cur + 7 <= vEnd) {
                if (!isRar5) {
                    std::vector<uint8_t> rh(7, 0); mv.ReadAt(cur, &rh.front(), 7);
                    uint8_t hType = *(rh.begin() + 2); uint16_t hFlags = GetLE16(&rh.front() + 3); uint16_t hSize = GetLE16(&rh.front() + 5);
                    if (hSize < 7 || cur + hSize > vEnd) break;
                    uint64_t addSz = 0;
                    if (hFlags & 0x8000) { std::vector<uint8_t> ab(4, 0); mv.ReadAt(cur + 7, &ab.front(), 4); addSz = GetLE32(&ab.front()); }
                    if (hType == 0x74 && hSize >= 32) {
                        std::vector<uint8_t> fh(hSize, 0); mv.ReadAt(cur, &fh.front(), hSize);
                        uint64_t packSz = GetLE32(&fh.front() + 7), unpSz = GetLE32(&fh.front() + 11);
                        uint8_t method = *(fh.begin() + 25); uint16_t nameSz = GetLE16(&fh.front() + 26); uint32_t attr = GetLE32(&fh.front() + 28);
                        size_t nameOff = 32;
                        if (hFlags & 0x0100) {
                            if (hSize < 40) break;
                            packSz |= ((uint64_t)GetLE32(&fh.front() + 32) << 32);
                            unpSz |= ((uint64_t)GetLE32(&fh.front() + 36) << 32);
                            nameOff = 40;
                        }
                        if (method != 0x30) { if (outFp) fclose(outFp); if (logFn) logFn("\x8D" "RAR Error: Compressed RAR (0x%02X); re-pack as Store RAR or 7z/ZIP\x9D" "\n", method); return false; }
                        std::string rel;
                        for (size_t k = 0; k < nameSz && nameOff + k < hSize && fh[nameOff + k]; ++k) rel += (fh[nameOff + k] == '/' ? '\\' : (char)fh[nameOff + k]);
                        std::string dst = std::string(destDir) + "\\" + rel;
                        if ((hFlags & 0x00E0) == 0x00E0 || (attr & 0x10)) { EnsureDirTree(dst); }
                        else {
                            if (!(hFlags & 0x0001)) {
                                if (outFp) { fclose(outFp); outFp = NULL; }
                                curFile = dst; curUnpSz = unpSz; outPartBytes = 0; splitIdx = 1;
                                if (logFn) logFn("RAR Extracting #%d: %s (%.1f MB)\n", ++count, rel.c_str(), unpSz / (1024.0 * 1024.0));
                                EnsureDirTree(dst.substr(0, dst.find_last_of("\\/")));
                            }
                            uint64_t dataPos = cur + hSize, rem = packSz;
                            while (rem > 0) {
                                if (!outFp || (curUnpSz > FATX_SPLIT_LIMIT && outPartBytes >= FATX_SPLIT_LIMIT)) {
                                    if (outFp) fclose(outFp);
                                    if (curUnpSz <= FATX_SPLIT_LIMIT) outFp = fopen(curFile.c_str(), "wb");
                                    else { std::vector<char> sp(512, 0); _snprintf(&sp.front(), 512, "%s.%03d", curFile.c_str(), splitIdx++); outFp = fopen(&sp.front(), "wb"); }
                                    if (!outFp) return false; outPartBytes = 0;
                                }
                                size_t chunk = (size_t)std::min<uint64_t>(rem, buf.size());
                                if (curUnpSz > FATX_SPLIT_LIMIT && outPartBytes + chunk > FATX_SPLIT_LIMIT) chunk = (size_t)(FATX_SPLIT_LIMIT - outPartBytes);
                                size_t r = mv.ReadAt(dataPos, &buf.front(), chunk); if (r == 0) { fclose(outFp); return false; }
                                fwrite(&buf.front(), 1, r, outFp); dataPos += r; rem -= r; outPartBytes += r;
                            }
                            if (!(hFlags & 0x0002) && outFp) { fclose(outFp); outFp = NULL; }
                        }
                        cur += hSize + packSz; continue;
                    }
                    if (hType == 0x7B) break;
                    cur += hSize + addSz;
                } else {
                    std::vector<uint8_t> rh(16, 0); mv.ReadAt(cur, &rh.front(), 16);
                    size_t p = 4; uint64_t hSize = 0, hType = 0, hFlags = 0, extraSz = 0, dataSz = 0;
                    for (int s = 0; p < 16; s += 7) { uint8_t b = rh[p++]; hSize |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                    size_t hdrStart = p; if (hSize < 2 || cur + hdrStart + hSize > vEnd) break;
                    std::vector<uint8_t> fh((size_t)(hdrStart + hSize), 0); mv.ReadAt(cur, &fh.front(), fh.size());
                    p = hdrStart;
                    for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; hType |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                    for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; hFlags |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                    if (hFlags & 0x0001) { for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; extraSz |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; } }
                    if (hFlags & 0x0002) { for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; dataSz |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; } }
                    if (hType == 2) {
                        uint64_t fFlags = 0, unpSz = 0, attr = 0, compInfo = 0, hostOs = 0, nameLen = 0;
                        for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; fFlags |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                        for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; unpSz |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                        for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; attr |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                        if (fFlags & 0x0002) p += 4;
                        if (fFlags & 0x0004) p += 4;
                        for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; compInfo |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                        for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; hostOs |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                        for (int s = 0; p < fh.size(); s += 7) { uint8_t b = fh[p++]; nameLen |= ((uint64_t)(b & 0x7F) << s); if (!(b & 0x80)) break; }
                        uint32_t method = (uint32_t)((compInfo >> 7) & 7);
                        if (method != 0) { if (outFp) fclose(outFp); if (logFn) logFn("\x8D" "RAR5 Error: Compressed RAR5; re-pack as Store RAR or 7z/ZIP\x9D" "\n"); return false; }
                        std::string rel;
                        for (size_t k = 0; k < nameLen && p + k < fh.size() && fh[p + k]; ++k) rel += (fh[p + k] == '/' ? '\\' : (char)fh[p + k]);
                        std::string dst = std::string(destDir) + "\\" + rel;
                        if (fFlags & 0x0001) { EnsureDirTree(dst); }
                        else {
                            if (!(hFlags & 0x0008)) {
                                if (outFp) { fclose(outFp); outFp = NULL; }
                                curFile = dst; curUnpSz = unpSz; outPartBytes = 0; splitIdx = 1;
                                if (logFn) logFn("RAR5 Extracting #%d: %s (%.1f MB)\n", ++count, rel.c_str(), unpSz / (1024.0 * 1024.0));
                                EnsureDirTree(dst.substr(0, dst.find_last_of("\\/")));
                            }
                            uint64_t dataPos = cur + hdrStart + hSize, rem = dataSz;
                            while (rem > 0) {
                                if (!outFp || (curUnpSz > FATX_SPLIT_LIMIT && outPartBytes >= FATX_SPLIT_LIMIT)) {
                                    if (outFp) fclose(outFp);
                                    if (curUnpSz <= FATX_SPLIT_LIMIT) outFp = fopen(curFile.c_str(), "wb");
                                    else { std::vector<char> sp(512, 0); _snprintf(&sp.front(), 512, "%s.%03d", curFile.c_str(), splitIdx++); outFp = fopen(&sp.front(), "wb"); }
                                    if (!outFp) return false; outPartBytes = 0;
                                }
                                size_t chunk = (size_t)std::min<uint64_t>(rem, buf.size());
                                if (curUnpSz > FATX_SPLIT_LIMIT && outPartBytes + chunk > FATX_SPLIT_LIMIT) chunk = (size_t)(FATX_SPLIT_LIMIT - outPartBytes);
                                size_t r = mv.ReadAt(dataPos, &buf.front(), chunk); if (r == 0) { fclose(outFp); return false; }
                                fwrite(&buf.front(), 1, r, outFp); dataPos += r; rem -= r; outPartBytes += r;
                            }
                            if (!(hFlags & 0x0010) && outFp) { fclose(outFp); outFp = NULL; }
                        }
                    }
                    if (hType == 5) break;
                    cur += hdrStart + hSize + dataSz;
                }
            }
        }
        if (outFp) fclose(outFp);
        return count > 0;
    }
    if (logFn) logFn("\x8D" "Archive Error: Unrecognized archive signature\x9D" "\n");
    return false;
}




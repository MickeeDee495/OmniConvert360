#ifndef GOD_ENGINE_H
#define GOD_ENGINE_H
typedef void (*GodLogCallback)(const char *fmt, ...);
enum OutputFormatMode { OUT_MODE_GOD = 0, OUT_MODE_FOLDER = 1, OUT_MODE_ISO = 2 };
void ForceCreateDirectoryTree(const char *path);
bool DeletePathRecursive(const char *path);
int ConvertToGodStandalone(const char *sourcePath, const char *contentRoot, const char *fallbackTitle, GodLogCallback logFn);
int PatchGodStorageIcon(const char *hdrPath, GodLogCallback logFn);
int ConvertUniversal(const char *sourcePath, const char *driveRoot, int outMode, const char *gamesDirName, const char *xgamesDirName, const char *isosDirName, const char *fallbackTitle, GodLogCallback logFn);
#endif


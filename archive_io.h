#ifndef OMNICONVERT_ARCHIVE_IO_H
#define OMNICONVERT_ARCHIVE_IO_H

typedef void (*ArcLogFn)(const char *fmt, ...);

bool MountOpticalDrive();
bool IsSupportedArchiveFile(const char *fileName);
bool ExtractArchiveToDir(const char *archivePath, const char *destDir, ArcLogFn logFn);
bool CreateMultipartZipFromPath(const char *sourcePath, const char *destDir, const char *baseName, ArcLogFn logFn);

#endif

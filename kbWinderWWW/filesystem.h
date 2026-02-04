#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <LittleFS.h>
const char *fsName = "LittleFS";
FS *fileSystem = &LittleFS;
LittleFSConfig fileSystemConfig = LittleFSConfig();
static bool fileSystemInitialized;

extern bool isSystemLocked();
bool copyFile(const String &sourcePath, const String &destPath);
void rotateAndCreateBackup(const char *filename);

#endif
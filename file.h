/*
 * Fade To Black engine rewrite
 * Copyright (C) 2006-2012 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef FILE_H__
#define FILE_H__

#include "util.h"

enum FileType {
	kFileType_DATA = 0,
	kFileType_SOUND,
	kFileType_TEXT,
	kFileType_VOICE,
	kFileType_RUNTIME,
};

enum FileLanguage {
	kFileLanguage_EN = 0,
	kFileLanguage_FR,
	kFileLanguage_GR,
	kFileLanguage_SP,
	kFileLanguage_IT,
};

enum FilePosition {
	kFilePosition_CUR = 0,
	kFilePosition_SET
};

extern bool g_isDemo;

struct FILEWHOLE {
	uint32_t pos, eof;
	uint8_t *data;
};

bool fileInit(int language, int voice, const char *dataPath);
bool fileExists(const char *fileName, int fileType);
FILE *fileOpen(const char *fileName, int *fileSize, int fileType, bool errorIfNotFound = true);
void fileClose(FILE *fp);
void fileRead(FILE *fp, void *buf, int size);
uint8_t fileReadByte(FILE *fp);
uint16_t fileReadUint16LE(FILE *fp);
uint32_t fileReadUint32LE(FILE *fp);
uint32_t fileGetPos(FILE *fp);
void fileSetPos(FILE *fp, uint32_t pos, int origin);
int fileEof(FILE *fp);

#endif // __FILE_H__

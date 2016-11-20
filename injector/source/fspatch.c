#include <3ds.h>
#include "fspatch.h"
#include "memory.h"
#include "strings.h"
#include "ifile.h"
#include "fsldr.h"
#include "fsredir_bin.h"

#define BRANCH(src,dst)    (0xEA000000 | ((u32)((((u8 *)(dst) - (u8 *)(src)) >> 2) - 2) & 0xFFFFFF))
#define BRANCH_L(src,dst)  (0xEB000000 | ((u32)((((u8 *)(dst) - (u8 *)(src)) >> 2) - 2) & 0xFFFFFF))

/*
Result openLumaFile(IFile *file, const char *path); // Externally implemented
Result fileOpen(IFile *file, FS_ArchiveID archiveId, const char *path, int flags);

int dumpTitleCodeSection(u64 progId, u8 *code, u32 size)
{
   
    char path[] = "/luma/code_sections/0000000000000000.bin";
    progIdToStr(path + 35, progId);

    IFile file;

    if(R_FAILED(fileOpen(&file, ARCHIVE_SDMC, path, 6))) return true;

    u64 tmp;

    FSFILE_Write(file.handle, &tmp, 0, code, size, 0);

    IFile_Close(&file);

    return 0;
}
*/

u32 findNearestStmfd(u8* code, u32 pos)
{
	if(code && pos)
	{
		while((u32)pos > 0)
		{
			if((*((u32*)(code + pos)) & 0xffff0000) == 0xe92d0000) return pos;
			pos -= 4;
		}
	}
	return pos;
}

static u32 findFunctionCommand(u8* code, u32 size, u32 command)
{
	u32 func = 0;
	for(u32 i = 0; i < size && !func; i += 4)
	{
		if(*((u32*)(code + i)) == command) func = i;
	}
	return findNearestStmfd(code, func);
}

static u32 findThrowFatalError(u8* code, u32 size)
{
	u32 func = 0;
	u32 connectToPort = 0;
	for(u32 i = 0; i < size && !connectToPort; i += 4)
	{
		if(*((u32*)(code + i)) == 0xef00002d) connectToPort = i - 4;
	}
	if(connectToPort)
	{
		for(u32 i = 0; i < size && !func; i += 4)
		{	
			if(*((u32*)(code + i)) == BRANCH_L(i, connectToPort))
			{
				u32 pos = findNearestStmfd(code, i);
				func = pos;
				pos += 4;
				while((*((u32*)(code + pos)) & 0xffff0000) != 0xe92d0000)
				{
					if(*((u32*)(code + pos)) == 0xe200167e) func = 0;
					pos += 4;
				}
			}
		}
	}
	return func;
}

static u32 findIFileOpen(u8* code, u32 size)
{
	u32 func = 0;
	for(u32 i = 0; i < size && !func; i += 4)
	{
		if((*((u32*)(code + i + 0x0)) == 0xe1a06000 && 
			*((u32*)(code + i + 0x4)) == 0xe24dd010 && 
			*((u32*)(code + i + 0x8)) == 0xe1a07002 && 
			*((u32*)(code + i + 0xc)) == 0xe1a08001) ||
		   (*((u32*)(code + i + 0x0)) == 0xe1a05000 && 
			*((u32*)(code + i + 0x4)) == 0xe24dd010 && 
			*((u32*)(code + i + 0x8)) == 0xe1a04001 && 
			*((u32*)(code + i + 0xc)) == 0xe1a00001))
		{
			func = findNearestStmfd(code, i);
		}
	}
	return func;
}

static u32 findFsMountRom(u8* code, u32 size)
{
	u32 func = 0;
	for(u32 i = 0; i < size && !func; i += 4)
	{	
		if(!memcmp((void*)(code + i), (void*)(u8[]){0x0C, 0x00, 0x9D, 0xE5, 0x00, 0x10, 0x90, 0xE5, 0x28, 0x10, 0x91, 0xE5, 0x31, 0xFF, 0x2F, 0xE1}, 16) ||
		   !memcmp((void*)(code + i), (void*)(u8[]){0x31, 0xFF, 0x2F, 0xE1, 0x04, 0x00, 0xA0, 0xE1, 0x0F, 0x10, 0xA0, 0xE1, 0xA4, 0x2F, 0xB0, 0xE1}, 16))
			func = findNearestStmfd(code, i);
	}
	return func;
}

static u32 findFsMountArchive(u8* code, u32 size)
{
	u32 fsMountSave = findFunctionCommand(code, size, 0xC92044E7);
	u32 func = 0;

	/* fsMountArchive is the first function call in fsMountSave */
	for(u32 i = fsMountSave; i < fsMountSave + 0x100 && !func; i += 4)
	{
		u32 opcode = *((u32*)(code + i));
		if((opcode & 0xff000000) >> 24 == 0xeb) func = ((opcode & 0x00FFFFFF) << 2) + i + 8;
	}

	return func;
}

void patchLayeredFs(u64 progId, u8* code, u32 size)
{
	/* This only supports games and apps */
	if((u32)((progId & 0xFFFFFFF000000000LL) >> 0x24) != 0x0004000) return;

	/* Locate symbols */
	u32 binSpace = findThrowFatalError(code, size);
	u32 strSpace = findFunctionCommand(code, size, 0xe3a00b42);
	u32 iFileOpen = findIFileOpen(code, size);
	u32 fsMountRom = findFsMountRom(code, size);
	u32 fsRegisterArchive = findFunctionCommand(code, size, 0xC82044B4);
	u32 fsMountArchive = findFsMountArchive(code, size);

	/* Inject the payload just if we have enough symbols */
	if(fsMountRom && fsMountArchive && fsRegisterArchive && binSpace && strSpace)
	{ 
		/* Copy payload and string in the free space */
		memcpy((void*)(code + binSpace), (void*)fsredir_bin, fsredir_bin_size);
		memcpy((void*)(code + strSpace), (void*)"YS:/luma/titles/0000000000000000/romfs", 39);
		progIdToStr((char*)(code + strSpace + 31), progId);

		/* Insert symbols in the payload */
		u32* symbols = (u32*)(code + binSpace + fsredir_bin_size);
		while(*(symbols - 1) != 0xdeadbeef) symbols--;
		*symbols++ = 0x100000 + strSpace;
		*symbols++ = 0x100000 + fsMountArchive;
		*symbols++ = 0x100000 + fsRegisterArchive;
		*symbols++ = 0x100000 + iFileOpen;

		/* Place hooks in order to redirect code-flow */
		*((u32*)(code + binSpace + 4))  = *((u32*)(code + fsMountRom));
		*((u32*)(code + binSpace + 8))  = BRANCH(binSpace + 8, fsMountRom + 4);
		*((u32*)(code + fsMountRom))    = BRANCH(fsMountRom, binSpace);
		*((u32*)(code + binSpace + 16)) = *((u32*)(code + iFileOpen));
		*((u32*)(code + binSpace + 20)) = BRANCH(binSpace + 20, iFileOpen + 4);
		*((u32*)(code + iFileOpen))     = BRANCH(iFileOpen, binSpace + 12);
	}
}

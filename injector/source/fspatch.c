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
		if(!memcmp((void*)(code + i), (void*)(u8[]){0x00, 0x60, 0xA0, 0xE1, 0x14, 0xD0, 0x4D, 0xE2, 0x01, 0x40, 0xA0, 0xE1, 0x02, 0x70, 0xA0, 0xE1}, 16) ||
		   !memcmp((void*)(code + i), (void*)(u8[]){0x00, 0x60, 0xA0, 0xE1, 0x10, 0xD0, 0x4D, 0xE2, 0x02, 0x70, 0xA0, 0xE1, 0x01, 0x80, 0xA0, 0xE1}, 16) ||
		   !memcmp((void*)(code + i), (void*)(u8[]){0x00, 0x50, 0xA0, 0xE1, 0x10, 0xD0, 0x4D, 0xE2, 0x01, 0x40, 0xA0, 0xE1, 0x01, 0x00, 0xA0, 0xE1}, 16)) 
			func = findNearestStmfd(code, i);
		
	}
	return func;
}

static u32 findFsMountRom(u8* code, u32 size)
{
	u32 func = 0;
	for(u32 i = 0; i < size && !func; i += 4)
	{	
		if(!memcmp((void*)(code + i), (void*)(u8[]){0x0C, 0x00, 0x9D, 0xE5, 0x00, 0x10, 0x90, 0xE5, 0x28, 0x10, 0x91, 0xE5, 0x31, 0xFF, 0x2F, 0xE1}, 16) ||
		   !memcmp((void*)(code + i), (void*)(u8[]){0x08, 0x00, 0x9D, 0xE5, 0x00, 0x10, 0x90, 0xE5, 0x30, 0x10, 0x91, 0xE5, 0x31, 0xFF, 0x2F, 0xE1}, 16) ||
		   !memcmp((void*)(code + i), (void*)(u8[]){0x31, 0xFF, 0x2F, 0xE1, 0x04, 0x00, 0xA0, 0xE1, 0x0F, 0x10, 0xA0, 0xE1, 0xA4, 0x2F, 0xB0, 0xE1}, 16))
			func = findNearestStmfd(code, i);
	}
	return func;
}

static u32 findFsMountArchive(u8* code, u32 size)
{
	u32 func = 0;
	u32 fsMountSave = findFunctionCommand(code, size, 0xC92044E7);
	if(!fsMountSave)
	{
		for(u32 i = 0; i < size && !func; i += 4)
		{	
			if(!memcmp((void*)(code + i), (void*)(u8[]){0x00, 0x00, 0x9D, 0xE5, 0x00, 0x10, 0x90, 0xE5, 0x28, 0x10, 0x91, 0xE5, 0x31, 0xFF, 0x2F, 0xE1}, 16))
				func = findNearestStmfd(code, i);
		}
	}

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
	if(iFileOpen && fsMountRom && fsMountArchive && fsRegisterArchive && binSpace && strSpace)
	{ 
		/* Check if sdcard is already mounted by the app */
		u32 sdIsMounted = 0;
		if(memsearch(code, (const void*)"sdmc:", size, 6)) sdIsMounted = 1;

		/* Copy payload and string in the free space */
		memcpy((void*)(code + binSpace), (void*)fsredir_bin, fsredir_bin_size);
		memcpy((void*)(code + strSpace), (void*)"sdmc:/luma/titles/0000000000000000/romfs", 41);
		progIdToStr((char*)(code + strSpace + 33), progId);

		/* Insert symbols in the payload */
		u32* code32 = (u32*)(code + binSpace);
		for(u32 i = 0; i < fsredir_bin_size/4; i++)
		{
			if(code32[i] == 0xdead0000) code32[i] = *((u32*)(code + fsMountRom));
			if(code32[i] == 0xdead0001) code32[i] = BRANCH(binSpace + i*4, fsMountRom + 4);
			if(code32[i] == 0xdead0002) code32[i] = *((u32*)(code + iFileOpen));
			if(code32[i] == 0xdead0003) code32[i] = BRANCH(binSpace + i*4, iFileOpen + 4);
			if(code32[i] == 0xdead0004) code32[i] = 0x100000 + strSpace;
			if(code32[i] == 0xdead0005) code32[i] = 0x100000 + fsMountArchive;
			if(code32[i] == 0xdead0006) code32[i] = 0x100000 + fsRegisterArchive;
		}

		/* Place hooks in order to redirect code-flow */
		if(!sdIsMounted)
		{
			*((u32*)(code + fsMountRom)) = BRANCH(fsMountRom, binSpace);
		}
		*((u32*)(code + iFileOpen))  = BRANCH(iFileOpen, binSpace + 12);
	}
}

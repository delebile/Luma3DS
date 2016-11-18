#include <3ds.h>
#include "fspatch.h"
#include "memory.h"
#include "strings.h"
#include "ifile.h"
#include "fsldr.h"
#include "fsredir_bin.h"
#include "fsredirlayered_bin.h"

#define BRANCH(src,dst)    (0xEA000000 | ((u32)((((u8 *)(dst) - (u8 *)(src)) >> 2) - 2) & 0xFFFFFF))
#define BRANCH_L(src,dst)  (0xEB000000 | ((u32)((((u8 *)(dst) - (u8 *)(src)) >> 2) - 2) & 0xFFFFFF))

Result openLumaFile(IFile *file, const char *path); // Externally implemented
Result fileOpen(IFile *file, FS_ArchiveID archiveId, const char *path, int flags);

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

static u32 findErrDispThrowError(u8* code, u32 size)
{
	u32 func = findFunctionCommand(code, size, 0xe3a00b42);
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

int dumpTitleCodeSection(u64 progId, u8 *code, u32 size)
{
    /* Here we look for "/luma/code_sections/[u64 titleID in hex, uppercase].bin"
       If it exists it should be a decompressed binary code file */

    char path[] = "/luma/code_sections/0000000000000000.bin";
    progIdToStr(path + 35, progId);

    IFile file;

    if(R_FAILED(fileOpen(&file, ARCHIVE_SDMC, path, 6))) return true;

    u64 tmp;

    FSFILE_Write(file.handle, &tmp, 0, code, size, 0);

    IFile_Close(&file);

    return 0;
}

void patchLayeredFs(u64 progId, u8* code, u32 size)
{
	if((u32)((progId & 0xFFFFFFF000000000LL) >> 0x24) != 0x0004000) return;

	//dumpTitleCodeSection(progId, code, size);

	/* Locate symbols */
	u32 stubSpace = findThrowFatalError(code, size);
	u32 stringSpace = findErrDispThrowError(code, size);
	u32 iFileOpen = findIFileOpen(code, size);
	u32 fsMountRom = findFsMountRom(code, size);
	u32 fsMountSave = findFunctionCommand(code, size, 0xC92044E7);
	u32 fsRegisterArchive = findFunctionCommand(code, size, 0xC82044B4);
	u32 fsMountArchive = 0;
	/* fsMountArchive is referenced in fsMountSave */
	for(u32 i = fsMountSave; i < fsMountSave + 0x100 && !fsMountArchive; i += 4)
	{
		u32 opcode = *((u32*)(code + i));
		if((opcode & 0xff000000) >> 24 == 0xeb) fsMountArchive = ((opcode & 0x00FFFFFF) << 2) + i + 8;
	}

	/* Inject the payload just if we have enough symbols */
	u32 hookMount = fsMountRom;
	if(hookMount && fsMountArchive && fsRegisterArchive && stubSpace && stringSpace)
	{ 
		/* Copy payload in the stub space */
		memcpy((void*)(code + stubSpace), (void*)fsredirlayered_bin, fsredirlayered_bin_size);

		/* Craft a custom romfs file-path and inject in a free space */
		memcpy((void*)(code + stringSpace), (void*)"YS:/luma/titles/0000000000000000/romfs", 39);
		progIdToStr((char*)(code + stringSpace + 31), progId);

		/* Insert symbols in the payload */
		u32* symbols = (u32*)(code + stubSpace + fsredirlayered_bin_size);
		while(*(symbols - 1) != 0xdeadbeef) symbols--;
		*symbols++ = 0x100000 + stringSpace;
		*symbols++ = 0x100000 + fsMountArchive;
		*symbols++ = 0x100000 + fsRegisterArchive;
		*symbols++ = 0x100000 + iFileOpen;

		/* Place hooks in order to redirect code-flow */
		*((u32*)(code + stubSpace + 4)) = *((u32*)(code + hookMount));
		*((u32*)(code + stubSpace + 8)) = BRANCH(stubSpace + 8, hookMount + 4);
		*((u32*)(code + hookMount)) = BRANCH(hookMount, stubSpace);
		*((u32*)(code + stubSpace + 16)) = *((u32*)(code + iFileOpen));
		*((u32*)(code + stubSpace + 20)) = BRANCH(stubSpace + 20, iFileOpen + 4);
		*((u32*)(code + iFileOpen)) = BRANCH(iFileOpen, stubSpace + 12);
	}else *(u32*)0 = 0xdeadc0de;
}

void patchFsRedirection(u64 progId, u8* code, u32 size, u8 romfs, u8 save)
{
	u8* myCode = fsredir_bin;
	u32 myCodeSize = fsredir_bin_size;

	/* Grab functions addresses */
	u32 fsOpenFileDirectly = findFunctionCommand(code, size, 0x08030204);
	u32 fsOpenLinkFile = findFunctionCommand(code, size, 0x80C0000);
	u32 throwFatalError = findThrowFatalError(code, size);
	u32 iFileOpen = findIFileOpen(code, size); 
	u32 fsMountSave = findFunctionCommand(code, size, 0xC92044E7);
	u32 fsControlArchive = findFunctionCommand(code, size, 0x080D0144);
	u32 errDispThrowError = findErrDispThrowError(code, size);
	if(!throwFatalError || !errDispThrowError) return;

	/* Setup the custom title-data path in memory */
	char path[] = "data:/luma/titles/0000000000000000/romfs";
	progIdToStr(path + 33, progId);
	memcpy(((void*)(code + errDispThrowError)), (void*)path, sizeof(path));

	/* Setup the payload in memory.
	   Hard-coded offsets becouse i don't have space for an handler... */
	u32 payload = throwFatalError;
	u32 romfsRedirFunction = payload + 0;
	u32 saveRedirFunction = payload + 4;
	memcpy(((void*)(code + payload)), (void*)myCode, myCodeSize);
	*((u32*)(code + payload + 12))  = *((u32*)(code + fsOpenFileDirectly));
	*((u32*)(code + payload + 28)) = *((u32*)(code + iFileOpen));
	u32* payloadSymbols = (u32*)(code + payload + myCodeSize - 36);
	payloadSymbols[0] = 0x100000 + fsOpenFileDirectly + 4;
	payloadSymbols[1] = 0x100000 + iFileOpen + 4;
	payloadSymbols[2] = 0x100000 + errDispThrowError;
	payloadSymbols[3] = sizeof(path) - 5;

	/* RomFS image redirection (HANS-like) */
	if(romfs && fsOpenFileDirectly && payload && 0)
	{
		IFile file;
		u64 romfsSize, romfsOffset;
		if(R_SUCCEEDED(openLumaFile(&file, path + 5)))
    	{
    	    if(R_SUCCEEDED(IFile_GetSize(&file, &romfsSize)))
    	    {
    	        u64 total;
				u32 header;
    	        if(R_SUCCEEDED(IFile_Read(&file, &total, &header, 4)) && total == 4 && header == 0x43465649)
				{
					romfsOffset = 0x1000;
					romfsSize -= 0x1000;
				}

				/* Place all the hooks and data*/
				*((u32*)(code + fsOpenFileDirectly)) = BRANCH(fsOpenFileDirectly, romfsRedirFunction);
				*((u64*)&payloadSymbols[5]) = romfsOffset;
				*((u64*)&payloadSymbols[7]) = romfsSize;
				if(fsOpenLinkFile)
				{
					*((u32*)(code + fsOpenLinkFile)) = 0xE3A03003; // mov r3, #3
					*((u32*)(code + fsOpenLinkFile + 4)) = BRANCH(fsOpenLinkFile + 4, romfsRedirFunction);
				}
    	    }
    	    IFile_Close(&file);
    	}
	}
	
	/* Redirect SaveData file access to an sdcard folder */
	if(save && fsControlArchive && fsMountSave && iFileOpen)
	{
        /* Redirect SaveData is only for game apps */
        if((u32)((progId & 0xFFFFFFF000000000LL) >> 0x24) != 0x0004000) return;
        
        /* Create the title folder */
        path[34] = 0;
        FS_Archive sdmcArchive = (FS_Archive){0x00000009, (FS_Path){PATH_EMPTY, 1, (u8*)""}, 0};
        FSLDR_OpenArchive(&sdmcArchive);
        FSLDR_CreateDirectory(sdmcArchive, (FS_Path){PATH_ASCII, 6, (u8*)"/luma"}, 0);
        FSLDR_CreateDirectory(sdmcArchive, (FS_Path){PATH_ASCII, 13, (u8*)"/luma/titles"}, 0);
        FSLDR_CreateDirectory(sdmcArchive, (FS_Path){PATH_ASCII, sizeof(path) - 11, (u8*)path + 5}, 0);
        
        /* Substitute archiveId 4 (SaveData) to 9 (SDMC), to mount the save on sdcard */
        while(*((u32*)(code + fsMountSave)) != 0xe3a01004) fsMountSave += 4;
        *((u32*)(code + fsMountSave)) = 0xe3a01009;  // mov r1, #9
        
        /* Hook iFileOpen to redirect the save-file path to an sd directory */
        *((u32*)(code + iFileOpen)) = BRANCH(iFileOpen, saveRedirFunction);
        
        /* Patch fsControlArchive to never return errors */
        while((*((u32*)(code + fsControlArchive)) & 0xffff0000) != 0xe8bd0000) fsControlArchive += 4;
        *((u32*)(code + fsControlArchive - 4)) = 0xe3a00000;  // mov r0, #0
	}
}
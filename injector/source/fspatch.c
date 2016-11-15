#include <3ds.h>
#include "fspatch.h"
#include "memory.h"
#include "strings.h"
#include "ifile.h"
#include "romfsredir_bin.h"

#define BRANCH(src,dst)    (0xEA000000 | ((u32)((((u8 *)(dst) - (u8 *)(src)) >> 2) - 2) & 0xFFFFFF))
#define BRANCH_L(src,dst)  (0xEB000000 | ((u32)((((u8 *)(dst) - (u8 *)(src)) >> 2) - 2) & 0xFFFFFF))

Result openLumaFile(IFile *file, const char *path); // Externally implemented

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

void patchRomfsRedirection(u64 progId, u8* code, u32 size)
{
    /* Here we look for "/luma/romfs/[u64 titleID in hex, uppercase].romfs" */
    char path[] = "/luma/romfs/0000000000000000.romfs";
    IFile file;
    u64 romfsSize;
    u64 romfsOffset = 0;
    
    progIdToStr(path + 27, progId);
    if(R_FAILED(openLumaFile(&file, path))) return;
    else
    {
        if(R_FAILED(IFile_GetSize(&file, &romfsSize))) return;
        else
        {
            u64 total;
            u32 header;
            if(R_FAILED(IFile_Read(&file, &total, &header, 4)) || total != 4) return;
            else if(header == 0x43465649)
            {
                romfsOffset = 0x1000;
                romfsSize -= 0x1000;
            }		
        }
        IFile_Close(&file);
    }
    
    u32 fsOpenFileDirectly = findFunctionCommand(code, size, 0x08030204);
    u32 fsOpenLinkFile = findFunctionCommand(code, size, 0x80C0000);
    u32 throwFatalError = findThrowFatalError(code, size);
	
	if(fsOpenFileDirectly && throwFatalError)
	{
        u32 payload = throwFatalError;
   
        /* Setup the payload */
        memcpy(((void*)(code + payload)), (void*)romfsredir_bin, romfsredir_bin_size);
        *((u32*)(code + payload + 0x10)) = *((u32*)(code + fsOpenFileDirectly));
        *((u32*)(code + payload + romfsredir_bin_size - 0x08)) = strnlen(path, 0x30) + 1;
        *((u64*)(code + payload + romfsredir_bin_size - 0x10)) = (u64)romfsSize;
        *((u64*)(code + payload + romfsredir_bin_size - 0x18)) = (u64)romfsOffset;
        *((u32*)(code + payload + romfsredir_bin_size - 0x20)) = fsOpenFileDirectly + 0x100000;
        
        /* Place the hooks */
        *((u32*)(code + fsOpenFileDirectly)) = BRANCH(fsOpenFileDirectly, payload);
        if(fsOpenLinkFile)
        {
            *((u32*)(code + fsOpenLinkFile)) = 0xE3A03003; // mov r3, #3
            *((u32*)(code + fsOpenLinkFile + 4)) = BRANCH(fsOpenLinkFile + 4, payload);
            memcpy(((void*)(code + fsOpenLinkFile + 8)), (void*)path, 0x30);
            *((u32*)(code + payload + romfsredir_bin_size - 0x04)) = fsOpenLinkFile + 8 + 0x100000;  // String pointer
        }
        else
        {
            memcpy(((void*)(code + payload + romfsredir_bin_size)), (void*)path, 0x30);
            *((u32*)(code + payload + romfsredir_bin_size - 0x04)) = payload + romfsredir_bin_size + 0x100000;  // String pointer
        }
	}

}


#pragma once

#include <3ds/types.h>

void patchFsRedirection(u64 progId, u8* code, u32 size, u8 romfs, u8 save);

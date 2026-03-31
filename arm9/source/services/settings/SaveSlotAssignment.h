#pragma once
#include <nds/ndstypes.h>
#include "core/String.h"

class SaveSlotAssignment
{
public:
    SaveSlotAssignment() { }

    SaveSlotAssignment(const char* romPath, u32 saveSlot)
        : romPath(romPath), saveSlot(saveSlot) { }

    String<char, 256> romPath;
    u32 saveSlot = 1;
};

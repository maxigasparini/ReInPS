#ifndef PS2RECOMP_ADDRESS_TAKEN_CODE_H
#define PS2RECOMP_ADDRESS_TAKEN_CODE_H

#include "ps2recomp/types.h"

#include <cstdint>
#include <vector>

namespace ps2recomp
{
    struct AddressTakenCodeEntry
    {
        uint32_t sourceAddress = 0;
        uint32_t targetAddress = 0;
    };

    std::vector<AddressTakenCodeEntry>
    discoverAddressTakenCodeEntries(const std::vector<Section> &sections);
}

#endif // PS2RECOMP_ADDRESS_TAKEN_CODE_H

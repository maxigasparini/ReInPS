#ifndef PS2RECOMP_ADDRESS_TAKEN_CODE_H
#define PS2RECOMP_ADDRESS_TAKEN_CODE_H

#include "ps2recomp/types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ps2recomp
{
    struct AddressTakenCodeEntry
    {
        uint32_t sourceAddress = 0;
        uint32_t targetAddress = 0;
    };

    // Code addresses stored directly inside ELF data sections.
    std::vector<AddressTakenCodeEntry>
    discoverAddressTakenCodeEntries(
        const std::vector<Section> &sections);

    // Code addresses synthesized by executable instructions, for example:
    //
    //   lui   a0, 0x16
    //   addiu a0, a0, -0x1180
    //
    // producing 0x15EE80.
    std::vector<AddressTakenCodeEntry>
    discoverConstructedCodeEntries(
        const std::unordered_map<uint32_t, std::vector<Instruction>>
            &decodedFunctions,
        const std::vector<Section> &sections);
}

#endif // PS2RECOMP_ADDRESS_TAKEN_CODE_H

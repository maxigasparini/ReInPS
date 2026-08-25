#include "ps2recomp/address_taken_code.h"

#include <algorithm>
#include <cstring>

namespace ps2recomp
{
    namespace
    {
        bool isExecutableAddress(
            const std::vector<Section> &sections,
            uint32_t address)
        {
            for (const auto &section : sections)
            {
                if (!section.isCode || section.size == 0)
                {
                    continue;
                }

                const uint64_t start =
                    static_cast<uint64_t>(section.address);

                const uint64_t end =
                    start + static_cast<uint64_t>(section.size);

                const uint64_t candidate =
                    static_cast<uint64_t>(address);

                if (candidate >= start && candidate < end)
                {
                    return true;
                }
            }

            return false;
        }
    }

    std::vector<AddressTakenCodeEntry>
    discoverAddressTakenCodeEntries(
        const std::vector<Section> &sections)
    {
        std::vector<AddressTakenCodeEntry> entries;

        for (const auto &section : sections)
        {
            if (!section.isData ||
                section.isBSS ||
                !section.data ||
                section.size < sizeof(uint32_t))
            {
                continue;
            }

            for (uint32_t offset = 0;
                 offset + sizeof(uint32_t) <= section.size;
                 offset += sizeof(uint32_t))
            {
                uint32_t candidate = 0;

                std::memcpy(
                    &candidate,
                    section.data + offset,
                    sizeof(candidate));

                // EE instructions are 4-byte aligned.
                if ((candidate & 0x3u) != 0)
                {
                    continue;
                }

                if (!isExecutableAddress(sections, candidate))
                {
                    continue;
                }

                AddressTakenCodeEntry entry;
                entry.sourceAddress = section.address + offset;
                entry.targetAddress = candidate;

                entries.push_back(entry);
            }
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const AddressTakenCodeEntry &a,
               const AddressTakenCodeEntry &b)
            {
                if (a.sourceAddress != b.sourceAddress)
                {
                    return a.sourceAddress < b.sourceAddress;
                }

                return a.targetAddress < b.targetAddress;
            });

        return entries;
    }
}

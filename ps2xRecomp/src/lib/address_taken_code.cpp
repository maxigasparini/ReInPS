#include "ps2recomp/address_taken_code.h"
#include "ps2recomp/instructions.h"

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

                if (candidate >= start &&
                    candidate < end)
                {
                    return true;
                }
            }

            return false;
        }

        bool instructionWritesRegister(
            const Instruction &instruction,
            uint32_t reg)
        {
            if (reg == 0)
            {
                return false;
            }

            switch (instruction.opcode)
            {
            case OPCODE_ADDI:
            case OPCODE_ADDIU:
            case OPCODE_SLTI:
            case OPCODE_SLTIU:
            case OPCODE_ANDI:
            case OPCODE_ORI:
            case OPCODE_XORI:
            case OPCODE_LUI:
            case OPCODE_DADDI:
            case OPCODE_DADDIU:
            case OPCODE_LB:
            case OPCODE_LH:
            case OPCODE_LWL:
            case OPCODE_LW:
            case OPCODE_LBU:
            case OPCODE_LHU:
            case OPCODE_LWR:
            case OPCODE_LWU:
            case OPCODE_LQ:
            case OPCODE_LD:
                return instruction.rt == reg;

            case OPCODE_SPECIAL:
                if (instruction.function == SPECIAL_JR)
                {
                    return false;
                }

                if (instruction.function == SPECIAL_SYSCALL ||
                    instruction.function == SPECIAL_BREAK)
                {
                    return false;
                }

                return instruction.rd == reg;

            case OPCODE_MMI:
                return instruction.rd == reg;

            default:
                break;
            }

            // Conservative fallback for instructions already classified
            // by the decoder as modifying a GPR.
            if (instruction.modificationInfo.modifiesGPR)
            {
                return instruction.rt == reg ||
                       instruction.rd == reg;
            }

            return false;
        }

        bool tryCombineUpperImmediate(
            const Instruction &instruction,
            uint32_t upperRegister,
            uint32_t upperValue,
            uint32_t &target)
        {
            if (instruction.rs != upperRegister)
            {
                return false;
            }

            switch (instruction.opcode)
            {
            case OPCODE_ADDI:
            case OPCODE_ADDIU:
            case OPCODE_DADDI:
            case OPCODE_DADDIU:
            {
                const int32_t low =
                    static_cast<int32_t>(
                        static_cast<int16_t>(
                            instruction.immediate & 0xFFFFu));

                target =
                    upperValue +
                    static_cast<uint32_t>(low);

                return true;
            }

            case OPCODE_ORI:
                target =
                    upperValue |
                    (instruction.immediate & 0xFFFFu);

                return true;

            default:
                return false;
            }
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

                if ((candidate & 0x3u) != 0)
                {
                    continue;
                }

                if (!isExecutableAddress(
                        sections,
                        candidate))
                {
                    continue;
                }

                AddressTakenCodeEntry entry;
                entry.sourceAddress =
                    section.address + offset;
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
                    return a.sourceAddress <
                           b.sourceAddress;
                }

                return a.targetAddress <
                       b.targetAddress;
            });

        return entries;
    }

    std::vector<AddressTakenCodeEntry>
    discoverConstructedCodeEntries(
        const std::unordered_map<
            uint32_t,
            std::vector<Instruction>> &decodedFunctions,
        const std::vector<Section> &sections)
    {
        std::vector<AddressTakenCodeEntry> entries;

        // Function pointers are normally built very locally:
        //
        //   lui   reg, upper
        //   ...
        //   addiu reg, reg, lower
        //
        // Keep the window deliberately small so this remains
        // conservative and does not become global constant propagation.
        constexpr size_t kMaxLookahead = 12;

        for (const auto &[functionAddress, instructions] :
             decodedFunctions)
        {
            (void)functionAddress;

            for (size_t i = 0;
                 i < instructions.size();
                 ++i)
            {
                const Instruction &upper =
                    instructions[i];

                if (upper.opcode != OPCODE_LUI ||
                    upper.rt == 0)
                {
                    continue;
                }

                const uint32_t upperRegister =
                    upper.rt;

                const uint32_t upperValue =
                    (upper.immediate & 0xFFFFu) << 16;

                const size_t end =
                    std::min(
                        instructions.size(),
                        i + 1 + kMaxLookahead);

                bool stopAfterDelaySlot = false;

		for (size_t j = i + 1;
		     j < end;
		     ++j)
		{
		    const Instruction &candidateInstruction =
		        instructions[j];

		    const bool isDelaySlot =
		        stopAfterDelaySlot;

		    // A MIPS control-transfer instruction still executes
		    // the following instruction as its delay slot.
		    //
		    // Do not propagate beyond that delay slot, but allow
		    // the slot itself to complete a local LUI-derived
		    // address construction.
		    if (!isDelaySlot &&
		        (candidateInstruction.isBranch ||
		         candidateInstruction.isJump))
		    {
		        // Be conservative if the control-transfer itself
		        // destroys the register holding the upper half.
		        if (instructionWritesRegister(
		                candidateInstruction,
		                upperRegister))
		        {
		            break;
		        }

		        stopAfterDelaySlot = true;
		        continue;
		    }

		    uint32_t candidate = 0;

		    if (tryCombineUpperImmediate(
		            candidateInstruction,
		            upperRegister,
		            upperValue,
		            candidate))
		    {
		        if ((candidate & 0x3u) == 0 &&
		            isExecutableAddress(
		                sections,
		                candidate))
		        {
		            AddressTakenCodeEntry entry;
		            entry.sourceAddress =
		                candidateInstruction.address;
		            entry.targetAddress =
		                candidate;

		            entries.push_back(entry);
		        }

		        // If the low-half instruction overwrites the
		        // register that contained the upper half,
		        // the original LUI value is no longer live.
		        if (candidateInstruction.rt ==
		            upperRegister)
		        {
		            break;
		        }

		        // Never propagate past a branch/jump delay slot.
		        if (isDelaySlot)
		        {
		            break;
		        }

		        continue;
		    }

		    if (instructionWritesRegister(
		            candidateInstruction,
		            upperRegister))
		    {
		        break;
		    }

		    // The delay slot was inspected. Do not continue into
		    // whichever control-flow path follows the branch/jump.
		    if (isDelaySlot)
		    {
		        break;
		    }
		}
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
                    return a.sourceAddress <
                           b.sourceAddress;
                }

                return a.targetAddress <
                       b.targetAddress;
            });

        entries.erase(
            std::unique(
                entries.begin(),
                entries.end(),
                [](const AddressTakenCodeEntry &a,
                   const AddressTakenCodeEntry &b)
                {
                    return
                        a.sourceAddress ==
                            b.sourceAddress &&
                        a.targetAddress ==
                            b.targetAddress;
                }),
            entries.end());

        return entries;
    }
}

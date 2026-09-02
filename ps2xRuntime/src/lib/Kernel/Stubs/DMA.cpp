#include "Common.h"
#include "DMA.h"

namespace ps2_stubs
{
    namespace
    {
        struct SceDmaEnv
        {
            uint8_t sts = 0;
            uint8_t std = 0;
            uint8_t mfd = 0;
            uint8_t rele = 0;
            uint32_t pcr = 0;
            uint32_t sqwc = 0;
            uint32_t rbor = 0;
            uint32_t rbsr = 0;
        };

        static_assert(sizeof(SceDmaEnv) == 0x14, "sceDmaEnv must match the guest ABI");

        constexpr uint32_t DMA_REG_CTRL = 0x1000E000u;
        constexpr uint32_t DMA_REG_PCR = 0x1000E020u;
        constexpr uint32_t DMA_REG_SQWC = 0x1000E030u;
        constexpr uint32_t DMA_REG_RBSR = 0x1000E040u;
        constexpr uint32_t DMA_REG_RBOR = 0x1000E050u;
        constexpr uint32_t DMA_REG_STADR = 0x1000E060u;

        constexpr std::array<uint8_t, 10> kStsTable = {0u, 0u, 0u, 3u, 0u, 1u, 0u, 0u, 2u, 0u};
        constexpr std::array<uint8_t, 10> kStdTable = {0u, 1u, 2u, 0u, 0u, 0u, 3u, 0u, 0u, 0u};
        constexpr std::array<uint8_t, 10> kMfdTable = {0u, 2u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

        std::mutex g_dmaEnvMutex;
        SceDmaEnv g_dmaCurrentEnv;
    }

    void DmaAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, getRegU32(ctx, 4));
    }

    void sceDmaCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaCallback", rdram, ctx, runtime);
    }

    void sceDmaDebug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaDebug", rdram, ctx, runtime);
    }

    void sceDmaGetChan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t chanArg = getRegU32(ctx, 4);
        const uint32_t channelBase = resolveDmaChannelBase(rdram, chanArg);
        setReturnU32(ctx, channelBase);
    }

    void sceDmaGetEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t envAddr = getRegU32(ctx, 4);
        if (uint8_t *dst = getMemPtr(rdram, envAddr))
        {
            std::lock_guard<std::mutex> lock(g_dmaEnvMutex);
            std::memcpy(dst, &g_dmaCurrentEnv, sizeof(g_dmaCurrentEnv));
        }
        setReturnU32(ctx, envAddr);
    }

    void sceDmaLastSyncTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaLastSyncTime", rdram, ctx, runtime);
    }

    void sceDmaPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPause", rdram, ctx, runtime);
    }

    void sceDmaPutEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t envAddr = getRegU32(ctx, 4);
        const uint8_t *src = getConstMemPtr(rdram, envAddr);
        if (!src || !runtime)
        {
            setReturnS32(ctx, -1);
            return;
        }

        SceDmaEnv env{};
        std::memcpy(&env, src, sizeof(env));

        if (env.sts >= kStsTable.size())
        {
            setReturnS32(ctx, -1);
            return;
        }
        if (env.std >= kStdTable.size())
        {
            setReturnS32(ctx, -2);
            return;
        }
        if (env.mfd >= kMfdTable.size())
        {
            setReturnS32(ctx, -3);
            return;
        }
        if (env.rele >= 7u)
        {
            setReturnS32(ctx, -4);
            return;
        }

        PS2Memory &mem = runtime->memory();
        uint32_t ctrl = mem.readIORegister(DMA_REG_CTRL);
        ctrl = (ctrl & 0xFFFFFFCFu) | (static_cast<uint32_t>(kStsTable[env.sts]) << 4);
        ctrl = (ctrl & 0xFFFFFF3Fu) | (static_cast<uint32_t>(kStdTable[env.std]) << 6);
        ctrl = (ctrl & 0xFFFFFFF3u) | (static_cast<uint32_t>(kMfdTable[env.mfd]) << 2);
        if (env.rele == 0u)
        {
            ctrl &= 0xFFFFFFFDu;
        }
        else
        {
            ctrl = ((ctrl | 0x2u) & 0xFFFFFCFFu) | ((static_cast<uint32_t>(env.rele - 1u) & 0x7u) << 8);
        }

        mem.writeIORegister(DMA_REG_CTRL, ctrl);
        mem.writeIORegister(DMA_REG_PCR, env.pcr);
        mem.writeIORegister(DMA_REG_SQWC, env.sqwc);
        mem.writeIORegister(DMA_REG_RBOR, env.rbor);
        mem.writeIORegister(DMA_REG_RBSR, env.rbsr);

        {
            std::lock_guard<std::mutex> lock(g_dmaEnvMutex);
            g_dmaCurrentEnv = env;
        }

        setReturnS32(ctx, 0);
    }

    void sceDmaPutStallAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t newAddr = getRegU32(ctx, 4);
        uint32_t oldAddr = 0;
        if (runtime)
        {
            PS2Memory &mem = runtime->memory();
            oldAddr = mem.readIORegister(DMA_REG_STADR);
            if (newAddr != 0xFFFFFFFFu)
            {
                mem.writeIORegister(DMA_REG_STADR, newAddr);
            }
        }
        setReturnU32(ctx, oldAddr);
    }
void sceDmaRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (!runtime)
    {
        setReturnS32(ctx, -1);
        return;
    }

    const uint32_t chanArg = getRegU32(ctx, 4);
    const uint32_t channelBase = resolveDmaChannelBase(rdram, chanArg);

    // Channel 8: FromSPR.
    constexpr uint32_t FROM_SPR = 0x1000D000u;

    if (channelBase != FROM_SPR)
    {
        RUNTIME_LOG("[sceDmaRecv] unsupported channel=0x"
                    << std::hex << channelBase << std::dec << std::endl);
        setReturnS32(ctx, -1);
        return;
    }

    PS2Memory &mem = runtime->memory();

    uint32_t chcr = mem.readIORegister(FROM_SPR + 0x00u);
    uint32_t madr = mem.readIORegister(FROM_SPR + 0x10u);
    uint32_t qwc  = mem.readIORegister(FROM_SPR + 0x20u);
    uint32_t sadr = mem.readIORegister(FROM_SPR + 0x80u) & 0x3FFFu;

    const uint32_t mode = (chcr >> 2u) & 0x3u;

    // STR = DMA active.
    chcr |= 0x100u;
    mem.writeIORegister(FROM_SPR + 0x00u, chcr);

    auto copyFromSpr = [&](uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t sprAddr =
                0x70000000u | (sadr & 0x3FF0u);

            const __m128i value = mem.read128(sprAddr);
            mem.write128(madr, value);

            sadr = (sadr + 16u) & 0x3FFFu;
            madr += 16u;
        }
    };

    if (mode == 0u)
    {
        // Normal mode:
        // QWC quadwords from SPR[SADR] to RAM[MADR].
        copyFromSpr(qwc);
        qwc = 0u;
    }
    else if (mode == 1u)
    {
        // FromSPR uses destination-chain tags stored in scratchpad.
        constexpr uint32_t TAG_CNTS = 0u;
        constexpr uint32_t TAG_CNT  = 1u;
        constexpr uint32_t TAG_END  = 7u;

        bool finished = false;

        for (uint32_t tagIndex = 0;
             tagIndex < 4096u && !finished;
             ++tagIndex)
        {
            const uint32_t tagAddr =
                0x70000000u | (sadr & 0x3FF0u);

            const uint64_t tag = mem.read64(tagAddr);

            qwc = static_cast<uint32_t>(tag & 0xFFFFu);
            const uint32_t id =
                static_cast<uint32_t>((tag >> 28u) & 0x7u);

            const bool irq =
                (tag & 0x80000000ull) != 0ull;

            madr =
                static_cast<uint32_t>(tag >> 32u) &
                0x7FFFFFFFu;

            // Hardware consumes the 16-byte DMA tag first.
            sadr = (sadr + 16u) & 0x3FFFu;

            // Preserve the TAG field in CHCR.
            chcr =
                (chcr & 0x0000FFFFu) |
                (static_cast<uint32_t>((tag >> 16u) & 0xFFFFu) << 16u);

            copyFromSpr(qwc);
            qwc = 0u;

            if (id == TAG_END)
            {
                finished = true;
            }
            else if (id != TAG_CNTS && id != TAG_CNT)
            {
                RUNTIME_LOG("[sceDmaRecv:FromSPR] unsupported destination tag id="
                            << id << std::endl);
                finished = true;
            }

            // TIE + tag IRQ also terminates the chain.
            if ((chcr & 0x80u) != 0u && irq)
            {
                finished = true;
            }
        }
    }
    else
    {
        RUNTIME_LOG("[sceDmaRecv:FromSPR] unsupported mode="
                    << mode << std::endl);

        chcr &= ~0x100u;
        mem.writeIORegister(FROM_SPR + 0x00u, chcr);

        setReturnS32(ctx, -1);
        return;
    }

    // Store final hardware-visible channel state.
    mem.writeIORegister(FROM_SPR + 0x10u, madr);
    mem.writeIORegister(FROM_SPR + 0x20u, qwc);
    mem.writeIORegister(FROM_SPR + 0x80u, sadr);

    // DMA completes synchronously in the current runtime model.
    chcr &= ~0x100u;
    mem.writeIORegister(FROM_SPR + 0x00u, chcr);

    setReturnS32(ctx, 0);
}

    void sceDmaRecvI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvI", rdram, ctx, runtime);
    }

    void sceDmaRecvN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvN", rdram, ctx, runtime);
    }

    void sceDmaReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (runtime)
        {
            PS2Memory &mem = runtime->memory();

            // libdma reset leaves the controller runnable; DMAE must be re-enabled or chain submissions will be accepted but never execute.
            mem.writeIORegister(DMA_REG_CTRL, 0u);
            mem.writeIORegister(DMA_REG_PCR, 0u);
            mem.writeIORegister(DMA_REG_SQWC, 0u);
            mem.writeIORegister(DMA_REG_RBOR, 0u);
            mem.writeIORegister(DMA_REG_RBSR, 0u);
            mem.writeIORegister(DMA_REG_STADR, 0u);
            mem.writeIORegister(DMA_REG_CTRL, 1u);
        }

        {
            std::lock_guard<std::mutex> lock(g_dmaEnvMutex);
            g_dmaCurrentEnv = {};
        }

        setReturnS32(ctx, 0);
    }

    void sceDmaRestart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRestart", rdram, ctx, runtime);
    }

    void sceDmaSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, true));
    }

    void sceDmaSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSync(rdram, ctx, runtime));
    }

    void sceDmaSyncN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSync(rdram, ctx, runtime));
    }

    void sceDmaWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaWatch", rdram, ctx, runtime);
    }
}

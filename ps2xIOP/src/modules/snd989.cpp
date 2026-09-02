#include "module_factories.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kSnd989MainSid = 0x00123456u;
        constexpr uint32_t kSnd989LoaderSid = 0x00123457u;

        constexpr uint32_t kBatchCommand = 0x4Du;
        constexpr uint32_t kMaxBatchCommands = 256u;

        constexpr uint32_t kFirstBankHandle = 0x10000001u;
        constexpr uint32_t kFirstSoundHandle = 0x20000001u;

        constexpr uint32_t kCompletionMarker = 0xFFFFFFFFu;

        struct BankRecord
        {
            uint32_t location = 0u;
            uint32_t offset = 0u;
        };

        class Snd989Service final : public IopService
        {
        public:
            explicit Snd989Service(IopHost &host)
                : m_host(host),
                  m_sids{kSnd989MainSid, kSnd989LoaderSid}
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "989snd";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return m_sids;
            }

            void reset() override
            {
    {
        std::lock_guard<std::mutex> lock(m_movieMutex);
        releaseMovieBuffer();
    }

                m_statsAddress = 0u;

                m_nextBankHandle = kFirstBankHandle;
                m_nextSoundHandle = kFirstSoundHandle;

                m_banks.clear();

                m_mainCommandCount = 0u;
                m_batchCount = 0u;
                m_loaderCommandCount = 0u;
                m_unknownBatchCommandCount = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid == kSnd989LoaderSid)
                {
                    return handleLoaderRpc(request);
                }

                if (request.sid != kSnd989MainSid)
                {
                    return {};
                }

                if (request.function == kBatchCommand)
                {
                    return handleBatchRpc(request);
                }

                return handleMainRpc(request);
            }
	void onSifTransfer(const SifTransfer &transfer) override
	{
	    if (transfer.kind != SifTransferKind::SetDma ||
	        transfer.phase != SifTransferPhase::AfterCopy)
	    {
	        return;
	    }

	    std::lock_guard<std::mutex> lock(m_movieMutex);

	    if (m_movieBufferBase == 0u ||
	        m_movieBufferSize == 0u)
	    {
	        return;
	    }

	    const uint64_t base = m_movieBufferBase;
	    const uint64_t end = base + m_movieBufferSize;
	    const uint64_t destination = transfer.destinationAddress;

	    if (destination < base || destination >= end)
	    {
	        return;
	    }

	    const uint32_t offset =
	        static_cast<uint32_t>(destination - base);

	    const uint32_t nextOffset =
	        (offset + transfer.size) % m_movieBufferSize;

	    m_movieWritePosition =
	        m_movieBufferBase + nextOffset;

	    // Silent HLE:
	    // once playback has started, pretend the IOP consumes
	    // each block immediately after the EE sends it.
	    if (m_movieStarted)
	    {
	        m_movieTransferPosition =
	            m_movieWritePosition;
	    }
	}

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                metrics.push_back({"stats_address", m_statsAddress, true});
                metrics.push_back(
                    {"loaded_banks",
                     static_cast<uint64_t>(m_banks.size()),
                     false});
                metrics.push_back(
                    {"main_commands", m_mainCommandCount, false});
                metrics.push_back(
                    {"batches", m_batchCount, false});
                metrics.push_back(
                    {"loader_commands", m_loaderCommandCount, false});
                metrics.push_back(
                    {"unknown_batch_commands",
                     m_unknownBatchCommandCount,
                     false});
            }

        private:
            [[nodiscard]] bool readU32(
                const GuestBuffer &buffer,
                uint32_t offset,
                uint32_t &value) const
            {
                if (buffer.address == 0u ||
                    offset > buffer.size ||
                    buffer.size - offset < sizeof(uint32_t))
                {
                    return false;
                }

                return m_host.readGuest(
                    buffer.address + offset,
                    &value,
                    sizeof(value));
            }

            [[nodiscard]] bool writeSingleResponse(
                const RpcRequest &request,
                uint32_t value)
            {
                if (request.receive.address == 0u ||
                    request.receive.size < 3u * sizeof(uint32_t))
                {
                    return false;
                }

                const std::array<uint32_t, 3> response{
                    kCompletionMarker,
                    value,
                    kCompletionMarker,
                };

                return m_host.writeGuest(
                    request.receive.address,
                    response.data(),
                    sizeof(response));
            }

            [[nodiscard]] RpcResult makeHandledResult(
                const RpcRequest &request) const
            {
                RpcResult result{};
                result.handled = true;
                result.resultAddress = request.receive.address;
                return result;
            }

            [[nodiscard]] uint32_t allocateBankHandle(
                uint32_t location,
                uint32_t offset)
            {
                const uint32_t handle = m_nextBankHandle++;

                m_banks.emplace(
                    handle,
                    BankRecord{
                        .location = location,
                        .offset = offset,
                    });

                return handle;
            }

            [[nodiscard]] uint32_t allocateSoundHandle()
            {
                return m_nextSoundHandle++;
            }

            [[nodiscard]] bool executeMainCommand(
                uint32_t command,
                const GuestBuffer &payload,
                uint32_t &returnValue)
            {
                returnValue = 0u;

                switch (command)
                {
                case 0x00u:
                {
                    // snd_StartSoundSystem:
                    // payload[0] = EE address of gStats.
                    uint32_t statsAddress = 0u;

                    if (!readU32(payload, 0u, statsAddress))
                    {
                        return false;
                    }

                    m_statsAddress = statsAddress;
                    return true;
                }

                case 0x06u:
                {
                    // snd_UnloadBank
                    uint32_t bank = 0u;

                    if (readU32(payload, 0u, bank))
                    {
                        m_banks.erase(bank);
                    }

                    return true;
                }

                case 0x08u:
                    // snd_ResolveBankXREFS
                    return true;

                case 0x09u:
                    // snd_SetMasterVolume
                    return true;

                case 0x0Au:
                    // snd_GetMasterVolume.
                    // Basic HLE: neutral/default value.
                    returnValue = 0u;
                    return true;

                case 0x0Bu:
                    // snd_SetPlaybackMode
                    return true;

                case 0x0Du:
                    // snd_SetMixerMode
                    return true;

                case 0x0Eu:
                    // snd_SetReverbType
                    return true;

                case 0x0Fu:
                    // snd_SetReverbDepth
                    return true;

                case 0x11u:
                    // snd_PlaySoundVolPanPMPB.
                    //
                    // Return a non-zero opaque sound handle. We do not
                    // reproduce audio yet.
                    returnValue = allocateSoundHandle();
                    return true;

                case 0x13u:
                    // snd_PauseSound
                    return true;

                case 0x14u:
                    // snd_ContinueSound
                    return true;

                case 0x15u:
                    // snd_StopSound
                    return true;

                case 0x16u:
                    // snd_PauseAllSoundsInGroup
                    return true;

                case 0x17u:
                    // snd_ContinueAllSoundsInGroup
                    return true;

                case 0x19u:
                    // snd_SoundIsStillPlaying.
                    //
                    // Sounds complete immediately in the basic HLE. This
                    // prevents EE code from waiting forever for audio that
                    // ReInPS is not actually playing yet.
                    returnValue = 0u;
                    return true;

                case 0x1Au:
                    // snd_IsSoundALooper.
                    //
                    // We do not parse sound-bank metadata yet.
                    returnValue = 0u;
                    return true;

                case 0x1Bu:
                    // snd_SetSoundVolPan
                    return true;

                case 0x1Cu:
                    // snd_GetSoundOriginalPitch.
                    //
                    // No bank metadata is available in the basic HLE.
                    returnValue = 0u;
                    return true;

                case 0x1Eu:
                    // snd_SetSoundPitch
                    return true;

                case 0x22u:
                    // snd_AutoVol
                    return true;

                case 0x27u:
                    // snd_SetGlobalExcite
                    return true;

                case 0x28u:
                    // snd_GetMIDIRegister
                    returnValue = 0u;
                    return true;

                case 0x29u:
                    // snd_SetMIDIRegister
                    return true;

                case 0x2Au:
                    // snd_InitVAGStreamingEx.
                    //
                    // Return 0 intentionally: streaming remains disabled.
                    // EE-side 989snd then falls back to the regular CDVD
                    // functions instead of requiring real 989snd streaming.
                    returnValue = 0u;
                    return true;

                case 0x2Cu:
                    // snd_PlayVAGStreamByLoc.
                    //
                    // Normally unreachable while streaming is disabled, but
                    // provide an opaque handle if the game calls it anyway.
                    returnValue = allocateSoundHandle();
                    return true;

                case 0x2Eu:
                    // snd_ContinueVAGStream
                    return true;

                case 0x34u:
                    // snd_StopAllStreams
                    return true;

                case 0x36u:
                    // snd_StreamSafeCheckCDIdle.
                    //
                    // 0 = not busy for the simplified HLE.
                    returnValue = 0u;
                    return true;

                case 0x37u:
                    // snd_StreamSafeCdBreak
                    return true;

                case 0x38u:
                    // snd_StreamSafeCdRead.
                    //
                    // Streaming is disabled above, so this should normally
                    // not be used.
                    return true;
		case 0x3Bu:
		{
		    // snd_InitMovieSound
		    //
		    // payload:
		    // [0] sizeOfIOPBuffer
		    // [1] volumeLevel
		    // [2] panCenter
		    // [3] volumeGroup
		    // [4] type

		    uint32_t requestedSize = 0u;

		    if (!readU32(payload, 0u, requestedSize) ||
		        requestedSize == 0u)
		    {
		        return false;
		    }

		    std::lock_guard<std::mutex> lock(m_movieMutex);

		    releaseMovieBuffer();

		    const uint32_t buffer =
		        m_host.allocateIopMemory(requestedSize);

		    if (buffer == 0u)
		    {
		        returnValue = 0xFFFFFFFFu;
		        return true;
		    }

		    m_movieBufferBase = buffer;
		    m_movieBufferSize = requestedSize;

		    m_movieWritePosition = buffer;
		    m_movieTransferPosition = buffer;

		    m_movieStarted = false;

		    // snd_InitMovieSound returns the IOP buffer address.
		    returnValue = buffer;
		    return true;
		}

		case 0x3Cu:
		{
		    // snd_CloseMovieSound

		    std::lock_guard<std::mutex> lock(m_movieMutex);

		    releaseMovieBuffer();

		    returnValue = 0u;
		    return true;
		}

		case 0x3Du:
		{
		    // snd_ResetMovieSound

		    std::lock_guard<std::mutex> lock(m_movieMutex);

		    m_movieWritePosition = m_movieBufferBase;
		    m_movieTransferPosition = m_movieBufferBase;
		    m_movieStarted = false;

		    returnValue = 0u;
		    return true;
		}

		case 0x3Eu:
		{
		    // snd_StartMovieSound
		    //
		    // payload:
		    // [0] iopBuffer
		    // [1] iopBufferSize
		    // [2] iopPausePosition
		    // [3] sampleRate
		    // [4] channels

		    uint32_t buffer = 0u;
		    uint32_t bufferSize = 0u;
		    uint32_t pausePosition = 0u;

		    if (!readU32(payload, 0u, buffer) ||
		        !readU32(payload, 4u, bufferSize) ||
		        !readU32(payload, 8u, pausePosition))
		    {
		        return false;
		    }

		    std::lock_guard<std::mutex> lock(m_movieMutex);

		    // Normally these must be the same values returned
		    // by snd_InitMovieSound.
		    if (m_movieBufferBase == 0u)
		    {
		        m_movieBufferBase = buffer;
		    }

		    if (m_movieBufferSize == 0u)
		    {
		        m_movieBufferSize = bufferSize;
		    }

		    m_movieStarted = true;

		    // Any data already queued before Start is considered
		    // immediately consumed by this silent HLE.
		    m_movieTransferPosition =
		        m_movieWritePosition;

		    (void)pausePosition;

		    returnValue = 0u;
		    return true;
		}

		case 0x40u:
		{
		    // snd_GetTransStatus
		    //
		    // Return current IOP consumption position.

		    std::lock_guard<std::mutex> lock(m_movieMutex);

		    returnValue = m_movieTransferPosition;
		    return true;
		}

                case 0x4Eu:
                    // snd_SetGroupVoiceRange
                    return true;

                case 0x4Fu:
                    // snd_IsVAGStreamBuffered_CB
                    returnValue = 0u;
                    return true;

                case 0x51u:
                    // snd_PreAllocReverbWorkArea
                    return true;

                default:
                    return false;
                }
            }

            [[nodiscard]] RpcResult handleMainRpc(
                const RpcRequest &request)
            {
                uint32_t returnValue = 0u;

                if (!executeMainCommand(
                        request.function,
                        request.send,
                        returnValue))
                {
                    // Direct/synchronous unknown commands remain visible as
                    // unhandled RPCs. Their return semantics may matter, so
                    // do not silently invent a result.
                    return {};
                }

                if (!writeSingleResponse(request, returnValue))
                {
                    return {};
                }

                ++m_mainCommandCount;
                return makeHandledResult(request);
            }

            [[nodiscard]] RpcResult handleLoaderRpc(
                const RpcRequest &request)
            {
                // The known 989snd loader operation used by Sly:
                //
                // function 3 = snd_BankLoadByLoc
                //
                // send[0] = disc location
                // send[1] = offset
                // recv[0] = SoundBankPtr
                if (request.function != 3u)
                {
                    return {};
                }

                if (request.receive.address == 0u ||
                    request.receive.size < sizeof(uint32_t))
                {
                    return {};
                }

                uint32_t location = 0u;
                uint32_t offset = 0u;

                if (!readU32(request.send, 0u, location) ||
                    !readU32(request.send, 4u, offset))
                {
                    return {};
                }

                const uint32_t bankHandle =
                    allocateBankHandle(location, offset);

                if (!m_host.writeGuest(
                        request.receive.address,
                        &bankHandle,
                        sizeof(bankHandle)))
                {
                    return {};
                }

                ++m_loaderCommandCount;
                return makeHandledResult(request);
            }

            [[nodiscard]] RpcResult handleBatchRpc(
                const RpcRequest &request)
            {
                if (request.send.address == 0u ||
                    request.send.size < sizeof(uint32_t))
                {
                    return {};
                }

                uint32_t commandCount = 0u;

                if (!readU32(request.send, 0u, commandCount))
                {
                    return {};
                }

                if (commandCount > kMaxBatchCommands)
                {
                    return {};
                }

                const uint32_t responseWords = commandCount + 2u;
                const uint32_t responseBytes =
                    responseWords * sizeof(uint32_t);

                if (request.receive.address == 0u ||
                    request.receive.size < responseBytes)
                {
                    return {};
                }

                std::vector<uint32_t> response(
                    responseWords,
                    0u);

                response.front() = kCompletionMarker;
                response.back() = kCompletionMarker;

                uint32_t cursor = sizeof(uint32_t);

                for (uint32_t index = 0u;
                     index < commandCount;
                     ++index)
                {
                    if (cursor > request.send.size ||
                        request.send.size - cursor <
                            2u * sizeof(uint16_t))
                    {
                        return {};
                    }

                    std::array<uint16_t, 2> header{};

                    if (!m_host.readGuest(
                            request.send.address + cursor,
                            header.data(),
                            sizeof(header)))
                    {
                        return {};
                    }

                    const uint32_t command =
                        static_cast<uint32_t>(header[0]);

                    const uint32_t dataSize =
                        static_cast<uint32_t>(header[1]);

                    const uint32_t rawRecordSize =
                        2u * sizeof(uint16_t) + dataSize;

                    const uint32_t recordSize =
                        (rawRecordSize + 3u) & ~3u;

                    if (cursor > request.send.size ||
                        recordSize > request.send.size - cursor)
                    {
                        return {};
                    }

                    GuestBuffer payload{};

                    if (dataSize != 0u)
                    {
                        payload.address =
                            request.send.address +
                            cursor +
                            2u * sizeof(uint16_t);

                        payload.size = dataSize;
                    }

                    uint32_t commandResult = 0u;

                    if (!executeMainCommand(
                            command,
                            payload,
                            commandResult))
                    {
                        // Batched 989snd commands are overwhelmingly
                        // asynchronous setters/actions. For the basic HLE,
                        // unknown batched commands complete as no-ops, but we
                        // retain a metric so they are not invisible.
                        commandResult = 0u;
                        ++m_unknownBatchCommandCount;
                    }

                    response[index + 1u] = commandResult;
                    cursor += recordSize;
                }

                if (!m_host.writeGuest(
                        request.receive.address,
                        response.data(),
                        responseBytes))
                {
                    return {};
                }

                ++m_batchCount;
                return makeHandledResult(request);
            }

		void releaseMovieBuffer()
		{
		    if (m_movieBufferBase != 0u)
		    {
		        (void)m_host.freeIopMemory(
		            m_movieBufferBase);
		    }

		    m_movieBufferBase = 0u;
		    m_movieBufferSize = 0u;
		    m_movieWritePosition = 0u;
		    m_movieTransferPosition = 0u;
		    m_movieStarted = false;
		}

            IopHost &m_host;
            std::array<uint32_t, 2> m_sids;

            uint32_t m_statsAddress = 0u;

		std::mutex m_movieMutex;

		uint32_t m_movieBufferBase = 0u;
		uint32_t m_movieBufferSize = 0u;

		uint32_t m_movieWritePosition = 0u;
		uint32_t m_movieTransferPosition = 0u;

		bool m_movieStarted = false;

            uint32_t m_nextBankHandle = kFirstBankHandle;
            uint32_t m_nextSoundHandle = kFirstSoundHandle;

            std::unordered_map<uint32_t, BankRecord> m_banks;

            uint64_t m_mainCommandCount = 0u;
            uint64_t m_batchCount = 0u;
            uint64_t m_loaderCommandCount = 0u;
            uint64_t m_unknownBatchCommandCount = 0u;
        };
    }

    std::unique_ptr<IopService> createSnd989Service(IopHost &host)
    {
        return std::make_unique<Snd989Service>(host);
    }
}

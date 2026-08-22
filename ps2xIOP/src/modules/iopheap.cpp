#include "module_factories.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kIopHeapSid = 0x80000003u;

        constexpr uint32_t kRpcAlloc = 1u;
        constexpr uint32_t kRpcFree = 2u;
        constexpr uint32_t kRpcLoad = 3u;

        // ROMVER sintético para compatibilidad.
        // Formato: VVVVRTYYYYMMDD
        //
        // No contiene datos extraídos de una BIOS de Sony.
        constexpr std::array<uint8_t, 16> kSyntheticRomver{
            '0', '2', '0', '0',
            'A', 'C',
            '2', '0', '0', '0',
            '0', '1', '0', '1',
            0, 0
        };

        class IopHeapService final : public IopService
        {
        public:
            explicit IopHeapService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "iopheap";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
            }

            [[nodiscard]] RpcResult handleRpc(
                const RpcRequest &request) override
            {
                RpcResult result;

                if (request.sid != kIopHeapSid)
                {
                    return result;
                }

                if (request.function == kRpcAlloc)
                {
                    return handleAlloc(request);
                }

                if (request.function == kRpcFree)
                {
                    return handleFree(request);
                }

                if (request.function == kRpcLoad)
                {
                    return handleLoad(request);
                }

                return result;
            }

        private:
            RpcResult makeHandledResult(
                const RpcRequest &request) const
            {
                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;

                return result;
            }

            RpcResult handleAlloc(
                const RpcRequest &request)
            {
                RpcResult result = makeHandledResult(request);

                if (request.send.address == 0u ||
                    request.send.size < sizeof(uint32_t) ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(uint32_t))
                {
                    result.handled = false;
                    return result;
                }

                uint32_t size = 0u;

                if (!m_host.readGuest(
                        request.send.address,
                        &size,
                        sizeof(size)))
                {
                    result.handled = false;
                    return result;
                }

                const uint32_t address =
                    m_host.allocateIopMemory(size);

                if (!m_host.writeGuest(
                        request.receive.address,
                        &address,
                        sizeof(address)))
                {
                    result.handled = false;
                }

                return result;
            }

            RpcResult handleFree(
                const RpcRequest &request)
            {
                RpcResult result = makeHandledResult(request);

                if (request.send.address == 0u ||
                    request.send.size < sizeof(uint32_t) ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(uint32_t))
                {
                    result.handled = false;
                    return result;
                }

                uint32_t address = 0u;

                if (!m_host.readGuest(
                        request.send.address,
                        &address,
                        sizeof(address)))
                {
                    result.handled = false;
                    return result;
                }

                const int32_t response =
                    m_host.freeIopMemory(address)
                        ? 0
                        : -1;

                if (!m_host.writeGuest(
                        request.receive.address,
                        &response,
                        sizeof(response)))
                {
                    result.handled = false;
                }

                return result;
            }

            RpcResult handleLoad(
                const RpcRequest &request)
            {
                RpcResult result = makeHandledResult(request);

                if (request.send.address == 0u ||
                    request.send.size < 256u ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(int32_t))
                {
                    result.handled = false;
                    return result;
                }

                uint32_t destination = 0u;

                if (!m_host.readGuest(
                        request.send.address,
                        &destination,
                        sizeof(destination)))
                {
                    result.handled = false;
                    return result;
                }

                std::array<char, 252> path{};

                if (!m_host.readGuest(
                        request.send.address + sizeof(uint32_t),
                        path.data(),
                        path.size()))
                {
                    result.handled = false;
                    return result;
                }

                path.back() = '\0';

                int32_t response = -1;

                if (std::strcmp(path.data(), "rom:ROMVER") == 0 ||
                    std::strcmp(path.data(), "rom0:ROMVER") == 0)
                {
                    if (m_host.writeGuest(
                            destination,
                            kSyntheticRomver.data(),
                            kSyntheticRomver.size()))
                    {
                        response = 0;
                    }
                }

                if (!m_host.writeGuest(
                        request.receive.address,
                        &response,
                        sizeof(response)))
                {
                    result.handled = false;
                }

                return result;
            }

            inline static constexpr std::array<uint32_t, 1> kSids{
                kIopHeapSid
            };

            IopHost &m_host;
        };
    }

    std::unique_ptr<IopService> createIopHeapService(IopHost &host)
    {
        return std::make_unique<IopHeapService>(host);
    }
}

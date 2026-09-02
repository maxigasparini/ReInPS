#include "module_factories.h"

#include <array>
#include <cstdint>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kCdServerDiskReady = 0x8000059Au;

        constexpr uint32_t kSceCdComplete = 2u;

        class CdvdFsvService final : public IopService
        {
        public:
            explicit CdvdFsvService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "cdvdfsv";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kCdServerDiskReady)
                {
                    return {};
                }

                // sceCdDiskReady uses RPC function 0.
                if (request.function != 0u)
                {
                    return {};
                }

                if (request.receive.address == 0u ||
                    request.receive.size < sizeof(uint32_t))
                {
                    return {};
                }

                // TEMP HLE:
                // The emulated disc is considered ready.
                constexpr uint32_t resultValue = kSceCdComplete;

                if (!m_host.writeGuest(
                        request.receive.address,
                        &resultValue,
                        sizeof(resultValue)))
                {
                    return {};
                }

                RpcResult result{};
                result.handled = true;
                result.resultAddress = request.receive.address;
                return result;
            }

        private:
            inline static constexpr std::array<uint32_t, 1> kSids{
                kCdServerDiskReady,
            };

            IopHost &m_host;
        };
    }

    std::unique_ptr<IopService> createCdvdFsvService(IopHost &host)
    {
        return std::make_unique<CdvdFsvService>(host);
    }
}

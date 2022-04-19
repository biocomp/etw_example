#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <optional>

struct _GUID;
using GUID = _GUID;

namespace EtwLog
{
    __declspec(noreturn) void VerifyHResult(std::uint32_t hresult, std::string_view additionalInfo, std::optional<std::uint32_t> expectedGoodResult = {});

    class MiniLog
    {
    public:
        MiniLog(std::string_view sessionName, std::string_view outputFolder, std::size_t bufferSize);
        ~MiniLog();

        void operator()(std::span<std::byte> message) const;

        const GUID& GetProviderId() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // EtwLog
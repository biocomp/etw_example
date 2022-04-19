#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <optional>

namespace EtwLog
{
    void VerifyHResult(std::uint32_t hresult, std::string_view additionalInfo, std::uint32_t expectedGoodResult);

    class MiniLog
    {
    public:
        /// @brief Constructs the event provider, event session and enables the provider inside the session.
        /// @param sessionName - unique name of the ETW session created internally.
        /// @param outputFolder
        /// @param bufferSize - Kilobytes of memory allocated for each event tracing session buffer.
        MiniLog(
            const char* sessionName, 
            std::string_view outputFolder, 
            std::size_t bufferSize);
        ~MiniLog();

        MiniLog(MiniLog&&) noexcept;
        MiniLog& operator=(MiniLog&&) noexcept;

        /// @brief Uses EventWrite API to write the \a message into the provider.
        /// @param message 
        void operator()(std::span<const std::byte> message) const;

    private:
        class Impl;

        /// @brief Using PIMPL idiom to not require Windows.h with this header.
        std::unique_ptr<Impl> m_impl;
    };
} // EtwLog
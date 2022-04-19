#include "pch.h"
#include "MiniEtwLog.h"


#include <Windows.h>
#include <combaseapi.h>
#include <evntrace.h>
#include <evntprov.h>

#include <cassert>
#include <algorithm>
#include <optional>
#include <system_error>
#include <array>

using EtwLog::MiniLog;


void EtwLog::VerifyHResult(std::uint32_t hresult, std::string_view additionalInfo, std::uint32_t expectedGoodResult) {
    if (hresult != expectedGoodResult) {
        std::error_code error{static_cast<int>(hresult), std::system_category()};

        // If error was not properly formatted.
        if (error.message() == std::error_code{}.message()) {
            std::array<char, 512> errStr;
            if (::FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr,
                hresult,
                0u,
                errStr.data(),
                static_cast<DWORD>(errStr.size()), nullptr) != 0) {
                    throw std::system_error{error, std::string{ errStr.data() } + std::string{additionalInfo}};
            }
        }

        throw std::system_error{error, std::string{additionalInfo}};
    }
}

namespace
{
    using EtwLog::VerifyHResult;

    GUID MakeGuid() {
        GUID guid;
        VerifyHResult(::CoCreateGuid(&guid), "CoCreateGuid failed", S_OK);
        return guid;
    }

    namespace Controllers {
        /// @brief EVENT_TRACE_PROPERTIES has weird requirements that session name and log file path 
        /// buffers are located after this structure in memory, and their offsets are specified instead of actual strings.
        /// This structure helps to handle that.
        struct EventTracePropertiesWithBuffers {
            EventTracePropertiesWithBuffers(const GUID& sessionId, std::size_t bufferSize, std::string_view logFilePath) {
                ::ZeroMemory(this, sizeof(EventTracePropertiesWithBuffers));

                Properties.Wnode.BufferSize = sizeof(EventTracePropertiesWithBuffers);
                Properties.LoggerNameOffset = offsetof(EventTracePropertiesWithBuffers, SessionName);
                Properties.LogFileNameOffset = offsetof(EventTracePropertiesWithBuffers, LogFilePath);

                Properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
                Properties.Wnode.ClientContext = 1; //QPC clock resolution
                //Properties.Wnode.Guid = sessionId;
                
                Properties.LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;

                // Check if the buffer size is set correct.
                static constexpr std::size_t c_maxBufferSize{16384};
                Properties.BufferSize = static_cast<ULONG>(std::clamp<std::size_t>(bufferSize, 0, c_maxBufferSize));

                SetLogFilePath(logFilePath);
            }

            void SetLogFilePath(std::string_view logFilePath) {
                assert(logFilePath.size() <= std::extent<decltype(LogFilePath)>::value);
                std::copy(logFilePath.begin(), logFilePath.end(), std::begin(LogFilePath));
            }

            EVENT_TRACE_PROPERTIES Properties;
            char SessionName[256]; // Arbitrary max size for the buffer
            char LogFilePath[1024]; // Arbitrary max size for the buffer
        };

        /// @brief RAII wrapper around enabling/disabling provider for the session.
        class EnabledProvider {
        public:
            EnabledProvider(TRACEHANDLE sessionHandle, const GUID& providerIdToEnable) : SessionHandle{sessionHandle}, EnabledProviderId{providerIdToEnable} {
                VerifyHResult(::EnableTraceEx2(
                        SessionHandle,
                        &EnabledProviderId,
                        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                        TRACE_LEVEL_INFORMATION,
                        0,
                        0,
                        0,
                        NULL
                    ), 
                    "EnableTraceEx2 - enabling provider", 
                    ERROR_SUCCESS);
            }

            ~EnabledProvider() {
                ::EnableTraceEx2(
                    SessionHandle,
                    &EnabledProviderId,
                    EVENT_CONTROL_CODE_DISABLE_PROVIDER,
                    TRACE_LEVEL_INFORMATION,
                    0,
                    0,
                    0,
                    NULL
                );
            }

        private:
            TRACEHANDLE SessionHandle;
            const GUID& EnabledProviderId;
        };

        /// @brief Event session created by controller.
        /// In this case, the same app that produces the events is the controller as well.
        class Session {
        public:
            Session(const GUID& sessionId, const char* sessionName, std::string_view logFileName, std::size_t bufferSize) : m_properties(sessionId, bufferSize, logFileName) {
                assert(strlen(sessionName) <= std::extent<decltype(m_properties.SessionName)>::value);

                // Creates the session
                auto result{::StartTraceA(&Handle, sessionName, &m_properties.Properties)};

                // If session is already present, close it and restart.
                // Sessions are a limited system-wide resource, so creating a new one (with, say, unique suffix added to the base name) is not advised.
                if (result == ERROR_ALREADY_EXISTS) {
                    DestroySession(sessionName);
                    result = ::StartTraceA(&Handle, sessionName, &m_properties.Properties);
                }
                
                VerifyHResult(result, "StartTrace", ERROR_SUCCESS);
            }

            EnabledProvider EnableProvider(const GUID& providerId) const {
                return EnabledProvider{Handle, providerId};
            }

            ~Session() {
                // Destroys the session
                DestroySession(nullptr);
            }

            TRACEHANDLE Handle{0};

        private:
            void DestroySession(const char* sessionName) {
                ::ControlTraceA(Handle, sessionName, &m_properties.Properties, EVENT_TRACE_CONTROL_STOP);
            }

            EventTracePropertiesWithBuffers m_properties;
        };
    }

    namespace Providers {
        /// @brief Event provider
        class Provider {
        public:
            Provider(const GUID& providerId) {
                VerifyHResult(::EventRegister(&providerId, nullptr, nullptr, &Handle), "EventRegister", ERROR_SUCCESS);
            }

            ~Provider() { ::EventUnregister(Handle); }
            REGHANDLE Handle;
        };
    }
}

class EtwLog::MiniLog::Impl {
public:
    Impl(const char* sessionName, std::string_view outputFolder, std::size_t bufferSize) :
        m_session{m_sessionId, std::string{sessionName}.c_str(), std::string{outputFolder} + "\\log.etl", bufferSize},
        m_enabledProvider{m_session.EnableProvider(m_providerId)},
        m_provider{m_providerId}
    {}

    void Write(std::span<std::byte> message) const {
        constexpr static const EVENT_DESCRIPTOR c_descriptor = {
           0x1,    // Id
           0x1,    // Version
           0x0,    // Channel
           0x0,    // LevelSeverity
           0x0,    // Opcode
           0x0,    // Task
           0x0,    // Keyword
        };

        EVENT_DATA_DESCRIPTOR eventDataDescriptors[1];
        EventDataDescCreate(&eventDataDescriptors[0], message.data(), static_cast<ULONG>(message.size()));

        VerifyHResult(::EventWrite(m_provider.Handle, &c_descriptor, 1, eventDataDescriptors), "EventWrite", ERROR_SUCCESS);
    }

    const GUID& GetProviderId() const noexcept { return m_providerId; }

private:
    const GUID m_sessionId{MakeGuid()};
    const GUID m_providerId{MakeGuid()};

    /// @brief Create ETW session
    Controllers::Session m_session;

    /// @brief Enable the provider with m_providerId in it.
    Controllers::EnabledProvider m_enabledProvider;


    /// @brief Create the provider and use it for event logging.
    /// @note: It seems we need to create the provider AFTER the session was created and the provider was enabled in it. 
    /// Otherwise, the events would not reach the log file until extra ::ControlTrace is called for the session.
    Providers::Provider m_provider;
};

EtwLog::MiniLog::MiniLog(const char* sessionName, std::string_view outputFolder, std::size_t bufferSize) : m_impl{std::make_unique<Impl>(sessionName, outputFolder, bufferSize)} {}
EtwLog::MiniLog::~MiniLog() = default;

void EtwLog::MiniLog::operator()(std::span<std::byte> message) const { m_impl->Write(message); }
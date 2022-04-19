#include "MiniEtwLog.h"

#include <iostream>
#include <array>
#include <filesystem>
#include <functional>
#include <random>
#include <format>
#include <cstdio>

#define INITGUID
#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>

namespace Consumers {
    struct AutoTraceHandle {
        AutoTraceHandle(TRACEHANDLE trace) : Trace{trace} {
            if (Trace == INVALID_PROCESSTRACE_HANDLE) {
                throw std::invalid_argument{"Trace is invalid"};
            }
        }

        ~AutoTraceHandle() { ::CloseTrace(Trace); }

        AutoTraceHandle(const AutoTraceHandle&) = delete;
        AutoTraceHandle& operator=(const AutoTraceHandle&) = delete;

        TRACEHANDLE Trace;
    };

    struct EventHandler {
        EventHandler(std::function<void(const EVENT_RECORD&)> callback) 
            : 
            m_callback{std::move(callback)} 
        {}

        EventHandler(const EventHandler&) = delete;
        EventHandler& operator=(const EventHandler&) = delete;

        void* Context{this};
        PEVENT_RECORD_CALLBACK Callback = CallbackImpl;

    private:
        static void CallbackImpl(EVENT_RECORD* evt) {
            static_cast<EventHandler*>(evt->UserContext)->m_callback(*evt);
        }

        std::function<void(const EVENT_RECORD&)> m_callback;
    };

    std::vector<std::vector<std::byte>> ReadRecords(const std::filesystem::path& file) {
        std::vector<std::vector<std::byte>> results;
        EVENT_TRACE_LOGFILEA traceFile;

        EventHandler handler{[&results](const EVENT_RECORD& evt) {
            // Skip metadata records with predefined EventTraceGuid guid.
            if (::IsEqualGUID(evt.EventHeader.ProviderId, EventTraceGuid) == 0) {
                const std::byte* data{static_cast<const std::byte*>(evt.UserData)};
                const auto size{evt.UserDataLength};
                results.emplace_back(data, data + size);
            }
        }};

        const auto narrowString{file.string()};
        ::ZeroMemory(&traceFile, sizeof(traceFile));
        traceFile.LogFileName = const_cast<char*>(narrowString.c_str());
        traceFile.EventRecordCallback = handler.Callback;
        traceFile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
        traceFile.Context = handler.Context;

        FILETIME startTime;
        ::ZeroMemory(&startTime, sizeof(startTime));

        FILETIME currentTime;
        SYSTEMTIME st;

        ::GetSystemTime(&st);
        ::SystemTimeToFileTime(&st, &currentTime);

        AutoTraceHandle trace{::OpenTraceA(&traceFile)};
        EtwLog::VerifyHResult(::ProcessTrace(&trace.Trace, 1, &startTime, &currentTime), "ProcessTrace", ERROR_SUCCESS);
        return results;
    }
}

struct Fixture
{
    Fixture() {
        std::filesystem::remove_all(std::filesystem::current_path() / "temp_out");
        std::filesystem::create_directories(TempFolder);
    }

    ~Fixture() {
        std::filesystem::remove_all(TempFolder);
    }

    std::filesystem::path TempFolder{std::filesystem::current_path() / "temp_out" / std::to_string(std::random_device{}())};
};

template <typename... TArgs>
void Format(std::string_view message, TArgs&&... args) {
    std::printf("%s", std::format(message, std::forward<TArgs>(args)...).c_str());
}

template <typename... TArgs>
void Error(std::string_view message, TArgs&&... args) {
    Format("## Error: {}", std::format(message, std::forward<TArgs>(args)...));
}

int main() {
    try {
        const Fixture fixture;

        // Make sure log dies before reading the messages 
        {
            EtwLog::MiniLog log{"Mini logger", fixture.TempFolder.string(), 4};
    
            static constexpr char hello[]{"Hello World!"};
            std::array<std::byte, sizeof(hello)> helloBytes;
            std::memcpy(helloBytes.data(), hello, sizeof(hello));
            log(helloBytes);
        }

        const auto records{Consumers::ReadRecords(fixture.TempFolder / "log.etl")};
        if (records.size() != 1) {
            Error("Found {} records instead of 1", records.size());
        }

        const auto oneRecord{std::string{reinterpret_cast<const char*>(records[0].data()), records[0].size() - 1}};
        if (oneRecord != "Hello World!") {
            Error("Found one record, with unexpected value '{}'", oneRecord);
        } else {
            Format("Found one record, as expected, with value '{}'", oneRecord);
        }
    }
    catch (const std::exception& e) {
        Error("Failed with {}", e.what());
    }
}
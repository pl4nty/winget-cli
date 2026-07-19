// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "Public/AppInstallerFileLogger.h"

#include "Public/AppInstallerRuntime.h"
#include "Public/AppInstallerStrings.h"
#include "Public/AppInstallerDateTime.h"
#include "Public/AppInstallerTelemetry.h"
#include "Public/winget/UserSettings.h"
#include "Public/winget/ThreadGlobals.h"
#include <winget/Filesystem.h>
#include <corecrt_io.h>


namespace AppInstaller::Logging
{
    using namespace std::string_view_literals;
    using namespace std::chrono_literals;

    namespace
    {
        static constexpr std::string_view s_fileLoggerDefaultFilePrefix = "WinGet"sv;
        static constexpr std::string_view s_fileLoggerDefaultFileExt = ".log"sv;

        // Send to a string first to create a single block to write to a file.
        std::string ToLogLine(Channel channel, Level level, std::string_view message)
        {
            std::stringstream strstr;
            strstr << std::chrono::system_clock::now() << " <" << GetLevelChar(level) << "> [" << std::setw(GetMaxChannelNameLength()) << std::left << std::setfill(' ') << GetChannelName(channel) << "] " << message;
            return std::move(strstr).str();
        }

        // The token that ends the message portion of a CCM log entry, and the benign replacement
        // written when a message happens to contain it. The format has no official escape, so the
        // token can never be allowed to appear inside the message portion.
        static constexpr std::string_view s_ccmLogEndToken = "]LOG]!>"sv;
        static constexpr std::string_view s_ccmLogEndTokenReplacement = "|LOG|!>"sv;

        // Formats a log line in CCM (CMTrace-compatible) format; the format understood by the
        // CMTrace and OneTrace log viewers. There is no official specification; the field layout
        // matches the entries written by Configuration Manager clients:
        //   <![LOG[message]LOG]!><time="HH:mm:ss.fff+bias" date="M-d-yyyy" component="..." context="..." type="N" thread="id" file="">
        std::string ToCCMLogLine(Channel channel, Level level, std::string_view message)
        {
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            tm localTime{};
            _localtime64_s(&localTime, &tt);

            auto sinceEpoch = now.time_since_epoch();
            auto leftoverMillis = std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch) - std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch);

            // CMTrace expects the offset of UTC from local time in minutes (positive means west of UTC),
            // matching TIME_ZONE_INFORMATION.Bias adjusted for daylight saving time.
            TIME_ZONE_INFORMATION timeZoneInfo{};
            DWORD timeZoneResult = GetTimeZoneInformation(&timeZoneInfo);
            long biasMins = timeZoneInfo.Bias;
            if (timeZoneResult == TIME_ZONE_ID_DAYLIGHT)
            {
                biasMins += timeZoneInfo.DaylightBias;
            }
            else if (timeZoneResult == TIME_ZONE_ID_STANDARD)
            {
                biasMins += timeZoneInfo.StandardBias;
            }

            // CCM type: 1=Info/Verbose, 2=Warning, 3=Error/Critical
            int type;
            switch (level)
            {
            case Level::Warning: type = 2; break;
            case Level::Error:
            case Level::Crit:   type = 3; break;
            default:            type = 1; break;
            }

            // The end token has no official escape; replace it so that a message can never terminate the entry early.
            std::string escapedMessage{ message };
            Utility::FindAndReplace(escapedMessage, s_ccmLogEndToken, s_ccmLogEndTokenReplacement);

            // Use the current activity as the context when available.
            const GUID* activityId = nullptr;
            if (auto threadGlobals = ThreadLocalStorage::ThreadGlobals::GetForCurrentThread())
            {
                activityId = reinterpret_cast<TelemetryTraceLogger*>(threadGlobals->GetTelemetryObject())->GetActivityId();
            }

            std::stringstream strstr;
            strstr << "<![LOG[" << escapedMessage << "]LOG]!>"
                << "<time=\""
                << std::setw(2) << std::setfill('0') << localTime.tm_hour << ":"
                << std::setw(2) << std::setfill('0') << localTime.tm_min << ":"
                << std::setw(2) << std::setfill('0') << localTime.tm_sec << "."
                << std::setw(3) << std::setfill('0') << leftoverMillis.count()
                << (biasMins < 0 ? "-" : "+") << (biasMins < 0 ? -biasMins : biasMins) << "\""
                << " date=\""
                << std::setw(2) << std::setfill('0') << (1 + localTime.tm_mon) << "-"
                << std::setw(2) << std::setfill('0') << localTime.tm_mday << "-"
                << (1900 + localTime.tm_year) << "\""
                << " component=\"" << GetChannelName(channel) << "\""
                << " context=\"";
            if (activityId)
            {
                strstr << *activityId;
            }
            strstr << "\""
                << " type=\"" << type << "\""
                << " thread=\"" << GetCurrentThreadId() << "\""
                << " file=\"\">";
            return std::move(strstr).str();
        }

        // Determines the difference between the given position and the maximum as an offset.
        std::ofstream::off_type CalculateDiff(const std::ofstream::pos_type& position, std::ofstream::off_type maximum)
        {
            auto offsetPosition = static_cast<std::ofstream::off_type>(position);
            return maximum > offsetPosition ? maximum - offsetPosition : 0;
        }
    }

    FileLogger::FileLogger() : FileLogger(s_fileLoggerDefaultFilePrefix) {}

    FileLogger::FileLogger(const std::filesystem::path& filePath)
    {
        m_name = GetNameForPath(filePath);
        m_filePath = filePath;
        InitializeDefaultMaximumFileSize();
        OpenFileLoggerStream();
    }

    FileLogger::FileLogger(const std::string_view fileNamePrefix)
    {
        m_name = "file";
        m_filePath = Runtime::GetPathTo(Runtime::PathName::DefaultLogLocation);
        m_filePath /= fileNamePrefix.data() + ('-' + Utility::GetCurrentTimeForFilename() + s_fileLoggerDefaultFileExt.data());
        InitializeDefaultMaximumFileSize();
        OpenFileLoggerStream();
    }

    FileLogger::~FileLogger()
    {
        m_stream.flush();
        // When std::ofstream is constructed from an existing File handle, it does not call fclose on destruction
        // Only calling close() explicitly will close the file handle.
        m_stream.close();
    }

    FileLogger& FileLogger::SetMaximumSize(std::ofstream::off_type maximumSize)
    {
        THROW_HR_IF(E_INVALIDARG, maximumSize < 0);
        m_maximumSize = maximumSize;
        return *this;
    }

    std::string FileLogger::GetNameForPath(const std::filesystem::path& filePath)
    {
        using namespace std::string_literals;
        return "file :: "s + filePath.u8string();
    }

    std::string_view FileLogger::DefaultPrefix()
    {
        return s_fileLoggerDefaultFilePrefix;
    }

    std::string_view FileLogger::DefaultExt()
    {
        return s_fileLoggerDefaultFileExt;
    }

    std::string FileLogger::GetName() const
    {
        return m_name;
    }

    void FileLogger::Write(Channel channel, Level level, std::string_view message) noexcept try
    {
        WriteDirect(channel, level, ToLogLine(channel, level, message));
    }
    catch (...) {}

    void FileLogger::WriteDirect(Channel, Level, std::string_view message) noexcept try
    {
        HandleMaximumFileSize(message);
        m_stream << message << std::endl;
    }
    catch (...) {}

    void FileLogger::SetTag(Tag tag) noexcept try
    {
        if (tag == Tag::HeadersComplete)
        {
            auto currentPosition = m_stream.tellp();
            if (currentPosition != std::ofstream::pos_type{ -1 })
            {
                m_headersEnd = currentPosition;
            }
        }
    }
    catch (...) {}

    std::unique_ptr<FileLogger> FileLogger::Create()
    {
        if (Settings::User().Get<Settings::Setting::LoggingFormat>() == LogFileFormat::CCM)
        {
            return std::make_unique<CCMFileLogger>();
        }

        return std::make_unique<FileLogger>();
    }

    std::unique_ptr<FileLogger> FileLogger::Create(const std::filesystem::path& filePath)
    {
        if (Settings::User().Get<Settings::Setting::LoggingFormat>() == LogFileFormat::CCM)
        {
            return std::make_unique<CCMFileLogger>(filePath);
        }

        return std::make_unique<FileLogger>(filePath);
    }

    std::unique_ptr<FileLogger> FileLogger::Create(std::string_view fileNamePrefix)
    {
        if (Settings::User().Get<Settings::Setting::LoggingFormat>() == LogFileFormat::CCM)
        {
            return std::make_unique<CCMFileLogger>(fileNamePrefix);
        }

        return std::make_unique<FileLogger>(fileNamePrefix);
    }

    void FileLogger::Add()
    {
        Log().AddLogger(Create());
    }

    void FileLogger::Add(const std::filesystem::path& filePath)
    {
        Log().AddLogger(Create(filePath));
    }

    void FileLogger::Add(std::string_view fileNamePrefix)
    {
        Log().AddLogger(Create(fileNamePrefix));
    }

    void FileLogger::BeginCleanup()
    {
        BeginCleanup(Runtime::GetPathTo(Runtime::PathName::DefaultLogLocation));
    }

    void FileLogger::BeginCleanup(const std::filesystem::path& filePath)
    {
        std::thread([filePath]()
            {
                try
                {
                    const auto& settings = Settings::User();

                    Filesystem::FileLimits fileLimits;
                    fileLimits.Age = settings.Get<Settings::Setting::LoggingFileAgeLimitInDays>();
                    fileLimits.TotalSizeInMB = settings.Get<Settings::Setting::LoggingFileTotalSizeLimitInMB>();
                    fileLimits.Count = settings.Get<Settings::Setting::LoggingFileCountLimit>();

                    auto filesInPath = Filesystem::GetFileInfoFor(filePath);
                    Filesystem::FilterToFilesExceedingLimits(filesInPath, fileLimits);

                    for (const auto& file : filesInPath)
                    {
                        std::filesystem::remove(file.Path);
                    }
                }
                // Just throw out everything
                catch (...) {}
            }).detach();
    }

    void FileLogger::OpenFileLoggerStream() 
    {
        // Prevent other writers to our log file, but allow readers
        FILE* filePtr = _wfsopen(m_filePath.wstring().c_str(), L"w", _SH_DENYWR);

        if (filePtr)
        {
            auto closeFile = wil::scope_exit([&]() { fclose(filePtr); });

            // Prevent inheritance to ensure log file handle is not opened by other processes
            THROW_IF_WIN32_BOOL_FALSE(SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(filePtr))), HANDLE_FLAG_INHERIT, 0));

            m_stream = std::ofstream{ filePtr };
            m_filePtr = filePtr;
            closeFile.release();
        }
        else
        {
            AICLI_LOG(Core, Error, << "Failed to open log file " << m_filePath.u8string());
            throw std::system_error(errno, std::generic_category());
        }
    }

    void FileLogger::InitializeDefaultMaximumFileSize()
    {
        m_maximumSize = static_cast<std::ofstream::off_type>(Settings::User().Get<Settings::Setting::LoggingFileIndividualSizeLimitInMB>()) << 20;
    }

    void FileLogger::HandleMaximumFileSize(std::string_view& currentLog)
    {
        if (m_maximumSize == 0)
        {
            return;
        }

        auto maximumLogSize = static_cast<size_t>(CalculateDiff(m_headersEnd, m_maximumSize));

        // In the event that a single log is larger than the maximum
        if (currentLog.size() > maximumLogSize)
        {
            currentLog = currentLog.substr(0, maximumLogSize);
            WrapLogFile();
            return;
        }

        auto currentPosition = m_stream.tellp();
        if (currentPosition == std::ofstream::pos_type{ -1 })
        {
            // The expectation is that if the stream is in an error state the write won't actually happen.
            return;
        }

        auto availableSpace = static_cast<size_t>(CalculateDiff(currentPosition, m_maximumSize));

        if (currentLog.size() > availableSpace)
        {
            WrapLogFile();
            return;
        }
    }

    void FileLogger::TruncateToHeadersEnd()
    {
        m_stream.flush();

        if (m_filePtr)
        {
            fflush(m_filePtr);
            _chsize_s(_fileno(m_filePtr), static_cast<long long>(static_cast<std::ofstream::off_type>(m_headersEnd)));
        }

        m_stream.seekp(m_headersEnd);
    }

    void FileLogger::WrapLogFile()
    {
        m_stream.seekp(m_headersEnd);
        // Yes, we may go over the size limit slightly due to this and the unaccounted for newlines
        m_stream << ToLogLine(Channel::Core, Level::Info, "--- log file has wrapped ---") << std::endl;
    }

    CCMFileLogger::CCMFileLogger() : FileLogger() {}

    CCMFileLogger::CCMFileLogger(const std::filesystem::path& filePath) : FileLogger(filePath) {}

    CCMFileLogger::CCMFileLogger(const std::string_view fileNamePrefix) : FileLogger(fileNamePrefix) {}

    void CCMFileLogger::Write(Channel channel, Level level, std::string_view message) noexcept try
    {
        WriteDirect(channel, level, ToCCMLogLine(channel, level, message));
    }
    catch (...) {}

    void CCMFileLogger::WrapLogFile()
    {
        // Overwriting in place would leave stale partial entries after the write position,
        // which parsers of the structured format would see as corrupt XML fragments.
        // Truncate back to the headers instead so that the file only ever contains whole entries.
        TruncateToHeadersEnd();
        m_stream << ToCCMLogLine(Channel::Core, Level::Info, "--- log file has wrapped ---") << std::endl;
    }
}

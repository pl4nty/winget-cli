// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include <AppInstallerLogging.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace AppInstaller::Logging
{
    // Logs to a file.
    struct FileLogger : public ILogger
    {
        FileLogger();
        explicit FileLogger(const std::filesystem::path& filePath);
        explicit FileLogger(const std::string_view fileNamePrefix);

        ~FileLogger();

        FileLogger(const FileLogger&) = delete;
        FileLogger& operator=(const FileLogger&) = delete;

        FileLogger(FileLogger&&) = default;
        FileLogger& operator=(FileLogger&&) = default;

        // The default value for the maximum size comes from settings.
        // Setting the maximum size to 0 will disable the maximum.
        FileLogger& SetMaximumSize(std::ofstream::off_type maximumSize);

        static std::string GetNameForPath(const std::filesystem::path& filePath);

        static std::string_view DefaultPrefix();
        static std::string_view DefaultExt();

        // ILogger
        std::string GetName() const override;

        void Write(Channel channel, Level level, std::string_view message) noexcept override;

        void WriteDirect(Channel channel, Level level, std::string_view message) noexcept override;

        void SetTag(Tag tag) noexcept override;

        // Creates a file logger whose type is determined by the logging format user setting.
        static std::unique_ptr<FileLogger> Create();
        static std::unique_ptr<FileLogger> Create(const std::filesystem::path& filePath);
        static std::unique_ptr<FileLogger> Create(std::string_view fileNamePrefix);

        // Adds a file logger, whose type is determined by the logging format user setting, to the current Log
        static void Add();
        static void Add(const std::filesystem::path& filePath);
        static void Add(std::string_view fileNamePrefix);

        // Starts a background task to clean up old log files.
        static void BeginCleanup();
        static void BeginCleanup(const std::filesystem::path& filePath);

    protected:
        std::ofstream m_stream;

        // Truncates the file so that it ends at the headers section, leaving the stream positioned there.
        // Used when wrapping a format that cannot tolerate stale partial entries after the write position.
        void TruncateToHeadersEnd();

        // Resets the log file state so that it will overwrite the data portion.
        virtual void WrapLogFile();

    private:
        std::string m_name;
        std::filesystem::path m_filePath;
        FILE* m_filePtr = nullptr;
        std::ofstream::pos_type m_headersEnd = 0;
        std::ofstream::off_type m_maximumSize = 0;

        void OpenFileLoggerStream();

        // Initializes the default maximum file size.
        void InitializeDefaultMaximumFileSize();

        // Determines if the logger needs to wrap back to the beginning, doing so when needed.
        // May also shrink the given view if it exceeds the overall maximum.
        void HandleMaximumFileSize(std::string_view& currentLog);
    };

    // Logs to a file in the CCM (CMTrace-compatible) format.
    // Each entry is written as:
    //   <![LOG[message]LOG]!><time="HH:mm:ss.fff+bias" date="M-d-yyyy" component="channel" context="activity id" type="N" thread="id" file="">
    // which can be viewed with the CMTrace and OneTrace log viewers:
    // https://learn.microsoft.com/mem/configmgr/core/support/cmtrace
    struct CCMFileLogger : public FileLogger
    {
        CCMFileLogger();
        explicit CCMFileLogger(const std::filesystem::path& filePath);
        explicit CCMFileLogger(const std::string_view fileNamePrefix);

        void Write(Channel channel, Level level, std::string_view message) noexcept override;

    protected:
        void WrapLogFile() override;
    };
}

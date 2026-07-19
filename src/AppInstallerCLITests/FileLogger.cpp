// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "TestCommon.h"
#include "TestSettings.h"
#include "TestHooks.h"
#include <AppInstallerFileLogger.h>
#include <AppInstallerStrings.h>

#include <winrt/Windows.Data.Xml.Dom.h>

#include <cstdio>
#include <ctime>

using namespace AppInstaller::Logging;
using namespace AppInstaller::Utility;
using namespace TestCommon;


std::string GetHeaderString()
{
    return "TIME [CHAN] Header Message";
}

std::string GetLargeString()
{
    return "[===|Clearly defined start to large string|===]\r\n"
        "While this string does not need to be particularly unique, it is still good if it is not easily duplicated by any other random set of data.\r\n"
        "It should also end in a character that is not used in any other way within these tests, so please don't include that character when writing tests.\r\n"
        "That character is &";
}

namespace
{
#define WINGET_DEFINE_STRING_ENUM(_enum_,_value_) constexpr std::string_view _enum_##_##_value_ = #_value_##sv

    WINGET_DEFINE_STRING_ENUM(TagState, Unset);
    WINGET_DEFINE_STRING_ENUM(TagState, SetAtStart);
    WINGET_DEFINE_STRING_ENUM(TagState, SetAfterLogging);

    WINGET_DEFINE_STRING_ENUM(MaximumSizeState, Zero);
    WINGET_DEFINE_STRING_ENUM(MaximumSizeState, SmallerThanLargeString);
    WINGET_DEFINE_STRING_ENUM(MaximumSizeState, EqualToLargeString);
    WINGET_DEFINE_STRING_ENUM(MaximumSizeState, SlightlyLargerThanLargeString);
    WINGET_DEFINE_STRING_ENUM(MaximumSizeState, MuchLargerThanLargeString);

    constexpr std::string_view WrapIndicator = "--- log file has wrapped ---"sv;
    // The amount of extra size that is allowed (total indicator size + newline + newlines from test strings)
    constexpr size_t ExtraAllowedSize = 72;

    constexpr size_t NewLineCharacterCount = 2;
    constexpr size_t SmallDifferenceSize = 10;
    constexpr AppInstaller::Logging::Channel DefaultChannel = AppInstaller::Logging::Channel::Core;
    constexpr AppInstaller::Logging::Level DefaultLevel = AppInstaller::Logging::Level::Info;

    void ValidateFileContents(const std::filesystem::path& file, const std::vector<std::string_view>& expectedContents, size_t maximumSize)
    {
        std::ifstream fileStream{ file, std::ios::binary };
        auto fileContents = ReadEntireStream(fileStream);
        std::string_view fileContentsView = fileContents;

        std::string fileContentsCopy = fileContents;
        FindAndReplace(fileContentsCopy, "\r", "\\r");
        FindAndReplace(fileContentsCopy, "\n", "\\n");
        INFO("File contents:\n" << fileContentsCopy);

        if (maximumSize)
        {
            REQUIRE(maximumSize + ExtraAllowedSize >= fileContents.size());
        }

        size_t currentPosition = 0;
        for (std::string_view expectedContent : expectedContents)
        {
            REQUIRE(currentPosition < fileContents.size());

            if (expectedContent == WrapIndicator)
            {
                auto endLinePosition = fileContentsView.find('\n', currentPosition);
                REQUIRE(endLinePosition != -1);
                REQUIRE(endLinePosition >= expectedContent.size() + NewLineCharacterCount);
                auto actualContent = fileContentsView.substr(endLinePosition + 1 - expectedContent.size() - NewLineCharacterCount, expectedContent.size());
                REQUIRE(expectedContent == actualContent);
                currentPosition = endLinePosition + 1;
            }
            else
            {
                auto actualContent = fileContentsView.substr(currentPosition, expectedContent.size());
                REQUIRE(expectedContent == actualContent);
                currentPosition += expectedContent.size() + NewLineCharacterCount;
            }
        }
    }

    void FileLogger_MaximumSize_Test(std::string_view tagState, std::string_view sizeState)
    {
        auto headerString = GetHeaderString();
        auto largeString = GetLargeString();

        // Determine maximum size
        size_t maximumSize = 0;

        if (sizeState == MaximumSizeState_SmallerThanLargeString)
        {
            maximumSize = largeString.size() - SmallDifferenceSize;
        }
        else if (sizeState == MaximumSizeState_EqualToLargeString)
        {
            maximumSize = largeString.size();
        }
        else if (sizeState == MaximumSizeState_SlightlyLargerThanLargeString)
        {
            maximumSize = largeString.size() + SmallDifferenceSize;
        }
        else if (sizeState == MaximumSizeState_MuchLargerThanLargeString)
        {
            maximumSize = largeString.size() * 2;
        }

        INFO("Tag State: " << tagState << ", Size State: " << sizeState << "[" << maximumSize << "]");

        TempFile tempFile{ "FileLogger_MaximumSize", ".log" };
        FileLogger logger{ tempFile };

        INFO("File: " << tempFile.GetPath().u8string());

        logger.SetMaximumSize(wil::safe_cast<std::ofstream::off_type>(maximumSize));

        // Set tag and log strings
        size_t tagPosition = 0;
        if (tagState == TagState_SetAtStart)
        {
            logger.SetTag(Tag::HeadersComplete);
        }

        logger.WriteDirect(DefaultChannel, DefaultLevel, headerString);

        if (tagState == TagState_SetAfterLogging)
        {
            logger.SetTag(Tag::HeadersComplete);
            tagPosition = headerString.size() + NewLineCharacterCount;
        }

        // Due to text output in the logger, log with \n only
        std::string largeStringWithoutCarriageReturn = largeString;
        FindAndReplace(largeStringWithoutCarriageReturn, "\r\n", "\n");

        logger.WriteDirect(DefaultChannel, DefaultLevel, largeStringWithoutCarriageReturn);

        // Calculate current state
        size_t maximumAvailableSpace = std::numeric_limits<size_t>::max();
        size_t currentAvailableSpace = std::numeric_limits<size_t>::max();
        if (maximumSize)
        {
            maximumAvailableSpace = maximumSize - tagPosition;
            currentAvailableSpace = maximumSize - headerString.size() - NewLineCharacterCount;
        }

        bool shouldWrap = largeString.size() > currentAvailableSpace;

        INFO("Maximum Available: " << maximumAvailableSpace << ", Current Available: " << currentAvailableSpace << ", ShouldWrap: " << shouldWrap);

        std::vector<std::string_view> expectedFileContents;

        if (tagPosition || !shouldWrap)
        {
            expectedFileContents.push_back(headerString);
        }

        if (shouldWrap)
        {
            expectedFileContents.push_back(WrapIndicator);
        }

        std::string_view largeStringView = largeString;
        expectedFileContents.push_back(largeStringView.substr(0, std::min(largeString.size(), maximumAvailableSpace)));

        ValidateFileContents(tempFile, expectedFileContents, maximumSize);

        // Log again
        INFO("Second time logging large string");
        logger.WriteDirect(DefaultChannel, DefaultLevel, largeStringWithoutCarriageReturn);

        // The maximum size is twice the large log, so anything with a limit will wrap
        shouldWrap = maximumSize != 0;

        expectedFileContents.clear();

        if (tagPosition || !shouldWrap)
        {
            expectedFileContents.push_back(headerString);
        }

        if (shouldWrap)
        {
            expectedFileContents.push_back(WrapIndicator);
        }
        else
        {
            expectedFileContents.push_back(largeStringView);
        }

        expectedFileContents.push_back(largeStringView.substr(0, std::min(largeString.size(), maximumAvailableSpace)));

        ValidateFileContents(tempFile, expectedFileContents, maximumSize);
    }
}

TEST_CASE("FileLogger_MaximumSize", "[logging]")
{
    auto tagState = GENERATE(TagState_Unset, TagState_SetAtStart, TagState_SetAfterLogging);
    auto sizeState = GENERATE(MaximumSizeState_Zero, MaximumSizeState_SmallerThanLargeString, MaximumSizeState_EqualToLargeString, MaximumSizeState_SlightlyLargerThanLargeString, MaximumSizeState_MuchLargerThanLargeString);
    FileLogger_MaximumSize_Test(tagState, sizeState);
}

namespace
{
    constexpr std::string_view CCMLogStartToken = "<![LOG["sv;
    constexpr std::string_view CCMLogEndToken = "]LOG]!>"sv;

    // The parsed fields of a single CCM format log entry.
    struct CCMLogEntry
    {
        std::string Message;
        std::string Time;
        std::string Date;
        std::string Component;
        std::string Context;
        std::string Type;
        std::string Thread;
        std::string File;
    };

    // Splits file contents into individual CCM entries; a message may span multiple lines so split on the entry start token.
    std::vector<std::string> SplitCCMEntries(const std::string& fileContents)
    {
        std::vector<std::string> result;
        size_t position = 0;

        // Anything before the first entry (or after the last) that isn't whitespace will end up
        // attached to an entry and fail parsing, so leading content must be whitespace only.
        size_t firstEntry = fileContents.find(CCMLogStartToken);
        REQUIRE(fileContents.find_first_not_of("\r\n \t") == (fileContents.empty() ? std::string::npos : firstEntry));

        position = firstEntry;
        while (position != std::string::npos)
        {
            size_t next = fileContents.find(CCMLogStartToken, position + CCMLogStartToken.size());
            std::string entry = fileContents.substr(position, (next == std::string::npos ? fileContents.size() : next) - position);

            while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r'))
            {
                entry.pop_back();
            }

            result.emplace_back(std::move(entry));
            position = next;
        }

        return result;
    }

    // Parses a CCM log entry, using an XML parser to validate and extract the metadata fields.
    CCMLogEntry ParseCCMLogEntry(const std::string& entry)
    {
        INFO("Entry: " << entry);

        CCMLogEntry result;

        REQUIRE(entry.substr(0, CCMLogStartToken.size()) == CCMLogStartToken);
        size_t endToken = entry.find(CCMLogEndToken);
        REQUIRE(endToken != std::string::npos);
        result.Message = entry.substr(CCMLogStartToken.size(), endToken - CCMLogStartToken.size());

        // The metadata portion is an XML-like tag with attributes but no element name:
        //   <time="..." date="..." component="..." context="..." type="..." thread="..." file="">
        // Give it an element name so that a real XML parser can validate the structure.
        std::string metadata = entry.substr(endToken + CCMLogEndToken.size());
        REQUIRE(metadata.size() > 2);
        REQUIRE(metadata.front() == '<');
        REQUIRE(metadata.back() == '>');

        std::string xml = "<entry " + metadata.substr(1, metadata.size() - 2) + "/>";
        winrt::Windows::Data::Xml::Dom::XmlDocument document;
        document.LoadXml(winrt::to_hstring(xml));

        auto element = document.DocumentElement();
        REQUIRE(element.Attributes().Length() == 7u);
        result.Time = winrt::to_string(element.GetAttribute(L"time"));
        result.Date = winrt::to_string(element.GetAttribute(L"date"));
        result.Component = winrt::to_string(element.GetAttribute(L"component"));
        result.Context = winrt::to_string(element.GetAttribute(L"context"));
        result.Type = winrt::to_string(element.GetAttribute(L"type"));
        result.Thread = winrt::to_string(element.GetAttribute(L"thread"));
        result.File = winrt::to_string(element.GetAttribute(L"file"));

        return result;
    }

    // Reads the given log file and parses it as a sequence of CCM entries.
    std::vector<CCMLogEntry> ParseCCMLogFile(const std::filesystem::path& file)
    {
        std::ifstream fileStream{ file, std::ios::binary };
        auto fileContents = ReadEntireStream(fileStream);
        INFO("File contents: " << fileContents);

        std::vector<CCMLogEntry> result;
        for (const auto& entry : SplitCCMEntries(fileContents))
        {
            result.emplace_back(ParseCCMLogEntry(entry));
        }

        return result;
    }

    // Validates the automatic fields of a CCM log entry, given the time range in which it was written.
    void ValidateCCMLogEntryFields(const CCMLogEntry& entry, Channel channel, int expectedType, std::time_t beforeWrite, std::time_t afterWrite)
    {
        INFO("Time: " << entry.Time << ", Date: " << entry.Date);

        // time="HH:MM:SS.mmm<+/-><bias>"
        int hour = -1, minute = -1, second = -1, milliseconds = -1, bias = -1;
        char sign = 0;
        REQUIRE(sscanf_s(entry.Time.c_str(), "%2d:%2d:%2d.%3d%c%d", &hour, &minute, &second, &milliseconds, &sign, 1, &bias) == 6);
        REQUIRE((sign == '+' || sign == '-'));
        REQUIRE(bias >= 0);
        REQUIRE(bias <= 14 * 60);
        REQUIRE(milliseconds >= 0);
        REQUIRE(milliseconds <= 999);

        // date="MM-DD-YYYY"
        int month = -1, day = -1, year = -1;
        REQUIRE(sscanf_s(entry.Date.c_str(), "%2d-%2d-%4d", &month, &day, &year) == 3);

        // The reconstructed local timestamp must fall within the write window.
        std::tm writtenTime{};
        writtenTime.tm_hour = hour;
        writtenTime.tm_min = minute;
        writtenTime.tm_sec = second;
        writtenTime.tm_mon = month - 1;
        writtenTime.tm_mday = day;
        writtenTime.tm_year = year - 1900;
        writtenTime.tm_isdst = -1;
        std::time_t written = std::mktime(&writtenTime);
        REQUIRE(written != -1);
        REQUIRE(written >= beforeWrite - 1);
        REQUIRE(written <= afterWrite + 1);

        REQUIRE(entry.Component == GetChannelName(channel));
        REQUIRE(entry.Type == std::to_string(expectedType));
        REQUIRE(entry.Thread == std::to_string(GetCurrentThreadId()));
        REQUIRE(entry.File.empty());
    }

    std::time_t NowAsTimeT()
    {
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }
}

TEST_CASE("FileLogger_CCMFormat", "[logging]")
{
    // CCM type: 1=Info/Verbose, 2=Warning, 3=Error/Critical.
    Level level = Level::Info;
    int expectedType = 1;
    SECTION("Verbose maps to type 1") { level = Level::Verbose; expectedType = 1; }
    SECTION("Info maps to type 1") { level = Level::Info; expectedType = 1; }
    SECTION("Warning maps to type 2") { level = Level::Warning; expectedType = 2; }
    SECTION("Error maps to type 3") { level = Level::Error; expectedType = 3; }
    SECTION("Crit maps to type 3") { level = Level::Crit; expectedType = 3; }

    const std::string message = "CCM format test message";

    TempFile tempFile{ "FileLogger_CCM", ".log" };
    INFO("File: " << tempFile.GetPath().u8string());

    std::time_t beforeWrite = NowAsTimeT();
    {
        CCMFileLogger logger{ tempFile };
        logger.Write(DefaultChannel, level, message);
    }
    std::time_t afterWrite = NowAsTimeT();

    auto entries = ParseCCMLogFile(tempFile);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].Message == message);
    ValidateCCMLogEntryFields(entries[0], DefaultChannel, expectedType, beforeWrite, afterWrite);
}

TEST_CASE("FileLogger_CCMFormat_EndTokenEscaped", "[logging]")
{
    // The end token has no official escape; it is replaced so that a message can never terminate the entry early.
    const std::string message = "prefix ]LOG]!><time=\"oops\"> suffix ]LOG]!> end";
    const std::string expectedMessage = "prefix |LOG|!><time=\"oops\"> suffix |LOG|!> end";

    TempFile tempFile{ "FileLogger_CCM", ".log" };
    INFO("File: " << tempFile.GetPath().u8string());

    std::time_t beforeWrite = NowAsTimeT();
    {
        CCMFileLogger logger{ tempFile };
        logger.Write(DefaultChannel, DefaultLevel, message);
    }
    std::time_t afterWrite = NowAsTimeT();

    auto entries = ParseCCMLogFile(tempFile);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].Message == expectedMessage);
    ValidateCCMLogEntryFields(entries[0], DefaultChannel, 1, beforeWrite, afterWrite);
}

TEST_CASE("FileLogger_CCMFormat_MultiLineMessage", "[logging]")
{
    const std::string message = "first line\nsecond line\nthird line";

    TempFile tempFile{ "FileLogger_CCM", ".log" };
    INFO("File: " << tempFile.GetPath().u8string());

    std::time_t beforeWrite = NowAsTimeT();
    {
        CCMFileLogger logger{ tempFile };
        logger.Write(DefaultChannel, DefaultLevel, message);
        logger.Write(DefaultChannel, DefaultLevel, "second entry");
    }
    std::time_t afterWrite = NowAsTimeT();

    auto entries = ParseCCMLogFile(tempFile);
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].Message == message);
    REQUIRE(entries[1].Message == "second entry");
    ValidateCCMLogEntryFields(entries[0], DefaultChannel, 1, beforeWrite, afterWrite);
    ValidateCCMLogEntryFields(entries[1], DefaultChannel, 1, beforeWrite, afterWrite);
}

TEST_CASE("FileLogger_CCMFormat_Wrap", "[logging]")
{
    TempFile tempFile{ "FileLogger_CCM_Wrap", ".log" };
    INFO("File: " << tempFile.GetPath().u8string());

    size_t maximumSize = 4096;
    const std::string headerMessage = "CCM header message";
    const std::string message = "A message that is repeated to force the log file to wrap multiple times over.";

    std::time_t beforeWrite = NowAsTimeT();
    {
        CCMFileLogger logger{ tempFile };
        logger.SetMaximumSize(static_cast<std::ofstream::off_type>(maximumSize));

        logger.Write(DefaultChannel, DefaultLevel, headerMessage);
        logger.SetTag(Tag::HeadersComplete);

        // Enough entries to wrap many times over.
        for (size_t i = 0; i < 200; ++i)
        {
            logger.Write(DefaultChannel, DefaultLevel, message + " #" + std::to_string(i));
        }
    }
    std::time_t afterWrite = NowAsTimeT();

    // The maximum may be exceeded slightly by a wrap indicator entry and newlines.
    REQUIRE(std::filesystem::file_size(tempFile.GetPath()) <= maximumSize + 512);

    // Every entry in the file must still parse cleanly; wrapping must not leave partial entries behind.
    auto entries = ParseCCMLogFile(tempFile);
    REQUIRE(entries.size() > 2);

    // The header is preserved across wraps and is followed by the wrap indicator.
    REQUIRE(entries[0].Message == headerMessage);
    REQUIRE(entries[1].Message == "--- log file has wrapped ---");

    for (const auto& entry : entries)
    {
        ValidateCCMLogEntryFields(entry, DefaultChannel, 1, beforeWrite, afterWrite);
    }

    // The last entry written must be the last entry in the file.
    REQUIRE(entries.back().Message == message + " #199");
}

TEST_CASE("FileLogger_CreateFromSettings", "[logging]")
{
    // The logger type is chosen from the "logging.format" user setting at creation; use the settings override hook.
    std::string settingsJson;
    bool expectCCM = false;
    SECTION("Default") { settingsJson = "{}"; expectCCM = false; }
    SECTION("WinGet format") { settingsJson = R"({ "logging": { "format": "winget" } })"; expectCCM = false; }
    SECTION("CCM format") { settingsJson = R"({ "logging": { "format": "ccm" } })"; expectCCM = true; }

    UserSettingsTest userSettings{ settingsJson };
    TestHook::SetUserSettings_Override userSettingsOverride{ userSettings };

    TempFile tempFile{ "FileLogger_Create", ".log" };
    INFO("File: " << tempFile.GetPath().u8string());
    {
        auto logger = FileLogger::Create(tempFile.GetPath());
        logger->Write(DefaultChannel, DefaultLevel, "format selection test");
    }

    std::ifstream fileStream{ tempFile.GetPath(), std::ios::binary };
    auto fileContents = ReadEntireStream(fileStream);
    INFO("File contents: " << fileContents);

    REQUIRE((fileContents.compare(0, CCMLogStartToken.size(), CCMLogStartToken) == 0) == expectCCM);
    REQUIRE(fileContents.find("format selection test") != std::string::npos);
}

TEST_CASE("FileLogger_MaximumSize_ManyWraps", "[logging]")
{
    TempFile tempFile{ "FileLogger_ManyWraps", ".log" };
    FileLogger logger{ tempFile };

    INFO("File: " << tempFile.GetPath().u8string());

    size_t maximumSize = 1000;
    logger.SetMaximumSize(static_cast<std::ofstream::off_type>(maximumSize));

    std::string header = GetHeaderString();
    header += " !Now with more header!";
    std::string largeString = "[*=INIT=*]Now we just need another few dozen characters, which shouldn't be that hard to get. Wow, made it already.";
    std::string_view largeStringView = largeString;
    size_t initSize = 10;

    logger.WriteDirect(DefaultChannel, DefaultLevel, header);
    logger.SetTag(Tag::HeadersComplete);

    // Use the default seed value as we want arbitrary but reproducible results
    std::default_random_engine randomEngine;
    std::uniform_int_distribution<> sizeDistribution(static_cast<int>(initSize), 100);

    // We should expect ~500 wraps on average
    for (size_t i = 0; i < 9999; ++i)
    {
        logger.WriteDirect(DefaultChannel, DefaultLevel, largeStringView.substr(0, sizeDistribution(randomEngine)));
    }

    // We want the header to be preserved, followed by the wrap indicator, and at a minimum we should see the first few characters in the log string
    std::vector<std::string_view> expectedFileContents;
    expectedFileContents.push_back(header);
    expectedFileContents.push_back(WrapIndicator);
    expectedFileContents.push_back(largeStringView.substr(0, initSize));

    ValidateFileContents(tempFile, expectedFileContents, maximumSize);
}

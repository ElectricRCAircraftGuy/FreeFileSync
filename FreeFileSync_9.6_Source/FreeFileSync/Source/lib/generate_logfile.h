// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#ifndef GENERATE_LOGFILE_H_931726432167489732164
#define GENERATE_LOGFILE_H_931726432167489732164

#include <zen/error_log.h>
#include <zen/file_io.h>
#include <zen/format_unit.h>
#include "ffs_paths.h"
#include "../fs/abstract.h"
#include "../file_hierarchy.h"


namespace zen
{
struct SummaryInfo
{
    std::wstring jobName; //may be empty
    std::wstring finalStatus;
    int itemsProcessed;
    int64_t bytesProcessed;
    int itemsTotal;
    int64_t bytesTotal;
    int64_t totalTime; //unit: [sec]
};

void streamToLogFile(const SummaryInfo& summary, //throw FileError
                     const ErrorLog& log,
                     AFS::OutputStream& streamOut);

void saveToLastSyncsLog(const SummaryInfo& summary, //throw FileError
                        const ErrorLog& log,
                        size_t maxBytesToWrite,
                        const IOCallback& notifyUnbufferedIO);

Zstring getLastSyncsLogfilePath();



struct OnUpdateLogfileStatusNoThrow
{
    OnUpdateLogfileStatusNoThrow(ProcessCallback& pc, const std::wstring& logfileDisplayPath) : pc_(pc),
        msg(replaceCpy(_("Saving file %x..."), L"%x", fmtPath(logfileDisplayPath))) {}

    void operator()(int64_t bytesDelta)
    {
        bytesWritten += bytesDelta;
        try { pc_.reportStatus(msg + L" (" + formatFilesizeShort(bytesWritten) + L")"); /*throw X*/ }
        catch (...) {}
    }

private:
    ProcessCallback& pc_;
    int64_t bytesWritten = 0;
    const std::wstring msg;
};



//####################### implementation #######################
namespace
{
std::wstring generateLogHeader(const SummaryInfo& s)
{
    assert(s.itemsProcessed <= s.itemsTotal);
    assert(s.bytesProcessed <= s.bytesTotal);

    std::wstring output;

    //write header
    std::wstring headerLine = formatTime<std::wstring>(FORMAT_DATE);
    if (!s.jobName.empty())
        headerLine += L" - " + s.jobName;
    headerLine += L": " + s.finalStatus;

    //assemble results box
    std::vector<std::wstring> results;
    results.push_back(headerLine);
    results.push_back(L"");

    const wchar_t tabSpace[] = L"    ";

    std::wstring itemsProc = tabSpace + _("Items processed:") + L" " + formatNumber(s.itemsProcessed); //show always, even if 0!
    if (s.itemsProcessed != 0 || s.bytesProcessed != 0) //[!] don't show 0 bytes processed if 0 items were processed
        itemsProc += + L" (" + formatFilesizeShort(s.bytesProcessed) + L")";
    results.push_back(itemsProc);

    if (s.itemsTotal != 0 || s.bytesTotal != 0) //=: sync phase was reached and there were actual items to sync
    {
        if (s.itemsProcessed != s.itemsTotal ||
            s.bytesProcessed  != s.bytesTotal)
            results.push_back(tabSpace + _("Items remaining:") + L" " + formatNumber(s.itemsTotal - s.itemsProcessed) + L" (" + formatFilesizeShort(s.bytesTotal - s.bytesProcessed) + L")");
    }

    results.push_back(tabSpace + _("Total time:") + L" " + copyStringTo<std::wstring>(wxTimeSpan::Seconds(s.totalTime).Format()));

    //calculate max width, this considers UTF-16 only, not true Unicode...but maybe good idea? those 2-char-UTF16 codes are usually wider than fixed width chars anyway!
    size_t sepLineLen = 0;
    for (const std::wstring& str : results) sepLineLen = std::max(sepLineLen, str.size());

    output.resize(output.size() + sepLineLen + 1, L'_');
    output += L'\n';

    for (const std::wstring& str : results) { output += L'|'; output += str; output += L'\n'; }

    output += L'|';
    output.resize(output.size() + sepLineLen, L'_');
    output += L'\n';

    return output;
}
}


inline
void streamToLogFile(const SummaryInfo& summary, //throw FileError
                     const ErrorLog& log,
                     AFS::OutputStream& streamOut)
{
    const std::string header = replaceCpy(utfTo<std::string>(generateLogHeader(summary)), '\n', LINE_BREAK); //don't replace line break any earlier

    streamOut.write(&header[0], header.size()); //throw FileError, X

    //write log items in blocks instead of creating one big string: memory allocation might fail; think 1 million entries!
    std::string buffer;
    buffer += LINE_BREAK;

    for (const LogEntry& entry : log)
    {
        buffer += replaceCpy(utfTo<std::string>(formatMessage<std::wstring>(entry)), '\n', LINE_BREAK);
        buffer += LINE_BREAK;

        streamOut.write(&buffer[0], buffer.size()); //throw FileError, X
        buffer.clear();
    }
}


inline
Zstring getLastSyncsLogfilePath() { return getConfigDirPathPf() + Zstr("LastSyncs.log"); }


inline
void saveToLastSyncsLog(const SummaryInfo& summary, //throw FileError
                        const ErrorLog& log,
                        size_t maxBytesToWrite, //log may be *huge*, e.g. 1 million items; LastSyncs.log *must not* create performance problems!
                        const IOCallback& notifyUnbufferedIO)
{
    const Zstring filepath = getLastSyncsLogfilePath();

    Utf8String newStream = utfTo<Utf8String>(generateLogHeader(summary));
    replace(newStream, '\n', LINE_BREAK); //don't replace line break any earlier
    newStream += LINE_BREAK;

    //check size of "newStream": memory allocation might fail - think 1 million entries!
    for (const LogEntry& entry : log)
    {
        newStream += replaceCpy(utfTo<Utf8String>(formatMessage<std::wstring>(entry)), '\n', LINE_BREAK);
        newStream += LINE_BREAK;

        if (newStream.size() > maxBytesToWrite)
        {
            newStream += "[...]";
            newStream += LINE_BREAK;
            break;
        }
    }

    //fill up the rest of permitted space by appending old log
    if (newStream.size() < maxBytesToWrite)
    {
        Utf8String oldStream;
        try
        {
            oldStream = loadBinContainer<Utf8String>(filepath, notifyUnbufferedIO); //throw FileError, X
            //Note: we also report the loaded bytes via onUpdateSaveStatus()!
        }
        catch (FileError&) {}

        if (!oldStream.empty())
        {
            newStream += LINE_BREAK;
            newStream += LINE_BREAK;
            newStream += oldStream; //impliticly limited by "maxBytesToWrite"!

            //truncate size if required
            if (newStream.size() > maxBytesToWrite)
            {
                //but do not cut in the middle of a row
                auto it = std::search(newStream.cbegin() + maxBytesToWrite, newStream.cend(), std::begin(LINE_BREAK), std::end(LINE_BREAK) - 1);
                if (it != newStream.cend())
                {
                    newStream.resize(it - newStream.cbegin());
                    newStream += LINE_BREAK;

                    newStream += "[...]";
                    newStream += LINE_BREAK;
                }
            }
        }
    }

    saveBinContainer(filepath, newStream, notifyUnbufferedIO); //throw FileError, X
}
}

#endif //GENERATE_LOGFILE_H_931726432167489732164

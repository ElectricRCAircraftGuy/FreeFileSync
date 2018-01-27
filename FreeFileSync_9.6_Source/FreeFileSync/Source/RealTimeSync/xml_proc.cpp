// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "xml_proc.h"
#include <zen/file_access.h>
#include <wx/filefn.h>
#include "../lib/process_xml.h"
#include "../lib/ffs_paths.h"

using namespace zen;
using namespace xmlAccess;


namespace
{
void readConfig(const XmlIn& in, XmlRealConfig& config)
{
    in["Directories"](config.directories);
    in["Delay"      ](config.delay);
    in["Commandline"](config.commandline);
}


bool isXmlTypeRTS(const XmlDoc& doc) //throw()
{
    if (doc.root().getNameAs<std::string>() == "FreeFileSync")
    {
        std::string type;
        if (doc.root().getAttribute("XmlType", type))
            return type == "REAL";
    }
    return false;
}
}


void xmlAccess::readConfig(const Zstring& filepath, XmlRealConfig& config, std::wstring& warningMsg) //throw FileError
{
    XmlDoc doc = loadXmlDocument(filepath); //throw FileError

    if (!isXmlTypeRTS(doc))
        throw FileError(replaceCpy(_("File %x does not contain a valid configuration."), L"%x", fmtPath(filepath)));

    XmlIn in(doc);
    ::readConfig(in, config);

    try
    {
        checkForMappingErrors(in, filepath); //throw FileError
    }
    catch (const FileError& e)
    {
        warningMsg = e.toString();
    }
}


namespace
{
void writeConfig(const XmlRealConfig& config, XmlOut& out)
{
    out["Directories"](config.directories);
    out["Delay"      ](config.delay);
    out["Commandline"](config.commandline);
}
}


void xmlAccess::writeConfig(const XmlRealConfig& config, const Zstring& filepath) //throw FileError
{
    XmlDoc doc("FreeFileSync");
    doc.root().setAttribute("XmlType", "REAL");

    XmlOut out(doc);
    ::writeConfig(config, out);

    saveXmlDocument(doc, filepath); //throw FileError
}


namespace
{
xmlAccess::XmlRealConfig convertBatchToReal(const xmlAccess::XmlBatchConfig& batchCfg, const Zstring& batchFilePath)
{
    std::set<Zstring, LessFilePath> uniqueFolders;

    //add main folders
    uniqueFolders.insert(batchCfg.mainCfg.firstPair.folderPathPhraseLeft_);
    uniqueFolders.insert(batchCfg.mainCfg.firstPair.folderPathPhraseRight_);

    //additional folders
    for (const FolderPairEnh& fp : batchCfg.mainCfg.additionalPairs)
    {
        uniqueFolders.insert(fp.folderPathPhraseLeft_);
        uniqueFolders.insert(fp.folderPathPhraseRight_);
    }

    erase_if(uniqueFolders, [](const Zstring& str) { return trimCpy(str).empty(); });

    xmlAccess::XmlRealConfig output;
    output.directories.assign(uniqueFolders.begin(), uniqueFolders.end());
    output.commandline = Zstr("\"") + zen::getFreeFileSyncLauncherPath() + Zstr("\" \"") + batchFilePath + Zstr("\"");
    return output;
}
}


void xmlAccess::readRealOrBatchConfig(const Zstring& filepath, xmlAccess::XmlRealConfig& config, std::wstring& warningMsg) //throw FileError
{
    using namespace xmlAccess;

    if (getXmlType(filepath) != XML_TYPE_BATCH) //throw FileError
        return readConfig(filepath, config, warningMsg); //throw FileError

    //convert batch config to RealTimeSync config
    XmlBatchConfig batchCfg;
    readConfig(filepath, batchCfg, warningMsg); //throw FileError
    //<- redirect batch config warnings

    config = convertBatchToReal(batchCfg, filepath);
}


wxLanguage xmlAccess::getProgramLanguage()
{
    xmlAccess::XmlGlobalSettings settings;
    std::wstring warningMsg;
    try
    {
        xmlAccess::readConfig(getGlobalConfigFile(), settings, warningMsg); //throw FileError
    }
    catch (const FileError&) {} //use default language if error occurred

    return settings.programLanguage;
}

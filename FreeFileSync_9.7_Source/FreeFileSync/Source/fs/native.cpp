// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "native.h"
#include <zen/file_access.h>
#include <zen/symlink_target.h>
#include <zen/file_io.h>
#include <zen/file_id_def.h>
#include <zen/stl_tools.h>
#include <zen/recycler.h>
#include <zen/guid.h>
#include <zen/crc.h>
#include "../lib/resolve_path.h"
#include "../lib/icon_loader.h"
#include "native_traverser_impl.h"

    #include <fcntl.h> //fallocate, fcntl

using namespace zen;


namespace
{


void initComForThread() //throw FileError
{
}


class RecycleSessionNative : public AbstractFileSystem::RecycleSession
{
public:
    RecycleSessionNative(const Zstring baseFolderPath) : baseFolderPathPf_(appendSeparator(baseFolderPath)) {}

    bool recycleItem(const AbstractPath& itemPath, const Zstring& logicalRelPath) override; //throw FileError
    void tryCleanup(const std::function<void (const std::wstring& displayPath)>& notifyDeletionStatus) override; //throw FileError

private:
    const Zstring baseFolderPathPf_; //ends with path separator
};

//===========================================================================================================================

    typedef struct ::stat FileAttribs; //GCC 5.2 fails when "::" is used in "using FileAttribs = struct ::stat"


inline
FileAttribs getFileAttributes(FileBase::FileHandle fh, const Zstring& filePath) //throw FileError
{
    struct ::stat fileAttr = {};
    if (::fstat(fh, &fileAttr) != 0)
        THROW_LAST_FILE_ERROR(replaceCpy(_("Cannot read file attributes of %x."), L"%x", fmtPath(filePath)), L"fstat");
    return fileAttr;
}


struct InputStreamNative : public AbstractFileSystem::InputStream
{
    InputStreamNative(const Zstring& filePath, const IOCallback& notifyUnbufferedIO) : fi_(filePath, notifyUnbufferedIO) {} //throw FileError, ErrorFileLocked

    size_t read(void* buffer, size_t bytesToRead) override { return fi_.read(buffer, bytesToRead); } //throw FileError, ErrorFileLocked, X; return "bytesToRead" bytes unless end of stream!
    size_t getBlockSize() const override { return fi_.getBlockSize(); } //non-zero block size is AFS contract!
    Opt<AFS::StreamAttributes> getAttributesBuffered() override; //throw FileError

private:
    FileInput fi_;
};


Opt<AFS::StreamAttributes> InputStreamNative::getAttributesBuffered() //throw FileError
{
    const FileAttribs fileAttr = getFileAttributes(fi_.getHandle(), fi_.getFilePath()); //throw FileError

    const time_t modTime = fileAttr.st_mtime;

    const uint64_t fileSize = makeUnsigned(fileAttr.st_size);

    const AFS::FileId fileId = convertToAbstractFileId(extractFileId(fileAttr));

    return AFS::StreamAttributes({ modTime, fileSize, fileId });
}

//===========================================================================================================================

struct OutputStreamNative : public AbstractFileSystem::OutputStreamImpl
{
    OutputStreamNative(const Zstring& filePath, const uint64_t* streamSize, const IOCallback& notifyUnbufferedIO) :
        fo_(filePath, FileOutput::ACC_CREATE_NEW, notifyUnbufferedIO) //throw FileError, ErrorTargetExisting
    {
        if (streamSize) //pre-allocate file space, because we can
            fo_.preAllocateSpaceBestEffort(*streamSize); //throw FileError
    }

    void write(const void* buffer, size_t bytesToWrite) override { fo_.write(buffer, bytesToWrite); } //throw FileError, X

    AFS::FileId finalize() override //throw FileError, X
    {
        const AFS::FileId fileId = convertToAbstractFileId(extractFileId(getFileAttributes(fo_.getHandle(), fo_.getFilePath()))); //throw FileError

        fo_.finalize(); //throw FileError, X

        return fileId;
    }

private:
    FileOutput fo_;
};

//===========================================================================================================================

class NativeFileSystem : public AbstractFileSystem
{
public:
    NativeFileSystem(const Zstring& rootPath) : rootPath_(rootPath) {}

private:
    Zstring getNativePath(const AfsPath& afsPath) const { return appendPaths(rootPath_, afsPath.value, FILE_NAME_SEPARATOR); }

    Opt<Zstring> getNativeItemPath(const AfsPath& afsPath) const override { return getNativePath(afsPath); }

    Zstring getInitPathPhrase(const AfsPath& afsPath) const override { return getNativePath(afsPath); }

    std::wstring getDisplayPath(const AfsPath& afsPath) const override { return utfTo<std::wstring>(getNativePath(afsPath)); }

    bool isNullFileSystem() const override { return rootPath_.empty(); }

    int compareDeviceRootSameAfsType(const AbstractFileSystem& afsRhs) const override
    {
        const Zstring& rootPathRhs = static_cast<const NativeFileSystem&>(afsRhs).rootPath_;

        return CmpFilePath()(rootPath_  .c_str(), rootPath_  .size(),
                             rootPathRhs.c_str(), rootPathRhs.size());
    }

    //----------------------------------------------------------------------------------------------------------------
    ItemType getItemType(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        switch (zen::getItemType(getNativePath(afsPath))) //throw FileError
        {
            case zen::ItemType::FILE:
                return AFS::ItemType::FILE;
            case zen::ItemType::FOLDER:
                return AFS::ItemType::FOLDER;
            case zen::ItemType::SYMLINK:
                return AFS::ItemType::SYMLINK;
        }
        assert(false);
        return AFS::ItemType::FILE;
    }

    PathStatusImpl getPathStatus(const AfsPath& afsPath) const override //throw FileError
    {
        return getPathStatusViaFolderTraversal(afsPath); //throw FileError
    }
    //----------------------------------------------------------------------------------------------------------------

    //target existing: undefined behavior! (fail/overwrite) => Native will fail and give a clear error message
    void createFolderPlain(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        createDirectory(getNativePath(afsPath)); //throw FileError, ErrorTargetExisting
    }

    void removeFilePlain(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        zen::removeFilePlain(getNativePath(afsPath)); //throw FileError
    }

    void removeSymlinkPlain(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        zen::removeSymlinkPlain(getNativePath(afsPath)); //throw FileError
    }

    void removeFolderPlain(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        zen::removeDirectoryPlain(getNativePath(afsPath)); //throw FileError
    }

    //----------------------------------------------------------------------------------------------------------------
    void setModTime(const AfsPath& afsPath, time_t modTime) const override //throw FileError, follows symlinks
    {
        initComForThread(); //throw FileError
        zen::setFileTime(getNativePath(afsPath), modTime, ProcSymlink::FOLLOW); //throw FileError
    }

    AbstractPath getSymlinkResolvedPath(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        const Zstring nativePath = getNativePath(afsPath);

        const Zstring resolvedPath = zen::getSymlinkResolvedPath(nativePath); //throw FileError
        const Opt<zen::PathComponents> comp = parsePathComponents(resolvedPath);
        if (!comp)
            throw FileError(replaceCpy(_("Cannot determine final path for %x."), L"%x", fmtPath(nativePath)),
                            replaceCpy<std::wstring>(L"Invalid path %x.", L"%x", fmtPath(resolvedPath)));

        return AbstractPath(std::make_shared<NativeFileSystem>(comp->rootPath), AfsPath(comp->relPath));
    }

    std::string getSymlinkBinaryContent(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        const Zstring nativePath = getNativePath(afsPath);

        std::string content = utfTo<std::string>(getSymlinkTargetRaw(nativePath)); //throw FileError
        return content;
    }
    //----------------------------------------------------------------------------------------------------------------

    //return value always bound:
    std::unique_ptr<InputStream> getInputStream(const AfsPath& afsPath, const IOCallback& notifyUnbufferedIO) const override //throw FileError, ErrorFileLocked, (X)
    {
        initComForThread(); //throw FileError
        return std::make_unique<InputStreamNative>(getNativePath(afsPath), notifyUnbufferedIO); //throw FileError, ErrorFileLocked
    }

    //target existing: undefined behavior! (fail/overwrite/auto-rename) => Native will fail and give a clear error message
    std::unique_ptr<OutputStreamImpl> getOutputStream(const AfsPath& afsPath, //throw FileError
                                                      const uint64_t* streamSize,                          //optional
                                                      const IOCallback& notifyUnbufferedIO) const override //
    {
        initComForThread(); //throw FileError
        return std::make_unique<OutputStreamNative>(getNativePath(afsPath), streamSize, notifyUnbufferedIO); //throw FileError
    }

    //----------------------------------------------------------------------------------------------------------------
    void traverseFolder(const AfsPath& afsPath, TraverserCallback& sink /*throw X*/) const override //throw X
    {
        DirTraverser::execute(getNativePath(afsPath), sink); //throw X
    }
    //----------------------------------------------------------------------------------------------------------------

    //symlink handling: follow link!
    //target existing: undefined behavior! (fail/overwrite/auto-rename) => Native will fail and give a clear error message
    FileCopyResult copyFileForSameAfsType(const AfsPath& afsPathSource, const StreamAttributes& attrSource, //throw FileError, ErrorFileLocked
                                          const AbstractPath& apTarget, bool copyFilePermissions, const IOCallback& notifyUnbufferedIO) const override //may be nullptr; throw X!
    {
        const Zstring nativePathTarget = static_cast<const NativeFileSystem&>(getAfs(apTarget)).getNativePath(getAfsPath(apTarget));

        initComForThread(); //throw FileError

        const zen::FileCopyResult nativeResult = copyNewFile(getNativePath(afsPathSource), nativePathTarget, //throw FileError, ErrorTargetExisting, ErrorFileLocked
                                                             copyFilePermissions, notifyUnbufferedIO); //may be nullptr; throw X!
        FileCopyResult result;
        result.fileSize     = nativeResult.fileSize;
        result.modTime      = nativeResult.modTime;
        result.sourceFileId = convertToAbstractFileId(nativeResult.sourceFileId);
        result.targetFileId = convertToAbstractFileId(nativeResult.targetFileId);
        result.errorModTime = nativeResult.errorModTime;
        return result;
    }

    //target existing: undefined behavior! (fail/overwrite) => Native will fail and give a clear error message
    //symlink handling: follow link!
    void copyNewFolderForSameAfsType(const AfsPath& afsPathSource, const AbstractPath& apTarget, bool copyFilePermissions) const override //throw FileError
    {
        initComForThread(); //throw FileError

        const Zstring& sourcePath = getNativePath(afsPathSource);
        const Zstring& targetPath = static_cast<const NativeFileSystem&>(getAfs(apTarget)).getNativePath(getAfsPath(apTarget));

        zen::createDirectory(targetPath); //throw FileError, ErrorTargetExisting

        ZEN_ON_SCOPE_FAIL(try { removeDirectoryPlain(targetPath); }
        catch (FileError&) {});

        tryCopyDirectoryAttributes(sourcePath, targetPath); //throw FileError

        if (copyFilePermissions)
            copyItemPermissions(sourcePath, targetPath, ProcSymlink::FOLLOW); //throw FileError
    }

    void copySymlinkForSameAfsType(const AfsPath& afsPathSource, const AbstractPath& apTarget, bool copyFilePermissions) const override //throw FileError
    {
        const Zstring nativePathTarget = static_cast<const NativeFileSystem&>(getAfs(apTarget)).getNativePath(getAfsPath(apTarget));

        initComForThread(); //throw FileError
        zen::copySymlink(getNativePath(afsPathSource), nativePathTarget, copyFilePermissions); //throw FileError
    }

    //target existing: undefined behavior! (fail/overwrite/auto-rename) => Native will fail and give a clear error message
    void renameItemForSameAfsType(const AfsPath& afsPathSource, const AbstractPath& apTarget) const override //throw FileError, ErrorDifferentVolume
    {
        //perf test: detecting different volumes by path is ~30 times faster than having MoveFileEx fail with ERROR_NOT_SAME_DEVICE (6�s vs 190�s)
        //=> maybe we can even save some actual I/O in some cases?
        if (compareDeviceRootSameAfsType(getAfs(apTarget)) != 0)
            throw ErrorDifferentVolume(replaceCpy(replaceCpy(_("Cannot move file %x to %y."),
                                                             L"%x", L"\n" + fmtPath(getDisplayPath(afsPathSource))),
                                                  L"%y", L"\n" + fmtPath(AFS::getDisplayPath(apTarget))),
                                       formatSystemError(L"compareDeviceRoot", EXDEV)
                                      );
        initComForThread(); //throw FileError
        const Zstring nativePathTarget = static_cast<const NativeFileSystem&>(getAfs(apTarget)).getNativePath(getAfsPath(apTarget));
        zen::renameFile(getNativePath(afsPathSource), nativePathTarget); //throw FileError, ErrorTargetExisting, ErrorDifferentVolume
    }

    bool supportsPermissions(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        return zen::supportsPermissions(getNativePath(afsPath));
    }

    //----------------------------------------------------------------------------------------------------------------
    ImageHolder getFileIcon(const AfsPath& afsPath, int pixelSize) const override //noexcept; optional return value
    {
        try
        {
            initComForThread(); //throw FileError
            return zen::getFileIcon(getNativePath(afsPath), pixelSize);
        }
        catch (FileError&) { assert(false); return ImageHolder(); }
    }

    ImageHolder getThumbnailImage(const AfsPath& afsPath, int pixelSize) const override //noexcept; optional return value
    {
        try
        {
            initComForThread(); //throw FileError
            return zen::getThumbnailImage(getNativePath(afsPath), pixelSize);
        }
        catch (FileError&) { assert(false); return ImageHolder(); }
    }

    void connectNetworkFolder(const AfsPath& afsPath, bool allowUserInteraction) const override //throw FileError
    {
        //TODO: clean-up/remove/re-think connectNetworkFolder()

    }

    //----------------------------------------------------------------------------------------------------------------

    uint64_t getFreeDiskSpace(const AfsPath& afsPath) const override //throw FileError, returns 0 if not available
    {
        initComForThread(); //throw FileError
        return zen::getFreeDiskSpace(getNativePath(afsPath)); //throw FileError
    }

    bool supportsRecycleBin(const AfsPath& afsPath, const std::function<void ()>& onUpdateGui) const override //throw FileError
    {
        return true; //truth be told: no idea!!!
    }

    std::unique_ptr<RecycleSession> createRecyclerSession(const AfsPath& afsPath) const override //throw FileError, return value must be bound!
    {
        initComForThread(); //throw FileError
        assert(supportsRecycleBin(afsPath, nullptr));
        return std::make_unique<RecycleSessionNative>(getNativePath(afsPath));
    }

    void recycleItemIfExists(const AfsPath& afsPath) const override //throw FileError
    {
        initComForThread(); //throw FileError
        zen::recycleOrDeleteIfExists(getNativePath(afsPath)); //throw FileError
    }

    const Zstring rootPath_;
};

//===========================================================================================================================



bool RecycleSessionNative::recycleItem(const AbstractPath& itemPath, const Zstring& logicalRelPath) //throw FileError
{
    assert(!startsWith(logicalRelPath, FILE_NAME_SEPARATOR));

    Opt<Zstring> itemPathNative = AFS::getNativeItemPath(itemPath);
    if (!itemPathNative)
        throw std::logic_error("Contract violation! " + std::string(__FILE__) + ":" + numberTo<std::string>(__LINE__));

    return recycleOrDeleteIfExists(*itemPathNative); //throw FileError
}


void RecycleSessionNative::tryCleanup(const std::function<void (const std::wstring& displayPath)>& notifyDeletionStatus) //throw FileError
{
}
}


//coordinate changes with getResolvedFilePath()!
bool zen::acceptsItemPathPhraseNative(const Zstring& itemPathPhrase) //noexcept
{
    Zstring path = itemPathPhrase;
    path = expandMacros(path); //expand before trimming!
    trim(path);


    if (startsWith(path, Zstr("["))) //drive letter by volume name syntax
        return true;

    //don't accept relative paths!!! indistinguishable from Explorer MTP paths!
    //don't accept paths missing the shared folder! (see drag & drop validation!)
    return static_cast<bool>(parsePathComponents(path));
}


AbstractPath zen::createItemPathNative(const Zstring& itemPathPhrase) //noexcept
{
    //TODO: get volume by name hangs for idle HDD! => run createItemPathNative during getFolderStatusNonBlocking() but getResolvedFilePath currently not thread-safe!
    const Zstring itemPath = getResolvedFilePath(itemPathPhrase);
    return createItemPathNativeNoFormatting(itemPath);
}


AbstractPath zen::createItemPathNativeNoFormatting(const Zstring& nativePath) //noexcept
{
    if (const Opt<PathComponents> comp = parsePathComponents(nativePath))
        return AbstractPath(std::make_shared<NativeFileSystem>(comp->rootPath), AfsPath(comp->relPath));
    else
    {
        //  assert(nativePath.empty());
        return AbstractPath(std::make_shared<NativeFileSystem>(nativePath), AfsPath(Zstring()));
    }
}

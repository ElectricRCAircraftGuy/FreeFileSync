// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#ifndef SORTING_H_82574232452345
#define SORTING_H_82574232452345

#include <zen/type_tools.h>
#include "../file_hierarchy.h"


namespace zen
{
namespace
{
struct CompileTimeReminder : public FSObjectVisitor
{
    void visit(const FilePair&    file   ) override {}
    void visit(const SymlinkPair& symlink) override {}
    void visit(const FolderPair&  folder ) override {}
} checkDymanicCasts; //just a compile-time reminder to manually check dynamic casts in this file if ever needed
}


inline
bool isDirectoryPair(const FileSystemObject& fsObj)
{
    return dynamic_cast<const FolderPair*>(&fsObj) != nullptr;
}


template <bool ascending, SelectedSide side> inline
bool lessShortFileName(const FileSystemObject& a, const FileSystemObject& b)
{
    //sort order: first files/symlinks, then directories then empty rows

    //empty rows always last
    if (a.isEmpty<side>())
        return false;
    else if (b.isEmpty<side>())
        return true;

    //directories after files/symlinks:
    if (isDirectoryPair(a))
    {
        if (!isDirectoryPair(b))
            return false;
    }
    else if (isDirectoryPair(b))
        return true;

    //sort directories and files/symlinks by short name
    return makeSortDirection(LessNaturalSort() /*even on Linux*/, Int2Type<ascending>())(a.getItemName<side>(), b.getItemName<side>());
}


template <bool ascending, SelectedSide side> inline
bool lessFullPath(const FileSystemObject& a, const FileSystemObject& b)
{
    //empty rows always last
    if (a.isEmpty<side>())
        return false;
    else if (b.isEmpty<side>())
        return true;

    return makeSortDirection(LessNaturalSort() /*even on Linux*/, Int2Type<ascending>())(
               utfTo<Zstring>(AFS::getDisplayPath(a.getAbstractPath<side>())),
               utfTo<Zstring>(AFS::getDisplayPath(b.getAbstractPath<side>())));
}


template <bool ascending>  inline //side currently unused!
bool lessRelativeFolder(const FileSystemObject& a, const FileSystemObject& b)
{
    const bool isDirectoryA = isDirectoryPair(a);
    const Zstring& relFolderA = isDirectoryA ?
                                a.getPairRelativePath() :
                                a.parent().getPairRelativePath();

    const bool isDirectoryB = isDirectoryPair(b);
    const Zstring& relFolderB = isDirectoryB ?
                                b.getPairRelativePath() :
                                b.parent().getPairRelativePath();

    //compare relative names without filepaths first
    const int rv = CmpNaturalSort()(relFolderA.c_str(), relFolderA.size(),
                                    relFolderB.c_str(), relFolderB.size());
    if (rv != 0)
        return makeSortDirection(std::less<int>(), Int2Type<ascending>())(rv, 0);

    //make directories always appear before contained files
    if (isDirectoryB)
        return false;
    else if (isDirectoryA)
        return true;

    return makeSortDirection(LessNaturalSort(), Int2Type<ascending>())(a.getPairItemName(), b.getPairItemName());
}


template <bool ascending, SelectedSide side> inline
bool lessFilesize(const FileSystemObject& a, const FileSystemObject& b)
{
    //empty rows always last
    if (a.isEmpty<side>())
        return false;
    else if (b.isEmpty<side>())
        return true;

    //directories second last
    if (isDirectoryPair(a))
        return false;
    else if (isDirectoryPair(b))
        return true;

    const FilePair* fileA = dynamic_cast<const FilePair*>(&a);
    const FilePair* fileB = dynamic_cast<const FilePair*>(&b);

    //then symlinks
    if (!fileA)
        return false;
    else if (!fileB)
        return true;

    //return list beginning with largest files first
    return makeSortDirection(std::less<>(), Int2Type<ascending>())(fileA->getFileSize<side>(), fileB->getFileSize<side>());
}


template <bool ascending, SelectedSide side> inline
bool lessFiletime(const FileSystemObject& a, const FileSystemObject& b)
{
    if (a.isEmpty<side>())
        return false; //empty rows always last
    else if (b.isEmpty<side>())
        return true; //empty rows always last

    const FilePair* fileA = dynamic_cast<const FilePair*>(&a);
    const FilePair* fileB = dynamic_cast<const FilePair*>(&b);

    const SymlinkPair* symlinkA = dynamic_cast<const SymlinkPair*>(&a);
    const SymlinkPair* symlinkB = dynamic_cast<const SymlinkPair*>(&b);

    if (!fileA && !symlinkA)
        return false; //directories last
    else if (!fileB && !symlinkB)
        return true; //directories last

    const int64_t dateA = fileA ? fileA->getLastWriteTime<side>() : symlinkA->getLastWriteTime<side>();
    const int64_t dateB = fileB ? fileB->getLastWriteTime<side>() : symlinkB->getLastWriteTime<side>();

    //return list beginning with newest files first
    return makeSortDirection(std::less<>(), Int2Type<ascending>())(dateA, dateB);
}


template <bool ascending, SelectedSide side> inline
bool lessExtension(const FileSystemObject& a, const FileSystemObject& b)
{
    if (a.isEmpty<side>())
        return false; //empty rows always last
    else if (b.isEmpty<side>())
        return true; //empty rows always last

    if (dynamic_cast<const FolderPair*>(&a))
        return false; //directories last
    else if (dynamic_cast<const FolderPair*>(&b))
        return true; //directories last

    auto getExtension = [](const FileSystemObject& fsObj)
    {
        return afterLast(fsObj.getItemName<side>(), Zchar('.'), zen::IF_MISSING_RETURN_NONE);
    };

    return makeSortDirection(LessNaturalSort() /*even on Linux*/, Int2Type<ascending>())(getExtension(a), getExtension(b));
}


template <bool ascending> inline
bool lessCmpResult(const FileSystemObject& a, const FileSystemObject& b)
{
    //presort result: equal shall appear at end of list
    if (a.getCategory() == FILE_EQUAL)
        return false;
    if (b.getCategory() == FILE_EQUAL)
        return true;

    return makeSortDirection(std::less<CompareFilesResult>(), Int2Type<ascending>())(a.getCategory(), b.getCategory());
}


template <bool ascending> inline
bool lessSyncDirection(const FileSystemObject& a, const FileSystemObject& b)
{
    return makeSortDirection(std::less<>(), Int2Type<ascending>())(a.getSyncOperation(), b.getSyncOperation());
}
}

#endif //SORTING_H_82574232452345

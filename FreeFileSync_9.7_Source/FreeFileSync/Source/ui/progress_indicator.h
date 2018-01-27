// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#ifndef PROGRESS_INDICATOR_H_8037493452348
#define PROGRESS_INDICATOR_H_8037493452348

#include <functional>
#include <zen/error_log.h>
#include <zen/zstring.h>
#include <wx/frame.h>
#include "../lib/status_handler.h"
#include "../lib/process_xml.h"


class CompareProgressDialog
{
public:
    CompareProgressDialog(wxFrame& parentWindow); //CompareProgressDialog will be owned by parentWindow!

    wxWindow* getAsWindow(); //convenience! don't abuse!

    void init(const zen::Statistics& syncStat, bool ignoreErrors); //begin of sync: make visible, set pointer to "syncStat", initialize all status values
    void teardown(); //end of sync: hide again, clear pointer to "syncStat"

    void initNewPhase(); //call after "StatusHandler::initNewPhase"

    void updateGui();

    //allow changing a few options dynamically during sync
    bool getOptionIgnoreErrors() const;
    void setOptionIgnoreErrors(bool ignoreError);

private:
    class Impl;
    Impl* const pimpl_;
};


//StatusHandlerFloatingDialog will internally process Window messages => disable GUI controls to avoid unexpected callbacks!

struct SyncProgressDialog
{
    enum SyncResult
    {
        RESULT_ABORTED,
        RESULT_FINISHED_WITH_ERROR,
        RESULT_FINISHED_WITH_WARNINGS,
        RESULT_FINISHED_WITH_SUCCESS
    };
    //essential to call one of these two methods in StatusUpdater derived class' destructor at the LATEST(!)
    //to prevent access to callback to updater (e.g. request abort)
    virtual void showSummary(SyncResult resultId, const zen::ErrorLog& log) = 0; //sync finished, still dialog may live on
    virtual void closeDirectly(bool restoreParentFrame) = 0; //don't wait for user

    //---------------------------------------------------------------------------

    virtual wxWindow* getWindowIfVisible() = 0; //may be nullptr; don't abuse, use as parent for modal dialogs only!

    virtual void initNewPhase        () = 0; //call after "StatusHandler::initNewPhase"
    virtual void notifyProgressChange() = 0; //noexcept, required by graph!
    virtual void updateGui           () = 0; //update GUI and process Window messages

    //allow changing a few options dynamically during sync
    virtual bool getOptionIgnoreErrors()           const = 0;
    virtual void setOptionIgnoreErrors(bool ignoreError) = 0;
    virtual xmlAccess::PostSyncAction getOptionPostSyncAction() const = 0;

    virtual void stopTimer  () = 0; //halt all internal timers!
    virtual void resumeTimer() = 0; //

protected:
    ~SyncProgressDialog() {}
};


SyncProgressDialog* createProgressDialog(zen::AbortCallback& abortCb,
                                         const std::function<void()>& notifyWindowTerminate, //note: user closing window cannot be prevented on OS X! (And neither on Windows during system shutdown!)
                                         const zen::Statistics& syncStat,
                                         wxFrame* parentWindow, //may be nullptr
                                         bool showProgress,
                                         const wxString& jobName,
                                         const Zstring& soundFileSyncComplete,
                                         bool ignoreErrors,
                                         xmlAccess::PostSyncAction postSyncAction);
//DON'T delete the pointer! it will be deleted by the user clicking "OK/Cancel"/wxWindow::Destroy() after showSummary() or closeDirectly()


class PauseTimers
{
public:
    PauseTimers(SyncProgressDialog& ss) : ss_(ss) { ss_.stopTimer(); }
    ~PauseTimers() { ss_.resumeTimer(); }
private:
    SyncProgressDialog& ss_;
};


#endif //PROGRESS_INDICATOR_H_8037493452348

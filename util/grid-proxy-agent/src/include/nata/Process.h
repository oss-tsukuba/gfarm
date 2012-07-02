/* 
 * $Id$
 */
#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <nata/nata_rcsid.h>

#include <nata/nata_includes.h>

#include <nata/nata_macros.h>

#include <nata/nata_safe_syscall.h>

#include <nata/Mutex.h>
#include <nata/ScopedLock.h>
#include <nata/ProcessJanitor.h>
#include <nata/Thread.h>
#include <nata/Completion.h>

#include <nata/nata_perror.h>


#define closeIfGE0(fd) {                        \
        if ((fd) >= 0) {                        \
            (void)::close((fd));                \
        }                                       \
    }


#define closeIfGE0AndReset(fd) {                \
        closeIfGE0((fd));                       \
        (fd) = -1;                              \
    }



class Process {



private:
    __rcsId("$Id$");



private:


    class mParentThread: public Thread {
    private:
        Process *mMPPtr;

    protected:
        int
        run(void) {
            mMPPtr->mRunParent();
            if (mMPPtr->getProcessSyncType() == 
                Process::Process_Sync_DontCare) {
                mMPPtr->mWait(true);
            }

            return 0;
        }

    public:
        mParentThread(Process *mpPtr) :
            mMPPtr(mpPtr) {
        }
    };
    friend class mParentThread;



public:
    typedef enum {
	Process_Role_Unknown = 0,
        Process_Role_ParentBeforeFork,
	Process_Role_Parent,
	Process_Role_Child,
        Process_Role_End
    } ProcessRoleT;
#define isValidRoleRange(role)                                  \
    ((((int)(role) >= 0) &&                                     \
      ((int)(role) < (int)(Process_Role_End))) ? true : false)

    typedef enum {
        Process_Sync_Unknown = 0,
        Process_Sync_Synchronous,
        Process_Sync_Asynchronous,
        Process_Sync_DontCare,
        Process_Sync_End
    } ProcessSyncT;
#define isValidSyncRange(sync)                                  \
    ((((int)(sync) >= 0) &&                                     \
      ((int)(sync) < (int)(Process_Sync_End))) ? true : false)

    typedef enum {
        Process_IPC_Unknown = 0,
        Process_IPC_Pipe,
        Process_IPC_Pty,
        Process_IPC_PtyInteractive,
        Process_IPC_End
    } ProcessIPCT;
#define isValidIPCRange(ipc)                                   \
    ((((int)(ipc) >= 0) &&                                     \
      ((int)(ipc) < (int)(Process_IPC_End))) ? true : false)

#define EXITCODE_IMPOSSIBLE	INT_MIN
    typedef enum {
        Process_Exit_Unknown = EXITCODE_IMPOSSIBLE,
        Process_Exit_IPCSetupFailure = 100,
        Process_Exit_IPCSyncChildWriteFailure = 101,
        Process_Exit_IPCSyncChildReadFailure = 102,
        Process_Exit_End = 103
    } ProcessExitT;
#define isValidExitRange(exit)                                 \
    ((((int)(exit) >= 0) &&                                    \
      ((int)(exit) < (int)(Process_Exit_End))) ? true : false)



private:
    Mutex mStatusLock;
    Mutex mCleanupLock;
    Mutex mIOLock;
    Mutex mUsingLock;
    WaitCondition mUsingCond;

    const char *mCWD;		// malloc'd.

    const char *mStdInFile;	// NULL or "": pipe/pty
				// malloc'd.
    const char *mStdOutFile;	// malloc'd.
    const char *mStdErrFile;	// malloc'd.

    ProcessRoleT mRole;

    // Note:
    //	All the method and variable names are meaning from child's
    //	point of voew.

    // File discriptors for parent - child communication.
    int mChildInFd[2];		// 0: child read, 1: parent write
    int mChildOutFd[2];		// 0: parent read, 1: child write
    int mChildErrFd[2];		// 0: parent read, 1: child write

    FILE *mStdInFd;		// parent: write, child: read.
    FILE *mStdOutFd;		// parent: read, child: write.
    FILE *mStdErrFd;		// parent: read, child: write.

    ProcessSyncT mSyncType;
    ProcessIPCT mIpcType;
    pid_t mChildPid;
    int mExitCode;
    bool mIsWaited;
    bool mIsUsing;
    bool mIsIPCChannelsSetup[(int)Process_Role_End];

    bool mIsDeleting;
    bool mIsFdsValid;

    char mSlavePtyName[4096];
    int mMasterPtyFd;

    

private:


    inline void
    mCleanupFds(void) {
        ScopedLock lc(&mCleanupLock); {
            ScopedLock li(&mIOLock);

            int fd;

            closeIfGE0AndReset(mChildInFd[0]);
            closeIfGE0AndReset(mChildOutFd[1]);
            closeIfGE0AndReset(mChildErrFd[1]);

            // To avoid closing any opened files while we are here,
            // use a trick.
            if (mStdInFd != NULL && mStdInFd != stdin) {
                fd = fileno(mStdInFd);
                (void)fclose(mStdInFd);
                mStdInFd = NULL;
                if (fd != mChildInFd[1]) {
                    closeIfGE0AndReset(mChildInFd[1]);
                }
            }

            if (mStdOutFd != NULL && mStdOutFd != stdout) {
                fd = fileno(mStdOutFd);
                (void)fclose(mStdOutFd);
                mStdOutFd = NULL;
                if (fd != mChildOutFd[0]) {
                    closeIfGE0AndReset(mChildOutFd[0]);
                }
            }

            if (mStdErrFd != NULL && mStdErrFd != stdout) {
                fd = fileno(mStdErrFd);
                (void)fclose(mStdErrFd);
                mStdErrFd = NULL;
                if (fd != mChildErrFd[0]) {
                    closeIfGE0AndReset(mChildErrFd[0]);
                }
            }

            mChildInFd[0] = -1;
            mChildInFd[1] = -1;
            mChildOutFd[0] = -1;
            mChildOutFd[1] = -1;
            mChildErrFd[0] = -1;
            mChildErrFd[1] = -1;

            mIsFdsValid = false;
        }
    }


    void
    mCleanupStatus(void) {
        ScopedLock lc(&mCleanupLock); {
            ScopedLock ls(&mStatusLock); {
                //mCWD,
                //mStdInFile,
                //mStdOutFile,
                //mStdErrFile

                mRole = Process_Role_Unknown;

                //mStdInFd
                //mStdOutFd
                //mStdErrFd

                mSyncType = Process::Process_Sync_Unknown;
                mIpcType = Process::Process_IPC_Unknown;
                mChildPid = (pid_t)-1;
                mExitCode = EXITCODE_IMPOSSIBLE;
                mIsWaited = false;
                // mIsUsing

                mIsDeleting = false;
                // mIsFdsValid

                (void)memset((void *)mSlavePtyName, 0, sizeof(mSlavePtyName));
                mMasterPtyFd = -1;

                mIsIPCChannelsSetup[0] = false;
                mIsIPCChannelsSetup[1] = false;
                mIsIPCChannelsSetup[2] = false;
                mIsIPCChannelsSetup[3] = false;
            }
        }
    }


    void
    mCleanupAll(void) {
        mCleanupStatus();
        mCleanupFds();
    }



protected:


    inline ProcessSyncT
    getProcessSyncType(void) {
        return mSyncType;
    }


    inline ProcessIPCT
    getProcessIPCType(void) {
        return mIpcType;
    }


    inline bool
    setFds(int in, int out, int error) {
        bool ret = false;
#define closeAndSet(src, dst)                   \
        if ((src) != (dst)) {                   \
            if ((src) >= 0) {                   \
                if ((dst) >= 0) {               \
                    (void)::close((dst));       \
                }                               \
                (dst) = (src);                  \
            }                                   \
        }
        switch (mRole) {
            case Process_Role_Child: {
                closeAndSet(in, mChildInFd[0]);
                closeAndSet(out, mChildOutFd[1]);
                closeAndSet(error, mChildErrFd[1]);
                closeIfGE0AndReset(mChildInFd[1]);
                closeIfGE0AndReset(mChildOutFd[0]);
                closeIfGE0AndReset(mChildErrFd[0]);
                ret = true;
                break;
            }
            case Process_Role_Parent: {
                closeAndSet(in, mChildInFd[1]);
                closeAndSet(out, mChildOutFd[0]);
                closeAndSet(error, mChildErrFd[0]);
                closeIfGE0AndReset(mChildInFd[0]);
                closeIfGE0AndReset(mChildOutFd[1]);
                closeIfGE0AndReset(mChildErrFd[1]);
                ret = true;
                break;
            }
            default: {
                break;
            }
        }
        return ret;
#undef closeAndSet
    }


    virtual bool
    setupIPCChannels(ProcessRoleT role) {
        bool ret = false;

        switch (mIpcType) {

            case Process_IPC_Pipe: {

                switch (role) {
                    case Process_Role_ParentBeforeFork: {
                        if (pipe(mChildInFd) != 0) {
                            perror("pipe");
                            goto pbDone;
                        }
                        if (pipe(mChildOutFd) != 0) {
                            perror("pipe");
                            goto pbDone;
                        }
                        if (pipe(mChildErrFd) != 0) {
                            perror("pipe");
                            goto pbDone;
                        }
                        ret = true;

                        pbDone:
                        break;
                    }

                    case Process_Role_Child: {
                        ret = setFds(
                            mChildInFd[0],
                            mChildOutFd[1],
                            mChildErrFd[1]);
                        break;
                    }

                    case Process_Role_Parent: {
                        ret = setFds(
                            mChildInFd[1],
                            mChildOutFd[0],
                            mChildErrFd[0]);
                        break;
                    }

                    default: {
                        break;
                    }
                }
                break;
            }

            case Process_IPC_Pty:
            case Process_IPC_PtyInteractive: {

                switch (role) {
                    case Process_Role_ParentBeforeFork: {
                        mMasterPtyFd = 
                            nata_PTYOpenMaster(mSlavePtyName,
                                               sizeof(mSlavePtyName));
                        if (mMasterPtyFd >= 0 &&
                            isValidString(mSlavePtyName) == true) {
                            ret = true;
                        }
                        break;
                    }

                    case Process_Role_Child: {
                        int in = -1, out = -1, err = -1;

                        closeIfGE0(mMasterPtyFd);

                        if ((in = open(mSlavePtyName, O_RDWR)) < 0) {
                            goto rcDone;
                        }
                        if ((out = dup(in)) < 0) {
                            goto rcDone;
                        }
                        if ((err = dup(in)) < 0) {
                            goto rcDone;
                        }

                        ret = setFds(in, out, err);

                        rcDone:
                        if (ret == false) {
                            closeIfGE0(in);
                            closeIfGE0(out);
                            closeIfGE0(err);
                        } else {
                            // in this mode, after parent - child
                            // sync, set tty sane.
                            (void)nata_TTYSetRawMode(in);
                        }
                        break;
                    }

                    case Process_Role_Parent: {
                        int in = -1, out = -1;

                        in = mMasterPtyFd;
                        if ((out = dup(in)) < 0) {
                            goto rpDone;
                        }

                        ret = setFds(in, out, -1);

                        rpDone:
                        if (ret == false) {
                            closeIfGE0(in);
                            closeIfGE0(out);
                        }
                        break;
                    }
                    
                    default: {
                        break;
                    }
                }
                break;
            }

            default: {
                break;
            }

        }

        return ret;
    }


    virtual int
    runChild(void) {
        return 0;
    }


    virtual int
    runParent(void) {
	return 0;
    }



private:


    inline int
    mRunChild(void) {
        if (mIpcType == Process_IPC_Pty ||
            mIpcType == Process_IPC_PtyInteractive) {
            (void)nata_TTYSetCanonicalMode(mChildInFd[1]);
            if (mIpcType == Process_IPC_Pty) {
                // If not interactive, disable local echo.
                (void)nata_TTYSetNoEchoMode(mChildInFd[1]);
            }
        }
        return runChild();
    }


    inline int
    mRunParent(void) {
        return runParent();
    }


    inline bool
    mSetUsing(bool v) {
        ScopedLock l(&mUsingLock);

        switch (v) {

            case true: {
                ReCheck:
                if (mIsDeleting == false) {
                    if (mIsUsing == true) {
                        mUsingCond.wait(&mUsingLock);
                        goto ReCheck;
                    } else {
                        mIsUsing = true;
                    }
                }
                break;
            }

            case false: {
                mIsUsing = false;
                mUsingCond.wakeAll();
                break;
            }

        }
        return mIsUsing;
    }


    inline bool
    mIsRunning(void) {
	if (mChildPid != (pid_t)-1) {
	    return (kill(mChildPid, 0) == 0) ? true : false;
	} else {
	    return false;
	}
    }


    inline bool
    mSetupIPCChannels(ProcessRoleT role) {
        bool ret = false;
        switch (role) {
            case Process_Role_ParentBeforeFork:
            case Process_Role_Parent:
            case Process_Role_Child: {
                int idx = (int)role;
                if (role != Process_Role_ParentBeforeFork) {
                    if (mIsIPCChannelsSetup[
                            (int)Process_Role_ParentBeforeFork] != true) {
                        goto rrDone;
                    }
                }
                ret = setupIPCChannels(role);
                mIsIPCChannelsSetup[idx] = ret;

                rrDone:
                if (ret == false) {
                    closeIfGE0AndReset(mChildInFd[0]);
                    closeIfGE0AndReset(mChildInFd[1]);
                    closeIfGE0AndReset(mChildOutFd[0]);
                    closeIfGE0AndReset(mChildOutFd[1]);
                    closeIfGE0AndReset(mChildErrFd[0]);
                    closeIfGE0AndReset(mChildErrFd[1]);
                }
                break;
            }
            default: {
                break;
            }
        }
        return ret;
    }


    inline bool
    mWait(bool doForce = false) {
	if (mRole == Process_Role_Child ||
            mChildPid == (pid_t)-1) {
            return false;
	}

        ScopedLock l(&mStatusLock);

        if (mSyncType == Process::Process_Sync_DontCare &&
            doForce == false) {
            mIsWaited = true;
            return mIsWaited;
        }

	if (mIsWaited == false) {
            if (mExitCode == EXITCODE_IMPOSSIBLE) {
                int stat;
                bool waitSt;

                if ((waitSt = ProcessJanitor::waitExit(mChildPid,
                                                       stat)) == true) {
                    mExitCode = WEXITSTATUS(stat);
                    dbgMsg("got an exit code %d.\n", mExitCode);
                }
            }
            mCleanupFds();
            mSetUsing(false);
            mIsWaited = true;
        }

	return mIsWaited;
    }


    inline bool
    mStop(int sig = SIGTERM) {
	if (mIsRunning() == true) {
	    (void)killpg(mChildPid, sig);
	}
	return mWait();
    }


    bool
    mStart(ProcessSyncT syncType, ProcessIPCT ipcType) {
        mSetUsing(true);

	bool ret = false;

	mStatusLock.lock();

	mChildPid = (pid_t)-1;
	mIsWaited = false;
        mIsFdsValid = false;

	mSyncType = syncType;
        mIpcType = ipcType;
	mRole = Process_Role_ParentBeforeFork;

        // Call setupIPCChannels() for opening pipes, pty, etc.,
        // before fork().
        if (mSetupIPCChannels(mRole) == false) {
            goto Done;
        }

	mChildPid = fork();
	if (mChildPid < 0) {
	    perror("fork");
	    goto Done;
	}

	if (mChildPid == 0) {
	    // No need to do this, but:
	    mStatusLock.unlock();

	    mRole = Process_Role_Child;
	    mChildPid = getpid();
	    (void)nata_safe_Setsid();

            if (mSetupIPCChannels(mRole) == false) {
                ::exit((int)Process_Exit_IPCSetupFailure);
            }

	    bool isInDuped = false;
	    bool isOutDuped = false;
	    bool isErrDuped = false;

	    int i;

	    // File descriptors setup.
	    int inFd = mChildInFd[0];
	    if (isValidString(mStdInFile) == true) {
		if ((i = open(mStdInFile, O_RDONLY)) >= 0) {
                    closeIfGE0(inFd);
		    inFd = i;
		}
            }
	    if (inFd != 0) {
		if (dup2(inFd, 0) < 0) {
		    perror("dup2");
		} else {
		    (void)close(inFd);
		    isInDuped = true;
		}
	    }

	    int outFd = mChildOutFd[1];
	    if (isValidString(mStdOutFile) == true) {
		if ((i = open(mStdOutFile,
			      O_WRONLY|O_CREAT|O_TRUNC, 0644)) >= 0) {
                    closeIfGE0(outFd);
		    outFd = i;
		}
            }
	    if (outFd != 1) {
		if (dup2(outFd, 1) < 0) {
		    perror("dup2");
		} else {
		    (void)close(outFd);
		    isOutDuped = true;
		}
	    }

	    int errFd = mChildErrFd[1];
	    if (isValidString(mStdErrFile) == true) {
		if ((i = open(mStdErrFile,
			      O_WRONLY|O_CREAT|O_TRUNC, 0644)) >= 0) {
                    closeIfGE0(errFd);
		    errFd = i;
		}
            }
	    if (errFd != 2) {
		if (dup2(errFd, 2) < 0) {
		    perror("dup2");
		} else {
		    (void)close(errFd);
		    isErrDuped = true;
		}
	    }

#if 0
	    // stdin/stdout/stderr setup.
	    if (isInDuped == true) {
		mStdInFd = fdopen(inFd, "r");
		if (mStdInFd == NULL) {
		    mStdInFd = stdin;
		} else {
                    stdin = mStdInFd;
                }
	    } else {
		mStdInFd = stdin;
	    }

	    if (isOutDuped == true) {
		mStdOutFd = fdopen(outFd, "w");
		if (mStdOutFd == NULL) {
		    mStdOutFd = stdout;
		} else {
                    stdout = mStdOutFd;
                }
	    } else {
		mStdOutFd = stdout;
	    }

	    if (isErrDuped == true) {
		mStdErrFd = fdopen(errFd, "w");
		if (mStdErrFd == NULL) {
		    mStdErrFd = stderr;
		} else {
                    (void)setvbuf(mStdErrFd, NULL, _IONBF, 0);
                    stderr = mStdErrFd;
                }
	    } else {
		mStdErrFd = stderr;
	    }
#else
            mStdInFd = stdin;
            mStdOutFd = stdout;
            mStdErrFd = stderr;
#endif

	    // Write pid to parent.
	    if (write(1, (void *)&mChildPid, sizeof(pid_t)) !=
		sizeof(pid_t)) {
		::exit((int)Process_Exit_IPCSyncChildWriteFailure);
	    }

	    // Read pid from parent, for startup sync.
	    pid_t pid;
	    (void)read(0, (void *)&pid, sizeof(pid_t));
            if (pid != mChildPid) {
                ::exit((int)Process_Exit_IPCSyncChildReadFailure);
            }

            // Set all IO channels are go.
            mIsFdsValid = true;

	    // Change directory.
	    if (isValidString(mCWD) == true) {
		if (::chdir(mCWD) < 0) {
                    perror("chdir");
                }
	    }
	    // Finally, do child job.
	    int eCode = mRunChild();

	    ::exit(eCode);
	} else {
	    mRole = Process_Role_Parent;

            if (mSetupIPCChannels(mRole) == false) {
                goto ErrorReturnAfterFork;
            }

            if (mChildInFd[1] >= 0) {
                mStdInFd = fdopen(mChildInFd[1], "w");
                if (mStdInFd == NULL) {
                    goto ErrorReturnAfterFork;
                }
            }
            if (mChildOutFd[0] >= 0) {
                mStdOutFd = fdopen(mChildOutFd[0], "r");
                if (mStdOutFd == NULL) {
                    goto ErrorReturnAfterFork;
                }
            }
            if (mChildErrFd[0] >= 0) {
                mStdErrFd = fdopen(mChildErrFd[0], "r");
                if (mStdErrFd == NULL) {
                    goto ErrorReturnAfterFork;
                }
            }
		
	    // Read pid from child for startup sync.
	    pid_t pid;
	    if (read(mChildOutFd[0], (void *)&pid, sizeof(pid_t)) !=
		sizeof(pid_t)) {
		goto ErrorReturnAfterFork;
	    }
	    if (pid != mChildPid) {
		goto ErrorReturnAfterFork;
	    }

	    // Request ProcessJanitor to report exit status.
            if (ProcessJanitor::requestReport(mChildPid,
                                              &mStatusLock) != true) {
                dbgMsg("process janitor failed to regist a report "
                       "request for pid %d.\n", mChildPid);
	    }

	    // Write pid to child for startup sync.
            if (write(mChildInFd[1], (void *)&mChildPid, sizeof(pid_t)) !=
		sizeof(pid_t)) {
		goto ErrorReturnAfterFork;
	    }

            // Set all IO channels are go.
            mIsFdsValid = true;


            // Now we are ready for an action as a parent.
	    if (mSyncType == Process_Sync_Synchronous) {
                // Do a parent side job by oneself.
                mRunParent();
                mStatusLock.unlock();
                return mWait();
	    } else {
                // Use a thread to run the job.
                mParentThread *mPPtr = new mParentThread(this);
                mStatusLock.unlock();
                mPPtr->start(false, true);
                return true;
	    }

            // Error return.
	    ErrorReturnAfterFork:
            mStatusLock.unlock();
	    (void)mStop(SIGKILL);
            return ret;

            Done:
            mStatusLock.unlock();
            return ret;
        }
    }



protected:


    inline bool
    waitReadable(bool &inReadable, 
                 bool &outReadable,
                 bool &errReadable,
                 int64_t uSec = -1) {

	bool ret = false;
        int nSels;

	struct timeval to;
	struct timeval *tPtr = NULL;
	fd_set rFds;

	FD_ZERO(&rFds);
	int maxFd = -1;

	if (uSec >= 0) {
	    to.tv_sec = (unsigned int)(uSec / 1000000LL);
	    to.tv_usec = (unsigned int)(uSec % 1000000LL);
	    tPtr = &to;
	} else {
	    tPtr = NULL;
	}

        {
            ScopedLock li(&mIOLock);

            if (mIsFdsValid == false) {
                errno = EBADF;
                perror("select");
                goto BeforeSelect;
            }

            if (mRole == Process_Role_Parent) {
                if (mIpcType == Process_IPC_Pipe) {
                    bool setOut = false;
                    bool setErr = false;
                    if (mChildOutFd[0] >= 0) {
                        FD_SET(mChildOutFd[0], &rFds);
                        setOut = true;
                    }
                    if (mChildErrFd[0] >= 0) {
                        FD_SET(mChildErrFd[0], &rFds);
                        setErr = true;
                    }
                    if (setOut == true && setErr == true) {
                        maxFd =
                            (mChildOutFd[0] > mChildErrFd[0]) ? 
                            mChildOutFd[0] : mChildErrFd[0];
                    } else if (setOut == true && setErr == false) {
                        maxFd = mChildOutFd[0];
                    } else if (setOut == false && setErr == true) {
                        maxFd = mChildErrFd[0];
                    }
                } else if (mIpcType == Process_IPC_Pty ||
                           mIpcType == Process_IPC_PtyInteractive) {
                    if (mChildOutFd[0] >= 0) {
                        FD_SET(mChildOutFd[0], &rFds);
                        maxFd = mChildOutFd[0];
                    }
                }
            } else {
                FD_SET(0, &rFds);
                maxFd = 0;
            }

            BeforeSelect:
            ;;
        }

        if (maxFd < 0) {
            goto Done;
        }

        nSels = select(maxFd + 1, &rFds, NULL, NULL, tPtr);
	if (nSels < 0) {
	    perror("select");
	    goto Done;
	} else if (nSels == 0) {
	    goto Done;
	}

	if (mRole == Process_Role_Parent) {
            inReadable = false;
	    if (FD_ISSET(mChildOutFd[0], &rFds)) {
                outReadable = true;
	    }
            if (mIpcType == Process_IPC_Pipe) {
                if (FD_ISSET(mChildErrFd[0], &rFds)) {
                    errReadable = true;
                }
            } else {
                errReadable = false;
            }
	    ret = true;
	} else {
            outReadable = false;
            errReadable = false;
	    if (FD_ISSET(0, &rFds)) {
                inReadable = true;
		ret = true;
	    }
	}

	Done:
	return ret;
    }


    // A child reads from a parent.
    inline ssize_t
    readIn(void *buf, size_t nBytes) {
	if (mRole != Process_Role_Child) {
	    return -1;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("read");
            return -1;
        }
	return read(0, buf, nBytes);
    }


    // A child reads a parent.
    inline char *
    getsIn(char *buf, int size) {
	if (mRole != Process_Role_Child) {
	    return NULL;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("read");
            return NULL;
        }
	return fgets(buf, size, mStdInFd);
    }


    // A parent writes to a child.
    inline ssize_t
    writeIn(void *buf, size_t nBytes) {
	if (mRole != Process_Role_Parent) {
	    return -1;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("write");
            return -1;
        }
	return write(mChildInFd[1], buf, nBytes);
    }
	

    // A parent prints to a child.
    inline int
    printIn(const char *fmt, ...) __attr_format_printf__(2, 3) {
	int ret = -1;

	if (mRole != Process_Role_Parent) {
	    return -1;
	}

	va_list args;
	va_start(args, fmt);
	va_end(args);

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("write");
            return -1;
        }
	ret = vfprintf(mStdInFd, fmt, args);
	fflush(mStdInFd);

	return ret;
    }
	

    // A parent reads a child's stdout.
    inline ssize_t
    readOut(void *buf, size_t nBytes) {
	if (mRole != Process_Role_Parent) {
	    return -1;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("read");
            return -1;
        }
	return read(mChildOutFd[0], buf, nBytes);
    }


    // A parent reads a child's stdout.
    inline char *
    getsOut(char *buf, int size) {
	if (mRole != Process_Role_Parent) {
	    return NULL;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("read");
            return NULL;
        }
	return fgets(buf, size, mStdOutFd);
    }


    // A parent reads a child's stderr.
    inline ssize_t
    readErr(void *buf, size_t nBytes) {
	if (mRole != Process_Role_Parent) {
	    return -1;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("read");
            return -1;
        }
	return read(mChildErrFd[0], buf, nBytes);
    }


    // A parent reads a child's stderr.
    inline char *
    getsErr(char *buf, int size) {
	if (mRole != Process_Role_Parent) {
	    return NULL;
	}

        ScopedLock l(&mIOLock);
        if (mIsFdsValid == false) {
            errno = EBADF;
            perror("read");
            return NULL;
        }
	return fgets(buf, size, mStdErrFd);
    }



public:
    inline bool
    stop(int sig = SIGTERM) {
	return mStop(sig);
    }


    inline bool
    start(ProcessSyncT syncType = Process_Sync_Synchronous,
          ProcessIPCT ipcType = Process_IPC_Pipe) {
        return mStart(syncType, ipcType);
    }


    inline bool
    wait(void) {
        return mWait();
    }


    inline void
    setCwd(const char *dir) {
        ScopedLock l(&mStatusLock);

	if (isValidString(mCWD) == true) {
	    (void)free((void *)mCWD);
	}
	if (isValidString(dir) == true) {
	    mCWD = strdup(dir);
	} else {
	    mCWD = dir;
	}
    }


    inline void
    setStdIn(const char *file) {
        ScopedLock l(&mStatusLock);

	if (isValidString(mStdInFile) == true) {
	    (void)free((void *)mStdInFile);
        }
	if (isValidString(file) == true) {
	    mStdInFile = strdup(file);
	} else {
	    mStdInFile = file;
        }
    }


    inline void
    setStdOut(const char *file) {
        ScopedLock l(&mStatusLock);

	if (isValidString(mStdOutFile) == true) {
	    (void)free((void *)mStdOutFile);
	}
	if (isValidString(file) == true) {
	    mStdOutFile = strdup(file);
	} else {
	    mStdOutFile = file;
	}
    }


    inline void
    setStdErr(const char *file) {
        ScopedLock l(&mStatusLock);

	if (isValidString(mStdErrFile) == true) {
	    (void)free((void *)mStdErrFile);
	}
	if (isValidString(file) == true) {
	    mStdErrFile = strdup(file);
	} else {
	    mStdErrFile = file;
	}
    }


    inline pid_t
    getChildPid(void) {
	return mChildPid;
    }


    inline int
    getExitCode(void) {
        int ret = EXITCODE_IMPOSSIBLE;
        if (mWait() == true) {
            ScopedLock l(&mStatusLock);
            ret = mExitCode;
        }
        mCleanupStatus();
	return ret;
    }


    inline bool
    isIPCChannelsSetup(ProcessRoleT role) {
        if (isValidRoleRange(role) == true) {
            return mIsIPCChannelsSetup[(int)role];
        }
        return false;
    }


    inline bool
    isRunning(void) {
        return mIsRunning();
    }



    Process(const char *cwd = NULL,
	    const char *stdInFile = NULL,
	    const char *stdOutFile = NULL,
	    const char *stdErrFile = NULL) :
	// mStatusLock,
        // mCleanupLock,
        // mIOLock,
        // mUsingLock,
        // mUsingCond,
	mCWD((isValidString(cwd) == true) ?
               strdup(cwd) : cwd),
        mStdInFile((isValidString(stdInFile) == true) ?
               strdup(stdInFile) : stdInFile),
	mStdOutFile((isValidString(stdOutFile) == true) ?
                strdup(stdOutFile) : stdOutFile),
	mStdErrFile((isValidString(stdErrFile) == true) ?
                strdup(stdErrFile) : stdErrFile),
	mRole(Process_Role_Unknown),
	// mChild{In|Out|Err}Fd,
	mStdInFd(NULL),
	mStdOutFd(NULL),
	mStdErrFd(NULL),
	mSyncType(Process::Process_Sync_Unknown),
	mIpcType(Process::Process_IPC_Unknown),
	mChildPid((pid_t)-1),
	mExitCode(EXITCODE_IMPOSSIBLE),
	mIsWaited(false),
        mIsUsing(false),
        // mIsIPCChannelsSetup,
        mIsDeleting(false),
        mIsFdsValid(false),
        // mSlavePtyName,
        mMasterPtyFd(-1) {

	mChildInFd[0] = -1;
	mChildInFd[1] = -1;
	mChildOutFd[0] = -1;
	mChildOutFd[1] = -1;
	mChildErrFd[0] = -1;
	mChildErrFd[1] = -1;

	setCwd(cwd);
	setStdIn(stdInFile);
	setStdOut(stdOutFile);
	setStdErr(stdErrFile);

        mIsIPCChannelsSetup[0] = false;
        mIsIPCChannelsSetup[1] = false;
        mIsIPCChannelsSetup[2] = false;
        mIsIPCChannelsSetup[3] = false;

        (void)memset((void *)mSlavePtyName, 0, sizeof(mSlavePtyName));
    }


    virtual
    ~Process(void) {
        mStatusLock.lock();
        mIsDeleting = true;
        mStatusLock.unlock();

	mStop(SIGKILL);

        mCleanupFds();

        freeIfValidString(mCWD);
        freeIfValidString(mStdInFile);
        freeIfValidString(mStdOutFile);
        freeIfValidString(mStdErrFile);

        mSetUsing(false);
    }


#ifdef NATA_API_POSIX
    static inline void
    blockAllSignals(void) {
        Thread::blockAllSignals();
    }


    static inline void
    unblockAllSignals(void) {
        Thread::unblockAllSignals();
    }
#endif // NATA_API_POSIX


private:
    Process(const Process &obj);
    Process operator = (const Process &obj);
};


#undef closeIfGE0
#undef closeIfGE0AndReset

#undef isValidRoleRange
#undef isValidSyncRange
#undef isValidIPCRange


#endif // ! __PROCESS_H__

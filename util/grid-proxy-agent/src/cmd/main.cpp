#include <nata/nata_rcsid.h>
__rcsId("$Id$")


#include <nata/libnata.h>

#include <nata/Process.h>
#include <nata/ProcessJanitor.h>
#include <nata/BoundedBlockingQueue.h>

#include <nata/Thread.h>
#include <nata/SignalThread.h>

#include <nata/nata_perror.h>





static const char *sMyName = NULL;

static char sPasswd[4096];
static size_t sPasswdLen = 0;

static int32_t sInterval = 4 * 3600;	// 4 h

static bool sDebug = false;

class GridProxyAgent;
static GridProxyAgent *gpaPtr = NULL;
static Mutex endLock;





class GridProxyInit: public Process {


private:
    char mPasswd[4096];
    size_t mPasswdLen;
    char *mCwd;
    bool mVerbose;


protected:
    int
    runChild() {
        const char * const argv[] = {
            "grid-proxy-init",
	    "-bits",
	    "1024",
            NULL
        };
        Process::unblockAllSignals();

        ::execvp((const char *)argv[0], (char * const *)argv);
        ::perror("execvp");
        ::exit(1);
    }


    int
    runParent() {
        if (writeIn(mPasswd, mPasswdLen) != (ssize_t)mPasswdLen) {
            return 1;
        }

	bool doLoop = true;
        bool doWaitOut = true;
        bool doWaitErr = (getProcessIPCType() != Process_IPC_Pipe) ?
            false : true;
        bool in, out, err;
        char buf[65536];
        ssize_t n;

        (void)memset((void *)buf, 0, sizeof(buf));

        while (doLoop == true &&
	       (doWaitOut == true ||
		doWaitErr == true)) {

            out = false;
            err = false;

            if ((doLoop = waitReadable(in, out, err)) == true) {
                if (doWaitOut == true && out == true) {
                    if ((n = readOut(buf, sizeof(buf))) > 0) {
                        if (mVerbose == true) {
                            (void)write(1, buf, n);
                        }
                    } else {
                        doWaitOut = false;
                    }
                }
                if (doWaitErr == true && err == true) {
                    if ((n = readErr(buf, sizeof(buf))) > 0) {
                        if (mVerbose == true) {
                            (void)write(2, buf, n);
                        }
                    } else {
                        doWaitErr = false;
                    }
                }
            }
        }
        return 0;
    }


public:
    GridProxyInit(const char *cwd, const char *passwd, size_t passLen) :
        Process(NULL, NULL, NULL, NULL),
        mPasswdLen(passLen),
        mCwd((isValidString(cwd) == true) ? strdup(cwd) : NULL),
        mVerbose(false) {
        setCwd(mCwd);
        (void)memset((void *)mPasswd, 0, sizeof(mPasswd));
        (void)memcpy((void *)mPasswd, (void *)passwd,
                     (sizeof(mPasswd) < passLen) ? sizeof(mPasswd) : passLen);
    }


    ~GridProxyInit(void) {
        freeIfNotNULL(mCwd);
    }


    bool
    setVerbose(bool v) {
        mVerbose = v;
        return v;
    }
};


typedef BoundedBlockingQueue<int> IntQ;


class GridProxyAgent: public Thread {
#define STOP_AND_EXIT	-1


private:
    IntQ mQ;
    GridProxyInit *mGPIPtr;
    bool mIsWorking;
    Mutex mWorkingLock;
    WaitCondition mCond;
    bool mIsFirst;
    bool mVerbose;


    void
    mSetWorking(bool v) {
        ScopedLock l(&mWorkingLock);
        mIsWorking = v;
        mIsFirst = false;
        mCond.wakeAll();
    }


    int
    mRun(bool verbose) {
        bool sVerbose = mVerbose;

        mVerbose = verbose;
        mGPIPtr->setVerbose(mVerbose);

        mGPIPtr->start(Process::Process_Sync_Synchronous,
                       Process::Process_IPC_Pty);
        mGPIPtr->wait();

        mVerbose = sVerbose;
        mGPIPtr->setVerbose(mVerbose);

        return mGPIPtr->getExitCode();
    }


protected:
    int
    run(void) {
        int val;

        int ret = 1;

        mSetWorking(true);
        while (mQ.get(val) == true) {
            if (val == STOP_AND_EXIT) {
                ret = 0;
                break;
            }
            if ((ret = mRun(mVerbose)) != 0) {
                if (mVerbose == true) {
                    fprintf(stderr, "%s: Got an error exit code: %d\n",
                            sMyName, ret);
                }
                break;
            }
        }
        mSetWorking(false);
        mQ.stop();

        return ret;
    }


public:
    int
    check(void) {
        return mRun(true);
    }


    bool
    update(void) {
        return mQ.put(0);
    }


    void
    stop(void) {
        (void)mQ.put(STOP_AND_EXIT);
    }


    bool
    isWorking(void) {
        ScopedLock l(&mWorkingLock);
        ReCheck:
        if (mIsFirst == true) {
            mCond.wait(&mWorkingLock);
            goto ReCheck;
        }
        return mIsWorking;
    }


    GridProxyAgent(const char *cwd,
                   const char *passwd,
                   size_t passLen) :
        mGPIPtr(new GridProxyInit(cwd, passwd, passLen)),
        mIsWorking(false),
        mIsFirst(true),
        mVerbose(false) {
    }


    ~GridProxyAgent(void) {
        mQ.stop();
        deleteIfNotNULL(mGPIPtr);
    }


    bool
    setVerbose(bool v) {
        mVerbose = v;
        return v;
    }
};





static void
finalize(int sig) {
    (void)sig;
    static bool isCalled = false;
    if (gpaPtr != NULL) {
        ScopedLock l(&endLock);
        if (isCalled == false) {
            gpaPtr->stop();
            gpaPtr->wait();
            isCalled = true;
            int eCode = gpaPtr->exitCode();
            delete gpaPtr;
            exit(eCode);
        }
    }
}


static void
setupSignals() {
    SignalThread *st = new SignalThread();

    st->ignore(SIGPIPE);
    st->setHandler(SIGINT, finalize);
    st->setHandler(SIGHUP, finalize);
    st->setHandler(SIGTERM, finalize);
    st->start(false, false);
}


static void
progName(char *name) {
    sMyName = strrchr(name, '/');
    if (sMyName != NULL) {
        sMyName++;
    } else {
        sMyName = name;
    }
}


static void
usage(void) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s [-i #] [-d]\n", sMyName);
    fprintf(stderr, "\nwhere:\n");
    fprintf(stderr, "\t-i #:\tspecify an update interval in sec. "
            "(default: 4 hour)\n");
    fprintf(stderr, "\t-d:\tdebug mode.\n");
}


static void
parseArgs(int argc, char *argv[]) {
    (void)argc;
    while (*argv != NULL) {
        if (strcmp("-i", *argv) == 0) {
            if (isValidString(*(argv + 1)) == true) {
                argv++;
                int32_t val;
                if (nata_ParseInt32(*argv, &val) == true) {
                    sInterval = val;
                }
            }
        } else if (strcmp("-d", *argv) == 0) {
            sDebug = true;
        } else if (strcmp("-?", *argv) == 0 ||
                   strcmp("-h", *argv) == 0) {
            usage();
            exit(0);
        }
        argv++;
    }

    if (sInterval < 10) {
        sInterval = 10;
    }
}


int
main(int argc, char *argv[]) {
    char tmpPasswd[4096];
    uint64_t n = 0;
    int checkECode = -INT_MAX;

    progName(argv[0]);

    parseArgs(argc - 1, argv + 1);

    fprintf(stderr, "Enter your pass phrase: ");
    if (nata_TTYGetPassword(tmpPasswd, sizeof(tmpPasswd) - 1) != true) {
        fprintf(stderr, "%s: error: Failed to get an pass phrase.\n",
                sMyName);
        exit(1);
    } else {
        snprintf(sPasswd, sizeof(sPasswd), "%s\n", tmpPasswd);
        (void)memset((void *)tmpPasswd, 0, sizeof(tmpPasswd));
        sPasswdLen = strlen(sPasswd);
    }
    fprintf(stderr, "\n");

    if (sDebug == false) {
        nata_Daemonize();
    }

    nata_InitializeLogger(emit_Unknown, "", true, false,
                              sDebug == true ? 1 : 0);
    setupSignals();
    ProcessJanitor::initialize();

    gpaPtr = new GridProxyAgent("/", sPasswd, sPasswdLen);
    if (gpaPtr == NULL) {
        return 1;
    }
    gpaPtr->setVerbose(sDebug);

    if ((checkECode = gpaPtr->check()) != 0) {
        return 1;
    }

    gpaPtr->start();

    while (gpaPtr->isWorking() == true) {
        if (n % (uint64_t)sInterval == 0) {
            if (gpaPtr->update() == false) {
                break;
            }
        }
        sleep(1);
        n++;
    }

    finalize(1);

    return 0;
}

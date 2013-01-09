#include <nata/nata_rcsid.h>

#include <nata/nata_includes.h>

#include <nata/nata_macros.h>

#include <nata/Mutex.h>
#include <nata/WaitCondition.h>
#include <nata/Thread.h>

#include <nata/nata_perror.h>





extern int end;





namespace ThreadStatics {


    __rcsId("$Id$")





#ifdef NATA_API_POSIX


    static bool sIsInitialized = false;
    static sigset_t sFullSignalSet;


    static inline void
    sInitializeSignalMask(void) {
        (void)sigfillset(&sFullSignalSet);        
#if defined(SIGRTMIN) && defined(SIGRTMAX)
        //
        // Exclude realtime signals (also for Linux's thread
        // cancellation).
        //
        {
            int i;
            int sMin = SIGRTMIN;
            int sMax = SIGRTMAX;
            for (i = sMin; i < sMax; i++) {
                (void)sigdelset(&sFullSignalSet, i);
            }
        }
#endif // SIGRTMIN && SIGRTMAX
        //
        // And exclude everything that seems for thread-related.
        //
#ifdef SIGWAITING
        (void)sigdelset(&sFullSignalSet, SIGWAITING);
#endif // SIGWAITING
#ifdef SIGLWP
        (void)sigdelset(&sFullSignalSet, SIGLWP);
#endif // SIGLWP
#ifdef SIGFREEZE
        (void)sigdelset(&sFullSignalSet, SIGFREEZE);
#endif // SIGFREEZE
#ifdef SIGCANCEL
        (void)sigdelset(&sFullSignalSet, SIGCANCEL);
#endif // SIGCANCEL
    }


    class ThreadInitializer {
    public:
        ThreadInitializer(void) {
            if (sIsInitialized == false) {
                sInitializeSignalMask();
                sIsInitialized = true;
            }
        }

    private:
        ThreadInitializer(const ThreadInitializer &obj);
        ThreadInitializer operator = (const ThreadInitializer &obj);
    };


    static ThreadInitializer sTi;


    void
    sBlockAllSignals(void) {
        (void)pthread_sigmask(SIG_SETMASK, &sFullSignalSet, NULL);
    }


    void
    sUnblockAllSignals(void) {
        sigset_t empty;
        (void)sigemptyset(&empty);
        (void)pthread_sigmask(SIG_SETMASK, &empty, NULL);
    }


    sigset_t
    sGetFullSignalSet(void) {
        return sFullSignalSet;
    }


#endif // NATA_API_POSIX





    bool
    sIsOnHeap(void *addr) {
#if defined(NATA_API_POSIX)
        return ((((uintptr_t)&end) <= ((uintptr_t)addr)) &&
                (((uintptr_t)addr) < ((uintptr_t)sbrk((intptr_t)0)))) ?
            true : false;
#elif defined(NATA_API_WIN32API)
        /*
         * Not yet.
         */
        (void)addr;
        return false;
#else
#error Unknown/Non-supported API.
#endif /* NATA_API_POSIX, NATA_API_WIN32API */
    }


    void
    sCancelHandler(void *ptr) {
        Thread *tPtr = (Thread *)ptr;

#ifdef __THREAD_DEBUG__
        dbgMsg("Enter.\n");
#endif // __THREAD_DEBUG__
        if (tPtr != NULL) {
            tPtr->mCancelLock.lock();
            tPtr->mIsCanceled = true;
            tPtr->pSetState(Thread::State_Stopped);
            tPtr->mCancelLock.unlock();
            tPtr->pExit(-1);
        }
#ifdef __THREAD_DEBUG__
        dbgMsg("Leave.\n");
#endif // __THREAD_DEBUG__
    }


    void *
    sThreadEntryPoint(void *ptr) {
        Thread *tPtr = (Thread *)ptr;

#ifdef __THREAD_DEBUG__
        dbgMsg("Enter.\n");
#endif // __THREAD_DEBUG__

#ifdef NATA_API_POSIX
        sBlockAllSignals();
#endif // NATA_API_POSIX

        if (tPtr != NULL) {

            tPtr->pSynchronizeStart();

            int ret = -1;
            volatile bool cleanFinish = false;

            tPtr->disableCancellation();
            {
                if (tPtr->mDetachFailure == true) {
                    tPtr->enableCancellation();
                    goto Done;
                }

                tPtr->pSetState(Thread::State_Started);
#ifdef NATA_API_WIN32API
                tPtr->mWinTid = GetCurrentThreadId();
#endif /* NATA_API_WIN32API */
                
            }
            tPtr->enableCancellation();

            pthread_cleanup_push(sCancelHandler, ptr);
            {
                ret = tPtr->run();
                cleanFinish = true;
            }
            pthread_cleanup_pop((cleanFinish == true) ? 0 : 1);

            Done:
            tPtr->pExit(ret);
        }

#ifdef __THREAD_DEBUG__
        dbgMsg("Leave.\n");
#endif // __THREAD_DEBUG__
        return NULL;
    }


}

#include "qmlavthread.h"

void QmlAVWorkerThread::requestInterrupt()
{
    if (m_worker) {
        m_worker->requestInterrupt();
    }

    m_loopInterruptRequested.store(true, std::memory_order_release);
}

void QmlAVWorkerThread::run()
{
    assert(m_worker);

    try {
        while (!m_loopInterruptRequested.load(std::memory_order_acquire)) {
            QmlAVLoopController ctrl = m_worker->invoke();
            if (ctrl.isBreak()) {
                break;
            }

            std::this_thread::yield();
            ctrl.sleep();
        }
    } catch (const std::exception &e) {
        logWarning() << QString("Exception thrown in worker thread: %1").arg(e.what());
    } catch (...) {
        logWarning() << "Unknown exception thrown in worker thread";
    }

    setRunning(false);
    m_waitCond.notify_all();
}

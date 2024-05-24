#include "qmlavthread.h"

void QmlAVWorkerThread::requestInterruption()
{
    if (m_worker) {
        m_worker->requestInterruption();
    }

    m_loopInterruptionRequested.store(true, std::memory_order_release);
}

void QmlAVWorkerThread::run()
{
    assert(m_worker);

    while (!m_loopInterruptionRequested.load(std::memory_order_acquire)) {
        QmlAVLoopController ctrl = m_worker->invoke();
        if (ctrl.isBreak()) {
            break;
        }

        std::this_thread::yield();
        ctrl.sleep();
    }

    setRunning(false);
    m_waitCond.notify_all();
}

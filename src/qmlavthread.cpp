#include "qmlavthread.h"

void QmlAVWorkerThread::requestInterruption()
{
    QThread::requestInterruption();

    if (m_worker) {
        m_worker->requestInterruption();
    }

    m_loopInterruptionRequested.store(true, std::memory_order_release);
}

void QmlAVWorkerThread::run()
{
    if (!m_worker) {
        return;
    }

    do {
        QmlAVLoopController ctrl = m_worker->invoke();
        if (ctrl.isInterrupt()) {
            break;
        }

        QThread::yieldCurrentThread();

        ctrl.sleep();

    } while (!m_loopInterruptionRequested.load(std::memory_order_acquire));
}

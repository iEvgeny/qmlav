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
    while (!m_loopInterruptionRequested.load(std::memory_order_acquire)) {
        if (m_worker) {
            QmlAVLoopController ctrl = m_worker->invoke();
            if (ctrl.isBreak()) {
                break;
            }

            QThread::yieldCurrentThread();

            ctrl.sleep();
        }
    }
}

#ifndef QMLAVTHREAD_H
#define QMLAVTHREAD_H

#include <QThread>

#include "qmlavqueue.h"
#include "qmlavutils.h"

class QmlAVLoopController
{
public:
    enum LoopControl {
        Interrupt,
        Continue
    };

    QmlAVLoopController(int64_t sleep = 0) : QmlAVLoopController(Continue, sleep) { }
    QmlAVLoopController(LoopControl ctrl, int64_t sleep = 0)
    {
        if (ctrl == Interrupt) {
            m_ctrl = -1;
        } else if (ctrl == Continue) {
            m_ctrl = std::max<int64_t>(0, sleep);
        }
    }

    bool isInterrupt() const { return m_ctrl < 0; }
    bool isContinue() const { return m_ctrl >= 0; }
    void sleep() const { QThread::usleep(m_ctrl); }

private:
    int64_t m_ctrl;
};

class QmlAVAbstractWorker
{
public:
    QmlAVAbstractWorker() { }
    virtual ~QmlAVAbstractWorker() { }

    virtual QmlAVLoopController invoke() = 0;
    virtual void *results() { return nullptr; }
    virtual void requestInterruption() { }
};

template<typename Callable, typename Result = QmlAVUtils::InvokeResult<Callable>>
class QmlAVWorkerResultImpl : public QmlAVAbstractWorker
{
public:
    // TODO: Maybe use std::any instead of void*?
    virtual void *results() override final {
        if constexpr (!std::is_void_v<Result>) {
            return &m_results;
        }

        return nullptr;
    }

    virtual void requestInterruption() override {
        m_results.setWait(false);
    }

protected:
    template <typename URef>
    void setResult(URef &&result) {
        m_results.enqueue(std::forward<URef>(result));
    }

private:
    QmlAVQueue<Result> m_results;
};

template<typename Callable>
class QmlAVWorkerResultImpl<Callable, void>  : public QmlAVAbstractWorker { };

template<typename Callable>
class QmlAVWorkerResultImpl<Callable, QmlAVLoopController>  : public QmlAVAbstractWorker { };

template<typename Callable>
class QmlAVWorkerInvokeImpl : public QmlAVWorkerResultImpl<Callable>
{
protected:
    using ArgsTuple = QmlAVUtils::InvokeArgsTuple<Callable>;
    using Result = QmlAVUtils::InvokeResult<Callable>;

public:
    QmlAVWorkerInvokeImpl(Callable &&callable)
        : m_callable(std::forward<Callable>(callable)) { }

protected:
    template <typename URef>
    QmlAVLoopController invokeImpl(URef &&args) {
        if constexpr (std::is_same_v<Result, QmlAVLoopController>) {
            static_assert(std::tuple_size_v<ArgsTuple> == 0,
                          "Any arguments are not allowed for loops. Use the capture list for lambda functions.");
            return std::apply(&QmlAVUtils::FunctionTraits<Callable>::template invoke<Callable>, std::forward_as_tuple(m_callable));
        } else {
            auto data = std::tuple_cat(std::forward_as_tuple(m_callable), std::forward<URef>(args));

            if constexpr (std::is_void_v<Result>) {
                std::apply(&QmlAVUtils::FunctionTraits<Callable>::template invoke<Callable>, data);
            } else {
                auto result = std::apply(&QmlAVUtils::FunctionTraits<Callable>::template invoke<Callable>, data);
                this->setResult(std::move(result));
            }
        }

        return QmlAVLoopController::Interrupt;
    }

private:
    Callable m_callable;
};

template<typename Callable, typename ...Args>
class QmlAVWorker : public QmlAVWorkerInvokeImpl<Callable>
{
public:
    // NOTE: Here and below, we don't rely on "Callable" deduction, so we don't use universal/forwarding references.
    QmlAVWorker(Callable callable, Args ...args)
        : QmlAVWorkerInvokeImpl<Callable>(std::forward<Callable>(callable))
        , m_args(std::forward<Args>(args)...) { }

    virtual QmlAVLoopController invoke() override final {
        return this->invokeImpl(m_args);
    }

private:
    typename QmlAVWorkerInvokeImpl<Callable>::ArgsTuple m_args;
};

template<typename Callable, typename ArgsQueue>
class QmlAVWorker<Callable, std::shared_ptr<ArgsQueue>> : public QmlAVWorkerInvokeImpl<Callable>
{
    static_assert(std::is_same_v<ArgsQueue, QmlAVQueue<typename QmlAVWorkerInvokeImpl<Callable>::ArgsTuple>>,
                  "This implementation is only for \"std::shared_ptr<QmlAVQueue<InvokeArgsTuple<Callable>>>\" type.");

public:
    QmlAVWorker(Callable callable, const std::shared_ptr<ArgsQueue> &argsQueue)
        : QmlAVWorkerInvokeImpl<Callable>(std::forward<Callable>(callable))
        , m_argsQueue(argsQueue) { }

    virtual QmlAVLoopController invoke() override final {
        typename QmlAVWorkerInvokeImpl<Callable>::ArgsTuple args;

        if (m_argsQueue->dequeue(args)) {
            this->invokeImpl(args);
            return QmlAVLoopController::Continue;
        }

        return QmlAVLoopController::Interrupt;
    }

    virtual void requestInterruption() override final {
        if (m_argsQueue) {
            m_argsQueue->setWait(false);
        }
    }

private:
    std::shared_ptr<ArgsQueue> m_argsQueue;
};

class QmlAVWorkerThread : public QThread
{
    Q_OBJECT

public:
    QmlAVWorkerThread(std::unique_ptr<QmlAVAbstractWorker> worker)
        : QThread()
        , m_worker(std::move(worker))
        , m_loopInterruptionRequested(false) { }

    void *results() {
        if (m_worker) {
            return m_worker->results();
        }

        return nullptr;
    }
    void requestInterruption();

protected:
    void run() override final;

private:
    std::unique_ptr<QmlAVAbstractWorker> m_worker;
    std::atomic<bool> m_loopInterruptionRequested;
};

// NOTE: For use from a single thread only!
template<typename Result>
class QmlAVThreadLiveController
{
public:
    QmlAVThreadLiveController() : m_thread(nullptr) { }
    virtual ~QmlAVThreadLiveController() {
        logDebug() << "~QmlAVThreadLiveController()";

        // In multithreaded environment, the value returned by use_count() is approximate
        // (typical implementations use a std::memory_order_relaxed load).
        if (m_thread.use_count() == 1) {
            if (m_thread->isRunning()) {
                logWarning() << "Attempting to destroy an object of a running thread!";
            }

            requestInterruption(true);
        }
    }

    void requestInterruption(bool wait = false) {
        logDebug() << QString("requestInterruption(wait=%1)").arg(wait);

        if (m_thread) {
            m_thread->requestInterruption();

            if (wait) {
                m_thread->wait();
            }
        }
    }
    bool isRunning() const {
        if (m_thread) {
            return m_thread->isRunning();
        }

        return false;
    }
    void waitForFinished() {
        if (m_thread) {
            m_thread->wait();
        }
    }

    template<typename T = Result, typename = QmlAVUtils::EnableForNonVoid<T>>
    Result result() const {
        Result data = {};

        if (m_thread) {
            auto queue = static_cast<QmlAVQueue<Result> *>(m_thread->results());
            if (queue) {
                queue->dequeue(data);
            }
        }

        return data;
    }

    template<typename T = Result, typename = QmlAVUtils::EnableForNonVoid<T>>
    auto results() const {
        std::list<Result> list;

        if (m_thread) {
            auto queue = static_cast<QmlAVQueue<Result> *>(m_thread->results());
            if (queue) {
                Result data = {};

                do {
                    if (queue->dequeue(data)) {
                        list.push_back(std::move(data));
                    }
                } while (!queue->isEmpty());
            }
        }

        return list;
    }

protected:
    QmlAVThreadLiveController(std::shared_ptr<QmlAVWorkerThread> thread) : m_thread(thread) {
        logDebug() << QString("QmlAVThreadLiveController(thread=0x%1)").arg(QString().number(reinterpret_cast<uintptr_t>(thread.get()), 16));

        if (m_thread) {
            m_thread->start();
        }
    }

private:
    std::shared_ptr<QmlAVWorkerThread> m_thread;

    friend class QmlAVThread;
    template<typename> friend class QmlAVThreadTask;
};

template<typename Callable>
class QmlAVThreadTask
{
    using ArgsQueue = QmlAVQueue<QmlAVUtils::InvokeArgsTuple<Callable>>;

public:
    QmlAVThreadTask(Callable &&callable)
        : m_callable(std::forward<Callable>(callable))
        , m_argsQueue(std::make_shared<ArgsQueue>()) { }

    int length() const { return m_argsQueue->size(); }

    template<typename ...URef>
    void operator () (URef &&...args) {
        m_argsQueue->enqueue(std::forward_as_tuple(args...));
    }

    auto getLiveController() {
        auto worker = std::make_unique<QmlAVWorker<Callable, std::shared_ptr<ArgsQueue>>>(m_callable, m_argsQueue);
        auto thread = std::make_shared<QmlAVWorkerThread>(std::move(worker));

        using Result = QmlAVUtils::InvokeResult<Callable>;
        return QmlAVThreadLiveController<Result>(thread);
    }

private:
    Callable m_callable;
    std::shared_ptr<ArgsQueue> m_argsQueue;
};

class QmlAVThread
{
    static_assert(__cplusplus >= 201703L, "This QmlAVThread class expects a C++17 compatible compiler.");

public:
    template<typename Callable, typename ...Args>
    static auto run(Callable &&callable, Args &&...args) {
        auto worker = std::make_unique<QmlAVWorker<Callable, Args...>>(std::forward<Callable>(callable), std::forward<Args>(args)...);
        auto thread = std::make_shared<QmlAVWorkerThread>(std::move(worker));

        using Result = QmlAVUtils::InvokeResult<Callable>;
        return QmlAVThreadLiveController<Result>(thread);
    }
    template<typename Callable>
    static auto loop(Callable &&callable) {
        return run(std::forward<Callable>(callable));
    }
};

#endif // QMLAVTHREAD_H

#ifndef QMLAVTHREAD_H
#define QMLAVTHREAD_H

#include <thread>

#include "qmlavwaitingqueue.h"
#include "qmlavutils.h"

class QmlAVLoopController
{
public:
    enum Operator {
        Break,
        Continue,
        Retry // Same as Continue, but without dequeue() for the Args Queue
    };

    QmlAVLoopController(int64_t sleep = 0) : QmlAVLoopController(Continue, sleep) { }
    QmlAVLoopController(Operator ctrl, int64_t sleep = 0)
    {
        m_ctrl = ctrl;
        m_sleep = std::max<int64_t>(0, sleep);
    }

    bool isBreak() const { return m_ctrl == Break; }
    bool isContinue() const { return m_ctrl == Continue; }
    bool isRetry() const { return m_ctrl == Retry; }
    void sleep() const {
        if (m_sleep > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(m_sleep));
        }
    }

private:
    Operator m_ctrl;
    int64_t m_sleep;
};

class QmlAVAbstractWorker
{
public:
    QmlAVAbstractWorker() { }
    virtual ~QmlAVAbstractWorker() { }

    virtual QmlAVLoopController invoke() = 0;
    virtual void *results() { return nullptr; }
    virtual void requestInterrupt() { }
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

    virtual void requestInterrupt() override {
        m_results.setConsumerLimit(0);
    }

protected:
    template <typename URef>
    void setResult(URef &&result) {
        m_results.enqueue(std::forward<URef>(result));
    }

private:
    QmlAVWaitingQueue<Result> m_results;
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

    constexpr static bool isLoopCtrlResult() { return std::is_same_v<Result, QmlAVLoopController>; }
    constexpr static bool isVoidResult() { return std::is_void_v<Result>; }

public:
    QmlAVWorkerInvokeImpl(Callable &&callable)
        : m_callable(std::forward<Callable>(callable)) { }

protected:
    template <typename URef>
    QmlAVLoopController invokeImpl(URef &&args) {
        auto data = std::tuple_cat(std::forward_as_tuple(m_callable), std::forward<URef>(args));

        if constexpr (isLoopCtrlResult()) {
            return std::apply(&QmlAVUtils::FunctionTraits<Callable>::template invoke<Callable>, data);
        } else if constexpr (isVoidResult()) {
            std::apply(&QmlAVUtils::FunctionTraits<Callable>::template invoke<Callable>, data);
        } else {
            auto result = std::apply(&QmlAVUtils::FunctionTraits<Callable>::template invoke<Callable>, data);
            this->setResult(std::move(result));
        }

        return QmlAVLoopController::Break;
    }

private:
    Callable m_callable;
};

template<typename Callable, typename ...Args>
class QmlAVWorker : public QmlAVWorkerInvokeImpl<Callable>
{
    using __Super = QmlAVWorkerInvokeImpl<Callable>;

public:
    // NOTE: Here and below, we don't rely on "Callable" deduction, so we don't use universal/forwarding references.
    QmlAVWorker(Callable callable, Args ...args)
        : __Super(std::forward<Callable>(callable))
        , m_args(std::forward<Args>(args)...) { }

    virtual QmlAVLoopController invoke() override final {
        return this->invokeImpl(m_args);
    }

private:
    typename __Super::ArgsTuple m_args;
};

template<typename Callable, typename ArgsQueue>
class QmlAVWorker<Callable, std::shared_ptr<ArgsQueue>> : public QmlAVWorkerInvokeImpl<Callable>
{
    using __Super = QmlAVWorkerInvokeImpl<Callable>;

    static_assert(std::is_same_v<ArgsQueue, QmlAVWaitingQueue<typename __Super::ArgsTuple>>,
                  "This implementation is only for \"std::shared_ptr<QmlAVWaitingQueue<InvokeArgsTuple<Callable>>>\" type.");

public:
    QmlAVWorker(Callable callable, const std::shared_ptr<ArgsQueue> &argsQueue)
        : __Super(std::forward<Callable>(callable))
        , m_argsQueue(argsQueue) { }

    virtual QmlAVLoopController invoke() override final {
        typename __Super::ArgsTuple args;

        if constexpr (__Super::isLoopCtrlResult()) {
            if (m_argsQueue->head(args)) {
                auto ctrl =  this->invokeImpl(args);
                if (!ctrl.isRetry()) {
                    m_argsQueue->dequeue();
                }
                return ctrl;
            }
        } else {
            if (m_argsQueue->dequeue(args)) {
                if constexpr (__Super::isVoidResult()) {
                    return this->invokeImpl(args);
                } else {
                    this->invokeImpl(args);
                }
            }
        }

        return QmlAVLoopController::Continue;
    }

    virtual void requestInterrupt() override final {
        if (m_argsQueue) {
            m_argsQueue->setConsumerLimit(0);
        }

        __Super::requestInterrupt();
    }

private:
    std::shared_ptr<ArgsQueue> m_argsQueue;
};

class QmlAVWorkerThread
{
public:
    QmlAVWorkerThread(std::unique_ptr<QmlAVAbstractWorker> worker)
        : m_running(false)
        , m_loopInterruptRequested(false)
        , m_worker(std::move(worker)) { }

    void start() {
        if (setRunning(true)) {
            std::thread(&QmlAVWorkerThread::run, this).detach();
        }
    }
    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_waitCond.wait(lock, [&] {
            // Executes in lock context
            return !m_running;
        });
    }
    bool isRunning() const {
        std::scoped_lock lock(m_mutex);
        return m_running;
    }
    void requestInterrupt();

    void *results() {
        if (m_worker) {
            return m_worker->results();
        }

        return nullptr;
    }

protected:
    void run();
    bool setRunning(bool running) {
        std::scoped_lock lock(m_mutex);

        if (m_running == running) {
            return false;
        }
        m_running = running;
        return true;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_waitCond;

    bool m_running;
    std::atomic<bool> m_loopInterruptRequested;

    std::unique_ptr<QmlAVAbstractWorker> m_worker;
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

            requestInterrupt(true);
        }
    }

    void requestInterrupt(bool wait = false) {
        logDebug() << QString("requestInterrupt(wait=%1)").arg(wait);

        if (m_thread) {
            m_thread->requestInterrupt();

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
            auto queue = static_cast<QmlAVWaitingQueue<Result> *>(m_thread->results());
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
            auto queue = static_cast<QmlAVWaitingQueue<Result> *>(m_thread->results());
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
        logDebug() << QString("QmlAVThreadLiveController(thread=0x%1)").arg(QString().number(reinterpret_cast<uintptr_t>(m_thread.get()), 16));

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
    using ArgsQueue = QmlAVWaitingQueue<QmlAVUtils::InvokeArgsTuple<Callable>>;

public:
    QmlAVThreadTask(Callable &&callable)
        : m_callable(std::forward<Callable>(callable))
        , m_argsQueue(std::make_shared<ArgsQueue>()) { }

    auto argsQueue() const { return m_argsQueue; }

    template<typename ...URef>
    void operator() (URef &&...args) {
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

#ifndef QMLAVWAITINGQUEUE_H
#define QMLAVWAITINGQUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class QmlAVWaitingQueue
{
public:
    QmlAVWaitingQueue()
        : m_producerLimit(0) // Unlim
        , m_consumerLimit(1) { }
    virtual ~QmlAVWaitingQueue()
    {
        setProducerLimit(0);
        setConsumerLimit(0);
    }

    QmlAVWaitingQueue(const QmlAVWaitingQueue &other) = delete;
    QmlAVWaitingQueue(QmlAVWaitingQueue &&other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_queue = std::move(other.m_queue);
    }

    QmlAVWaitingQueue &operator=(const QmlAVWaitingQueue &other) = delete;
    QmlAVWaitingQueue &operator=(QmlAVWaitingQueue &&other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        if (this != std::addressof(other)) {
            m_queue = std::move(other.m_queue);
        }
        return *this;
    }

    template <typename URef>
    void enqueue(URef &&value) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_producerCond.wait(lock, [&] {
                // Executes in lock context
                return m_producerLimit == 0 || m_queue.size() < m_producerLimit;
            });

            m_queue.push(std::forward<T>(value));
        }
        m_consumerCond.notify_one();
    }
    bool head(T &value) {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_consumerCond.wait(lock, [&] {
            // Executes in lock context
            return m_queue.size() >= m_consumerLimit;
        });

        if (m_queue.empty()) {
            return false;
        }

        value = m_queue.front();

        return true;
    }
    bool dequeue(T &value) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_consumerCond.wait(lock, [&] {
                // Executes in lock context
                return m_queue.size() >= m_consumerLimit;
            });

            if (m_queue.empty()) {
                return false;
            }

            value = std::move(m_queue.front());
            m_queue.pop();
        }
        m_producerCond.notify_all();

        return true;
    }
    void dequeue() {
        {
            std::scoped_lock lock(m_mutex);
            if (!m_queue.empty()) {
                m_queue.pop();
            }
        }
        m_producerCond.notify_all();
    }

    void waitForEmpty() {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_producerCond.wait(lock, [&] {
            // Executes in lock context
            return m_queue.empty();
        });
    }

    void setProducerLimit(size_t limit) {
        {
            std::scoped_lock lock(m_mutex);
            m_producerLimit = limit;
        }
        m_producerCond.notify_all();
    }
    void setConsumerLimit(size_t limit) {
        {
            std::scoped_lock lock(m_mutex);
            m_consumerLimit = limit;
        }
        m_consumerCond.notify_all();
    }

    // NOTE: Be careful! Potential API race.
    bool isEmpty() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.empty();
    }
    int length() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_producerCond;
    std::condition_variable m_consumerCond;

    std::queue<T> m_queue;
    size_t m_producerLimit, m_consumerLimit;
};

#endif // QMLAVWAITINGQUEUE_H

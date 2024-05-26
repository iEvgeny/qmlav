#ifndef QMLAVQUEUE_H
#define QMLAVQUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class QmlAVQueue
{
public:
    QmlAVQueue()
        : m_minLimit(1)
        , m_maxLimit(0) { }
    virtual ~QmlAVQueue() { resetWaitLimits(); }

    QmlAVQueue(const QmlAVQueue &other) = delete;
    QmlAVQueue(QmlAVQueue &&other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_queue = std::move(other.m_queue);
    }

    QmlAVQueue &operator=(const QmlAVQueue &other) = delete;
    QmlAVQueue &operator=(QmlAVQueue &&other) {
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
                return m_maxLimit == 0 || m_queue.size() < m_maxLimit;
            });

            m_queue.push(std::forward<T>(value));
        }
        m_consumerCond.notify_one();
    }
    bool head(T &value) {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_consumerCond.wait(lock, [&] {
            // Executes in lock context
            return m_queue.size() >= m_minLimit;
        });

        if (!m_queue.empty()) {
            value = m_queue.front();
            return true;
        }

        return false;
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

    void resetWaitLimits(size_t min = 0, size_t max = 0) {
        {
            std::scoped_lock lock(m_mutex);
            m_minLimit = min;
            m_maxLimit = max;
        }
        m_producerCond.notify_all();
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
    size_t m_minLimit, m_maxLimit;
};

#endif // QMLAVQUEUE_H

#ifndef QMLAVQUEUE_H
#define QMLAVQUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class QmlAVQueue
{
public:
    QmlAVQueue() : m_waitValue(true) { }
    virtual ~QmlAVQueue() {
        setWaitValue(false);
    }

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
            std::scoped_lock lock(m_mutex);
            m_queue.push(std::forward<T>(value));
        }
        m_conditionVar.notify_one();
    }
    bool dequeue(T &value) {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_conditionVar.wait(lock, [&] {
            // Executes in lock context
            return !m_queue.empty() || !m_waitValue;
        });

        if (!m_queue.empty()) {
            value = m_queue.front();
            m_queue.pop();
            return true;
        }

        return false;
    }

    void setWaitValue(bool wait) {
        {
            std::scoped_lock lock(m_mutex);
            m_waitValue = wait;
        }
        m_conditionVar.notify_all();
    }

    // NOTE: Be careful! Potential API race.
    bool isEmpty() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.empty();
    }
    int size() const {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVar;
    std::queue<T> m_queue;
    bool m_waitValue;
};

#endif // QMLAVQUEUE_H

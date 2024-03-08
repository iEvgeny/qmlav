#include "qmlavaudioqueue.h"

QmlAVAudioQueue::QmlAVAudioQueue(QObject *parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadOnly);
}

QmlAVAudioQueue::~QmlAVAudioQueue()
{
    close();
}

qint64 QmlAVAudioQueue::bytesAvailable() const
{
    return m_buffer.size() + QIODevice::bytesAvailable();
}

#define QUEUE_LIMIT 0xFFFFF  // 1 MB
void QmlAVAudioQueue::push(const std::shared_ptr<QmlAVAudioFrame> frame)
{
    if (m_buffer.size() < QUEUE_LIMIT) {
        m_buffer.append(frame->data(), frame->dataSize());
    } else {
        logDebug() << "Audio buffer queue exceeded 1 MB (Current size: " << m_buffer.size() << ")";
    }
}

qint64 QmlAVAudioQueue::readData(char *data, qint64 maxSize)
{
    qint64 size = std::min(static_cast<qint64>(m_buffer.size()), maxSize);
    memcpy(data, m_buffer.constData(), size);
    m_buffer.remove(0, size);
    return size;
}

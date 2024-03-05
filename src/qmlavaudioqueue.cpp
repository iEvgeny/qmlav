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

bool QmlAVAudioQueue::isSequential() const
{
    return true;
}

#define QUEUE_LIMIT 0x7FFFFF  // 8 MB
void QmlAVAudioQueue::push(const std::shared_ptr<QmlAVAudioFrame> frame)
{
    // FIXME: Temporary solution.
    // The reason for the inconsistency between producer and consumer data volumes for some formats is unclear.
    if (m_buffer.size() < QUEUE_LIMIT) {
        m_buffer.append(frame->data(), frame->dataSize());
    } else {
        logDebug() << "FIXME: Audio buffer queue exceeded 8 MB (Current size: " << m_buffer.size() << ")";
    }
}

qint64 QmlAVAudioQueue::readData(char *data, qint64 maxSize)
{
    qint64 size = qMin(static_cast<qint64>(m_buffer.size()), maxSize);
    memcpy(data, m_buffer.constData(), size);
    m_buffer.remove(0, size);
    return size;
}

qint64 QmlAVAudioQueue::writeData(const char *data, qint64 maxSize)
{
    Q_UNUSED(data);
    Q_UNUSED(maxSize);

    return 0;
}

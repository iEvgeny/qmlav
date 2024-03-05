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

void QmlAVAudioQueue::push(const std::shared_ptr<QmlAVAudioFrame> frame)
{
    m_buffer.append(frame->data(), frame->dataSize());
}

qint64 QmlAVAudioQueue::readData(char *data, qint64 maxSize)
{
    // This function might be called with a maxSize of 0, which can be used to perform post-reading operations.
    if (maxSize == 0 && m_buffer.size() > 0xffffff) {
        logWarning() << "Audio buffer queue exceeded 16 MB (Current size: " << m_buffer.size() << ")";
    }

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

#include "qmlavaudioiodevice.h"

QmlAVAudioIODevice::QmlAVAudioIODevice(QObject *parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadOnly);
}

QmlAVAudioIODevice::~QmlAVAudioIODevice()
{
    close();
}

void QmlAVAudioIODevice::enqueue(const std::shared_ptr<QmlAVAudioFrame> frame)
{
    m_frames.push_back(frame);
}

qint64 QmlAVAudioIODevice::readData(char *data, qint64 maxSize)
{
    size_t size = 0;

    if (!m_frames.empty()) {
        auto &f = m_frames.front();
        size = f->readData(reinterpret_cast<uint8_t *>(data), maxSize);
        if (f->dataSize() == 0) {
            m_frames.pop_front();
        }
    }

    return size;
}

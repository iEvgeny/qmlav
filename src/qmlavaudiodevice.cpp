#include "qmlavaudiodevice.h"

QmlAVAudioDevice::QmlAVAudioDevice(QObject *parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadOnly);
}

QmlAVAudioDevice::~QmlAVAudioDevice()
{
    close();
}

void QmlAVAudioDevice::enqueue(const std::shared_ptr<QmlAVAudioFrame> frame)
{
    m_frames.push_back(frame);
}

qint64 QmlAVAudioDevice::readData(char *data, qint64 maxSize)
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

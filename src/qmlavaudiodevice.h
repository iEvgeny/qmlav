#ifndef QMLAVAUDIODEVICE_H
#define QMLAVAUDIODEVICE_H

#include <QtCore>

#include "qmlavframe.h"

#define PA_PREBUF_SIZE 32768

class QmlAVAudioDevice final : public QIODevice
{
    Q_OBJECT

public:
    QmlAVAudioDevice(QObject *parent = nullptr);
    ~QmlAVAudioDevice() override;

    // In this combination, these two functions set the size of the PulseAudio buffer.
    // PulseAudio prebuffering determines the tradeoff between playback latency and audio quality.
    qint64 bytesAvailable() const override { return PA_PREBUF_SIZE; }
    bool isSequential() const override { return true; }

    void enqueue(const std::shared_ptr<QmlAVAudioFrame> frame);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData([[maybe_unused]] const char *data, [[maybe_unused]] qint64 maxSize) override { return 0; }

private:
    std::deque<std::shared_ptr<QmlAVAudioFrame>> m_frames;
};

#endif // QMLAVAUDIODEVICE_H

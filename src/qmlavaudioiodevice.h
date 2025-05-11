#ifndef QMLAVAUDIOIODEVICE_H
#define QMLAVAUDIOIODEVICE_H

#include <deque>
#include <memory>

#include <QIODevice>

#define PA_PREBUF_SIZE 32768

class QmlAVAudioFrame;

class QmlAVAudioIODevice final : public QIODevice
{
    Q_OBJECT

public:
    QmlAVAudioIODevice(QObject *parent = nullptr);
    ~QmlAVAudioIODevice() override;

    // In this combination, these two functions set the size of the PulseAudio buffer.
    // PulseAudio prebuffering determines the tradeoff between playback latency and audio quality.
    qint64 bytesAvailable() const override { return PA_PREBUF_SIZE; }
    bool isSequential() const override { return true; }

    void enqueue(const std::shared_ptr<QmlAVAudioFrame> frame);
    void clear();

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData([[maybe_unused]] const char *data, [[maybe_unused]] qint64 maxSize) override { return 0; }

private:
    std::deque<std::shared_ptr<QmlAVAudioFrame>> m_frames;
};

#endif // QMLAVAUDIOIODEVICE_H

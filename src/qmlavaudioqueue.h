#ifndef QMLAVAUDIOQUEUE_H
#define QMLAVAUDIOQUEUE_H

#include <QtCore>

#include "qmlavframe.h"

class QmlAVAudioQueue final : public QIODevice
{
    Q_OBJECT

public:
    QmlAVAudioQueue(QObject *parent = nullptr);
    ~QmlAVAudioQueue() override;

    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

    void push(const std::shared_ptr<QmlAVAudioFrame> frame);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData([[maybe_unused]] const char *data, [[maybe_unused]] qint64 maxSize) override { return 0; }

private:
    QByteArray m_buffer;
};

#endif // QMLAVAUDIOQUEUE_H

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
    bool isSequential() const override;

    void push(const std::shared_ptr<QmlAVAudioFrame> frame);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    QByteArray m_buffer;
};

#endif // QMLAVAUDIOQUEUE_H

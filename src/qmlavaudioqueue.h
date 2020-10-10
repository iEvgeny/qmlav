#ifndef QMLAVAUDIOQUEUE_H
#define QMLAVAUDIOQUEUE_H

#include <QtCore>

#include "qmlavframe.h"

class QmlAVAudioQueue : public QIODevice
{
    Q_OBJECT

public:
    QmlAVAudioQueue(QObject *parent = nullptr);
    virtual ~QmlAVAudioQueue();

    virtual qint64 bytesAvailable() const override;
    virtual bool isSequential() const override;

    void push(const std::shared_ptr<QmlAVAudioFrame> frame);

protected:
    virtual qint64 readData(char *data, qint64 maxSize) override;
    virtual qint64 writeData(const char *data, qint64 maxSize) override;

private:
    QByteArray m_buffer;
};

#endif // QMLAVAUDIOQUEUE_H

#ifndef QMLAVFRAME_H
#define QMLAVFRAME_H

#include <QtCore>
#include <QVideoFrame>
#include <QAudioFormat>

#include "qmlavutils.h"
#include "qmlavformat.h"

class QmlAVFrame
{
public:
    enum Type
    {
        TypeUnknown,
        TypeVideo,
        TypeAudio
    };

    QmlAVFrame(const AVFramePtr &avFramePtr, Type type = TypeUnknown);
    QmlAVFrame(const QmlAVFrame &other);
    virtual ~QmlAVFrame();

    QmlAVFrame &operator=(const QmlAVFrame &other);

    Type type() const { return m_type; }

    virtual bool isValid() const = 0;
    AVFramePtr &avFrame() { return m_avFrame; }
    const AVFramePtr &avFrame() const { return m_avFrame; }
    int64_t pts() const;

protected:
    template<typename T>
    T *decoder() const { return static_cast<T *>(m_avFrame->opaque); }

private:
    AVFramePtr m_avFrame;
    Type m_type;
};

class QmlAVVideoFrame final : public QmlAVFrame
{
public:
    QmlAVVideoFrame(const AVFramePtr &avFramePtr);

    bool isValid() const override;

    QSize size() const { return {avFrame()->width, avFrame()->height}; }
    QSize sampleAspectRatio() const;
    bool isHWDecoded() const { return avFrame()->hw_frames_ctx; }
    QmlAVPixelFormat pixelFormat() const { return avFrame()->format; }
    QmlAVPixelFormat swPixelFormat() const;
    QmlAVColorSpace colorSpace() const;

    operator QVideoFrame() const;
};

class QmlAVAudioFrame final : public QmlAVFrame
{
public:
    QmlAVAudioFrame(const AVFramePtr &avFramePtr);
    ~QmlAVAudioFrame() override;

    bool isValid() const override;

    QAudioFormat audioFormat() const { return m_audioFormat; }
    void setAudioFormat(const QAudioFormat &format) { m_audioFormat = format; }
    char *data() const { return reinterpret_cast<char *>(m_data); }
    int dataSize() const { return m_dataSize; }

private:
    uint8_t *m_data;
    int m_dataSize;
    QAudioFormat m_audioFormat;
};

#endif // QMLAVFRAME_H

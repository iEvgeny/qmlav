#ifndef QMLAVFRAME_H
#define QMLAVFRAME_H

#include <QtCore>
#include <QVideoFrame>
#include <QAudioFormat>

#include "qmlavutils.h"
#include "qmlavformat.h"

class QmlAVDecoder;

class QmlAVFrame
{
public:
    enum Type
    {
        TypeUnknown,
        TypeVideo,
        TypeAudio
    };

    QmlAVFrame(const AVFramePtr &avFrame, Type type = TypeUnknown);
    QmlAVFrame(const QmlAVFrame &other);
    virtual ~QmlAVFrame();

    Type type() const { return m_type; }

    virtual bool isValid() const = 0;
    AVFramePtr &avFrame() { return m_avFrame; }
    const AVFramePtr &avFrame() const { return m_avFrame; }
    int64_t startTime() const;
    double timeBaseUs() const;
    int64_t startPts() const;
    int64_t pts() const;

protected:
    const auto &decoder() const { return m_decoder; }
    // NOTE: Creates a new instance of std::shared_ptr
    template<typename T> std::shared_ptr<T> decoder() const { return std::static_pointer_cast<T>(m_decoder); }

private:
    AVFramePtr m_avFrame;
    Type m_type;
    std::shared_ptr<QmlAVDecoder> m_decoder;
};

class QmlAVVideoFrame final : public QmlAVFrame
{
public:
    QmlAVVideoFrame(const AVFramePtr &avFrame);

    bool isValid() const override;

    QSize size() const { return {avFrame()->width, avFrame()->height}; }
    AVRational sampleAspectRatio() const;
    bool isHWDecoded() const { return avFrame()->hw_frames_ctx; }
    QmlAVPixelFormat pixelFormat() const { return avFrame()->format; }
    QmlAVPixelFormat swPixelFormat() const;
    QmlAVColorSpace colorSpace() const;

    operator QVideoFrame() const;
};

class QmlAVAudioFrame final : public QmlAVFrame
{
public:
    QmlAVAudioFrame(const AVFramePtr &avFrame);
    ~QmlAVAudioFrame() override;

    bool isValid() const override;

    size_t dataSize() const { return m_dataSize - m_dataBegin; }
    size_t readData(uint8_t *data, size_t maxSize);

    AVSampleFormat sampleFormat() const;
    int sampleRate() const { return avFrame()->sample_rate; }
    int channelCount() const;

    QAudioFormat audioFormat() const;

private:
    uint8_t *m_buffer;
    size_t m_dataBegin;
    size_t m_dataSize;
};

#endif // QMLAVFRAME_H

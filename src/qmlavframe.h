#ifndef QMLAVFRAME_H
#define QMLAVFRAME_H

#include <memory>

#include <QVideoFrame>
#include <QAudioFormat>

#include "qmlavmediacontextholder.h"
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

    QmlAVFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVMediaContextHolder> &context, Type type = TypeUnknown);
    virtual ~QmlAVFrame();

    Type type() const { return m_type; }

    virtual bool isValid() const = 0;
    AVFramePtr &avFrame() { return m_avFrame; }
    const AVFramePtr &avFrame() const { return m_avFrame; }
    double timeBaseUs() const;
    int64_t startPts() const;
    int64_t pts() const;

protected:
    auto &context() { return m_context; }
    const auto &context() const { return m_context; }

    QmlAVDecoder *decoder() const {
        if (m_type == TypeVideo) {
            return m_context->videoDecoder;
        } else if (m_type == TypeAudio) {
            return m_context->audioDecoder;
        }
        return nullptr;
    }
    template<typename T> T *decoder() const { return static_cast<T *>(decoder()); }

private:
    Type m_type;
    AVFramePtr m_avFrame;
    std::shared_ptr<QmlAVMediaContextHolder> m_context;
};

class QmlAVVideoFrame final : public QmlAVFrame
{
public:
    QmlAVVideoFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVMediaContextHolder> &context);

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
    QmlAVAudioFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVMediaContextHolder> &context);
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

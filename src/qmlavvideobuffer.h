#ifndef QMLAVVIDEOBUFFER_H
#define QMLAVVIDEOBUFFER_H

#include <QAbstractPlanarVideoBuffer>

#include "qmlavframe.h"
#include "qmlavhwoutput.h"

struct SwsContext;

class QmlAVVideoBuffer : public QAbstractPlanarVideoBuffer
{
public:
    QmlAVVideoBuffer(const QmlAVVideoFrame &videoFrame, QAbstractVideoBuffer::HandleType type);

    struct MapData final
    {
        int size[QMLAV_NUM_DATA_POINTERS] = {};
        int bytesPerLine[QMLAV_NUM_DATA_POINTERS] = {};
        uchar *data[QMLAV_NUM_DATA_POINTERS] = {};
    };


    QAbstractVideoBuffer::MapMode mapMode() const override { return m_mapMode; }
    void unmap() override { m_mapMode = QAbstractVideoBuffer::NotMapped; }
    int map(QAbstractVideoBuffer::MapMode mapMode, int *numBytes, int bytesPerLine[], uchar *data[]) override final;

    virtual MapData map(QAbstractVideoBuffer::MapMode mapMode) = 0;

    virtual QmlAVPixelFormat pixelFormat() const = 0;

protected:
    void setMapMode(QAbstractVideoBuffer::MapMode mapMode) { m_mapMode = mapMode; }
    bool planesSize(int size[], const AVFramePtr &avFramePtr) const;
    AVFramePtr swsScale(const QmlAVPixelFormat &dstFormat);

protected:
    QmlAVVideoFrame m_videoFrame;

private:
    MapMode m_mapMode;
    SwsContext *m_swsCtx;
};

class QmlAVVideoBuffer_CPU : public QmlAVVideoBuffer
{
public:
    QmlAVVideoBuffer_CPU(const QmlAVVideoFrame &videoFrame);

    MapData map(QAbstractVideoBuffer::MapMode mapMode) override;

    QmlAVPixelFormat pixelFormat() const override;
};

class QmlAVVideoBuffer_GPU : public QmlAVVideoBuffer_CPU
{
public:
    QmlAVVideoBuffer_GPU(const QmlAVVideoFrame &videoFrame, std::shared_ptr<QmlAVHWOutput> hwOutput = nullptr);

    MapData map(QAbstractVideoBuffer::MapMode mapMode) override final;
    QVariant handle() const override final;

    QmlAVPixelFormat pixelFormat() const override;

private:
    std::shared_ptr<QmlAVHWOutput> m_hwOutput;
};

#endif // QMLAVVIDEOBUFFER_H

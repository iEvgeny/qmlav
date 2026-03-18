#ifndef QMLAVVIDEOBUFFER_H
#define QMLAVVIDEOBUFFER_H

#include "qmlavcompat.h"
#include "qmlavframe.h"
#include "qmlavhwoutput.h"

struct SwsContext;

class QmlAVVideoBuffer
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    : public QAbstractPlanarVideoBuffer
#endif
{
public:
    struct MapData final
    {
        int size[QMLAV_NUM_DATA_POINTERS] = {};
        int bytesPerLine[QMLAV_NUM_DATA_POINTERS] = {};
        uchar *data[QMLAV_NUM_DATA_POINTERS] = {};
    };

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    enum MapMode { NotMapped, ReadOnly, WriteOnly, ReadWrite };

    QmlAVVideoBuffer(const QmlAVVideoFrame &videoFrame);

    MapMode mapMode() const { return m_mapMode; }
    void unmap() { m_mapMode = NotMapped; }

    virtual MapData map(MapMode mapMode) = 0;
#else
    QmlAVVideoBuffer(const QmlAVVideoFrame &videoFrame, QAbstractVideoBuffer::HandleType type);

    QAbstractVideoBuffer::MapMode mapMode() const override { return m_mapMode; }
    void unmap() override { m_mapMode = QAbstractVideoBuffer::NotMapped; }
    int map(QAbstractVideoBuffer::MapMode mapMode, int *numBytes, int bytesPerLine[], uchar *data[]) override final;

    virtual MapData map(QAbstractVideoBuffer::MapMode mapMode) = 0;
#endif

    virtual ~QmlAVVideoBuffer() = default;
    virtual QmlAVPixelFormat pixelFormat() const = 0;

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void setMapMode(MapMode mapMode) { m_mapMode = mapMode; }
#else
    void setMapMode(QAbstractVideoBuffer::MapMode mapMode) { m_mapMode = mapMode; }
#endif
    bool planeSizes(int size[]) const;
    AVFramePtr swsScale(const QmlAVPixelFormat &dstFormat);

protected:
    QmlAVVideoFrame m_videoFrame;

private:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    MapMode m_mapMode;
#else
    MapMode m_mapMode;
#endif
    SwsContext *m_swsCtx;
};

class QmlAVVideoBuffer_CPU : public QmlAVVideoBuffer
{
public:
    QmlAVVideoBuffer_CPU(const QmlAVVideoFrame &videoFrame);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    MapData map(MapMode mapMode) override;
#else
    MapData map(QAbstractVideoBuffer::MapMode mapMode) override;
#endif

    QmlAVPixelFormat pixelFormat() const override;
};

class QmlAVVideoBuffer_GPU : public QmlAVVideoBuffer_CPU
{
public:
    QmlAVVideoBuffer_GPU(const QmlAVVideoFrame &videoFrame, std::shared_ptr<QmlAVHWOutput> hwOutput = nullptr);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    MapData map(MapMode mapMode) override final;
#else
    MapData map(QAbstractVideoBuffer::MapMode mapMode) override final;
    QVariant handle() const override final;
#endif

    QmlAVPixelFormat pixelFormat() const override;

private:
    std::shared_ptr<QmlAVHWOutput> m_hwOutput;
};

#endif // QMLAVVIDEOBUFFER_H

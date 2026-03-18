#ifndef QMLAVCOMPAT_H
#define QMLAVCOMPAT_H

#include <QtGlobal>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QAudioFormat>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

#include <QVideoSink>
#include <QVideoFrameFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>

using QmlAVVideoSurface = QVideoSink;
using QmlAVAudioOutput = QAudioSink;
using QmlAVPlaybackState = QMediaPlayer::PlaybackState;
using QmlAVPixelFormatEnum = QVideoFrameFormat::PixelFormat;
using QmlAVColorSpaceEnum = QVideoFrameFormat::ColorSpace;

#else

#include <QAbstractVideoSurface>
#include <QVideoSurfaceFormat>
#include <QAbstractPlanarVideoBuffer>
#include <QAudioOutput>
#include <QAudioDeviceInfo>

using QmlAVVideoSurface = QAbstractVideoSurface;
using QmlAVAudioOutput = QAudioOutput;
using QmlAVPlaybackState = QMediaPlayer::State;
using QmlAVPixelFormatEnum = QVideoFrame::PixelFormat;
using QmlAVColorSpaceEnum = QVideoSurfaceFormat::YCbCrColorSpace;

#endif

// Unified audio output creation
inline QmlAVAudioOutput *qmlavCreateAudioOutput(const QAudioFormat &format)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return new QAudioSink(QMediaDevices::defaultAudioOutput(), format);
#else
    return new QAudioOutput(QAudioDeviceInfo::defaultOutputDevice(), format);
#endif
}

#endif // QMLAVCOMPAT_H

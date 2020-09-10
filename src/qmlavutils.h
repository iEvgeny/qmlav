#ifndef QMLAVUTILS_H
#define QMLAVUTILS_H

#include <QtCore>

#undef av_err2str
#define av_err2str(errnum) \
    av_make_error_string(reinterpret_cast<char*>(alloca(AV_ERROR_MAX_STRING_SIZE)), AV_ERROR_MAX_STRING_SIZE, errnum)

class QmlAVDemuxer;

class QmlAVUtils
{
public:
    enum LogLevel {
        LogError = 0,
        LogInfo,
        LogVerbose,
        LogDebug
    };

    static int verboseLevel();
    static void log(const QString id, QmlAVUtils::LogLevel logLevel, const QString message);
    static void logError(const QString id, const QString message);
    static void logInfo(const QString id, const QString message);
    static void logVerbose(const QString id, const QString message);
    static void logDebug(const QString id, const QString message);
    static QString logId(const QmlAVDemuxer *p);
};

#endif // QMLAVUTILS_H

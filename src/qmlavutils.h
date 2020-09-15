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
    static void logError(const QString id, const QString message) { log(id, QmlAVUtils::LogError, message); }
    static void logInfo(const QString id, const QString message) { log(id, QmlAVUtils::LogInfo, message); }
    static void logVerbose(const QString id, const QString message) { log(id, QmlAVUtils::LogVerbose, message); }
    static void logDebug(const QString id, const QString message) { log(id, QmlAVUtils::LogDebug, message); }
    static QString logId(const QmlAVDemuxer *p);
};

#define logError(id, message) QmlAVUtils::log(id, QmlAVUtils::LogError, message)
#define logInfo(id, message) QmlAVUtils::log(id, QmlAVUtils::LogInfo, message)
#define logVerbose(id, message) QmlAVUtils::log(id, QmlAVUtils::LogVerbose, message)

#ifdef NO_DEBUG
    #define logDebug(id, message) (void)id; (void)message;
#else
    #define logDebug(id, message) QmlAVUtils::log(id, QmlAVUtils::LogDebug, message)
#endif

#endif // QMLAVUTILS_H

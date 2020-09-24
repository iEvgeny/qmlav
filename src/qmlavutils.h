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
    static void log(const QString prefix, QmlAVUtils::LogLevel logLevel, const QString message);
    static void logError(QObject *sender, const QString message) { log(logPrefix(sender), QmlAVUtils::LogError, message); }
    static void logInfo(QObject *sender, const QString message) { log(logPrefix(sender), QmlAVUtils::LogInfo, message); }
    static void logVerbose(QObject *sender, const QString message) { log(logPrefix(sender), QmlAVUtils::LogVerbose, message); }
    static void logDebug(QObject *sender, const QString message) { log(logPrefix(sender), QmlAVUtils::LogDebug, message); }
    static QString logPrefix(QObject *sender);
};

#define logError(sender, message) QmlAVUtils::logError(qobject_cast<QObject *>(sender), message)
#define logInfo(sender, message) QmlAVUtils::logInfo(qobject_cast<QObject *>(sender), message)
#define logVerbose(sender, message) QmlAVUtils::logVerbose(qobject_cast<QObject *>(sender), message)

#ifdef NO_DEBUG
    #define logDebug(sender, message) (void)sender; (void)message;
#else
    #define logDebug(sender, message) QmlAVUtils::logDebug(qobject_cast<QObject *>(sender), message)
#endif

#endif // QMLAVUTILS_H

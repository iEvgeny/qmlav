#include "qmlavutils.h"
#include "qmlavdemuxer.h"

int QmlAVUtils::verboseLevel()
{
    return QProcessEnvironment::systemEnvironment().value("VERBOSE_LEVEL", 0).toUInt();
}

void QmlAVUtils::log(const QString id, QmlAVUtils::LogLevel logLevel, const QString message)
{
    FILE *fileHandle = stdout;

    if ((verboseLevel() - logLevel) >= 0) {
        if (logLevel == QmlAVUtils::LogError) {
            fileHandle = stderr;
        }

        QTextStream(fileHandle) << QString("[%1] ").arg(id) << message << '\n';
    }
}

void QmlAVUtils::logError(const QString id, const QString message)
{
    log(id, QmlAVUtils::LogError, message);
}

void QmlAVUtils::logInfo(const QString id, const QString message)
{
    log(id, QmlAVUtils::LogInfo, message);
}

void QmlAVUtils::logVerbose(const QString id, const QString message)
{
    log(id, QmlAVUtils::LogVerbose, message);
}

void QmlAVUtils::logDebug(const QString id, const QString message)
{
    log(id, QmlAVUtils::LogDebug, message);
}

QString QmlAVUtils::logId(const QmlAVDemuxer *p)
{
    QString logId = QString().number(reinterpret_cast<long>(p), 16);

    if (p && p->m_formatCtx) {
        logId += QString(", %1").arg(p->m_formatCtx->url);
    }

    return logId;
}

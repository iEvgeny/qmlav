#include "qmlavutils.h"
#include "qmlavdemuxer.h"

int QmlAVUtils::verboseLevel()
{
    return QProcessEnvironment::systemEnvironment().value("VERBOSE_LEVEL", "0").toUInt();
}

void QmlAVUtils::log(const QString id, QmlAVUtils::LogLevel logLevel, const QString message)
{
    if ((verboseLevel() - logLevel) >= 0) {
        if (logLevel == QmlAVUtils::LogError) {
            qDebug() << QString("[%1] ").arg(id) << message;
        } else {
            qInfo() << QString("[%1] ").arg(id) << message;
        }
    }
}

QString QmlAVUtils::logId(const QmlAVDemuxer *p)
{
    QString logId = QString().number(reinterpret_cast<long>(p), 16);

    if (p && p->m_formatCtx) {
#ifdef FF_API_NEXT
        logId += QString(", %1").arg(p->m_formatCtx->url);
#else
        logId += QString(", %1").arg(p->m_formatCtx->filename);
#endif
    }

    return logId;
}

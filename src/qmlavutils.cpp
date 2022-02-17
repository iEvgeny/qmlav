#include "qmlavutils.h"
#include "qmlavdemuxer.h"

int QmlAVUtils::verboseLevel()
{
    return QProcessEnvironment::systemEnvironment().value("VERBOSE_LEVEL", "0").toUInt();
}

void QmlAVUtils::log(const QString prefix, QmlAVUtils::LogLevel logLevel, const QString message)
{
    if ((verboseLevel() - logLevel) >= 0) {
        qDebug("[%s] %s", prefix.toUtf8().data(), message.toUtf8().data());
    }
}

QString QmlAVUtils::logPrefix(QObject *sender)
{
    QString className;

    if (sender) {
        className = sender->metaObject()->className();
    }

    return QString("%1 @ %2").arg(className, QString().number(reinterpret_cast<quintptr>(sender), 16));
}

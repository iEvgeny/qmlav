#include "qmlavoptions.h"
#include "qmlavhwoutput.h"

#include <QGuiApplication>

QmlAVOptions::QmlAVOptions(const QVariantMap &avOptions)
{
    QVariantMap::const_iterator i = avOptions.constBegin();
    while (i != avOptions.constEnd()) {
        m_avOptions.push_back({i.key().toStdString(), i.value().toString().toStdString()});
        ++i;
    }
}

QmlAVOptions::operator AVDictionaryPtr() const
{
    AVDictionaryPtr dict;

    for (const auto& [key, value] : m_avOptions) {
        dict.set(key, value);
        logDebug() << "Added AVFormat option: -" << key << QmlAV::Space << value;
    }

    return dict;
}

LIBAVFORMAT_CONST AVInputFormat *QmlAVOptions::avInputFormat() const
{
    LIBAVFORMAT_CONST AVInputFormat *avInputFormat = nullptr;

    find("f", [&](std::string value) {
        avInputFormat = av_find_input_format(value.c_str());
        if (!avInputFormat) {
            logWarning() << "Unknown input format: -f " << value << ". Ignore this.";
        }
    });

    return avInputFormat;
}

AVHWDeviceType QmlAVOptions::avHWDeviceType() const
{
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

    // TODO: Implement support for "auto" value
    find("hwaccel", [&](std::string value) {
        type = av_hwdevice_find_type_by_name(value.c_str());
        if (type == AV_HWDEVICE_TYPE_NONE) {
            logWarning() << "Device type \"" << value << "\" is not supported!";
        }
    });

    return type;
}

std::shared_ptr<QmlAVHWOutput> QmlAVOptions::hwOutput() const
{
    std::shared_ptr<QmlAVHWOutput> hwOutput;

    find("hwaccel_output", [&](std::string value) {
        if (value == "glx") {
            if (avHWDeviceType() != AV_HWDEVICE_TYPE_VAAPI ||
                QGuiApplication::platformName() != "xcb") {
                logWarning() << "The \"" << value << "\" output module does not match the selected hardware decoder or the \"xcb\" platform underlying Qt!";
                return;
            }

            hwOutput = std::make_shared<QmlAVHWOutput_VAAPI_GLX>();
            return;
        }

        logWarning() << "Output module \"" << value << "\" is not supported!";
    });

    return hwOutput;
}

const AVCodec *QmlAVOptions::avCodec(const AVStream *avStream) const
{
    std::vector<std::string> opts;

    if (!avStream) {
        return nullptr;
    }

    switch (avStream->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        opts = {"vcodec", "codec:v", "c:v"};
        break;
    case AVMEDIA_TYPE_AUDIO:
        opts = {"acodec", "codec:a", "c:a"};
        break;
    default:
        return nullptr;
    }

    AVCodec *codec = nullptr;
    bool forced = find(opts, [&](std::string value) {
        codec = avcodec_find_decoder_by_name(value.c_str());
        if (!codec) {
            logWarning() << "Could not find codec with name: " << QmlAV::Quote << value;
        }
    });

    if (!forced) {
        codec = avcodec_find_decoder(avStream->codecpar->codec_id);
        if (!codec) {
            logWarning() << "Unable find decoder";
        }
    }

    return codec;
}

template<typename Predicate>
int QmlAVOptions::find(std::string opt, Predicate p) const
{
    return find(std::initializer_list<std::string>{opt}, p);
}

template<typename Predicate>
int QmlAVOptions::find(std::vector<std::string> opts, Predicate p) const
{
    int count = 0;

    for (const auto &i : m_avOptions) {
        if (std::find(opts.begin(), opts.end(), i.first) != opts.end()) {
            ++count;

            using Result = std::invoke_result_t<std::decay_t<Predicate>, std::string>;

            if constexpr (std::is_same_v<Result, QmlAVOptions::FindControl>) {
                if (p(i.second) == QmlAVOptions::MultiKey) {
                    continue;
                }
            } else {
                p(i.second);
            }

            break;
        }
    }

    return count;
}

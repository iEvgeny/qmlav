#include "qmlavoptions.h"
#include "qmlavhwoutput.h"

#include <QGuiApplication>

extern "C" {
#include <libavutil/parseutils.h>
}

QmlAVOptions::QmlAVOptions(const QVariantMap &avOptions)
    : m_avOptions(avOptions)
{
}

QmlAVOptions::operator AVDictionaryPtr() const
{
    AVDictionaryPtr dict;

    for (const auto &[key, value] : asKeyValueRange(m_avOptions)) {
        dict.set(key.toStdString(), value.toString().toStdString());
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
#if defined(__linux__) && !defined(__ANDROID__)
        if (value == "glx") {
            if (avHWDeviceType() != AV_HWDEVICE_TYPE_VAAPI ||
                QGuiApplication::platformName() != "xcb") {
                logWarning() << "The \"" << value << "\" output module does not match the selected hardware decoder or the \"xcb\" platform underlying Qt!";
                return;
            }

            hwOutput = std::make_shared<QmlAVHWOutput_VAAPI_GLX>();
            return;
        }
#endif

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

    const AVCodec *codec = nullptr;
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

uint32_t QmlAVOptions::demuxerTimeout() const
{
    uint32_t t = 30000000; // 30 sec. by default

    find("demuxer_timeout", [&](uint32_t value) {
        t = value;
    });

    return t;
}

bool QmlAVOptions::videoDisable() const
{
    bool disable = false;

    find("vn", [&](bool value) {
        disable = value;
    });

    return disable;
}

bool QmlAVOptions::audioDisable() const
{
    bool disable = false;

    find("an", [&](bool value) {
        disable = value;
    });

    return disable;
}

std::optional<bool> QmlAVOptions::realTime() const
{
    std::optional<bool> rt = std::nullopt;

    find("rt", [&](bool value) {
        rt = value;
    });

    return rt;
}

std::optional<AVRational> QmlAVOptions::aspectRatio() const
{
    std::optional<AVRational> ratio = std::nullopt;

    find("aspect", [&](std::string value) {
        AVRational q;
        if (av_parse_ratio_quiet(&q, value.c_str(), 255) < 0 || q.num <= 0 || q.den <= 0) {
            logWarning() << "Invalid aspect ratio: " << QmlAV::Quote << value;
            return;
        }

        ratio = q;
    });

    return ratio;
}

template<>
bool QmlAVOptions::sTo<bool>(std::string value) const
{
    return value.size() == 0 || value == "true";
}

template<>
uint32_t QmlAVOptions::sTo<uint32_t>(std::string value) const
{
    return std::stoul(value);
}

template<typename Callback>
int QmlAVOptions::find(std::string opt, Callback cb) const
{
    return find(std::initializer_list<std::string>{opt}, cb);
}

template<typename Callback>
int QmlAVOptions::find(std::vector<std::string> opts, Callback cb) const
{
    int count = 0;

    using Result = QmlAVUtils::InvokeResult<Callback>;
    using ValueType = std::tuple_element_t<0, QmlAVUtils::InvokeArgsTuple<Callback>>;

    for (const auto &[key, value] : asKeyValueRange(m_avOptions)) {
        if (std::find(opts.begin(), opts.end(), key.toStdString()) != opts.end()) {
            ++count;

            try {
                auto typedValue = sTo<ValueType>(value.toString().toStdString());

                if constexpr (std::is_same_v<Result, QmlAVOptions::FindControl>) {
                    if (cb(typedValue) == QmlAVOptions::MultiKey) {
                        continue;
                    }
                } else {
                    cb(typedValue);
                }
            } catch (...) {
                logWarning() << "Invalid value for -" << key << " option";
            }

            break;
        }
    }

    return count;
}

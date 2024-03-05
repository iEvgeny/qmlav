#ifndef QMLAVOPTIONS_H
#define QMLAVOPTIONS_H

extern "C" {
#include <libavformat/avformat.h>
}

#include <memory>

#include <QVariantMap>

#include "qmlavutils.h"

class QmlAVHWOutput;

// Unlike std::unique_ptr, Deleter in std::shared_ptr is not part of the type,
// so for convenience you have to use a wrapper.
class AVDictionaryPtr
{
public:
    void set(std::string key, std::string value) {
        av_dict_set(m_avDict.get(), key.c_str(), value.c_str(), AV_DICT_MULTIKEY);
    }

    operator AVDictionary *() { return *m_avDict.get(); }
    operator AVDictionary **() { return m_avDict.get(); }

    std::string getString() const {
        char *buf;
        av_dict_get_string(*m_avDict.get(), &buf, '=', ',');
        std::string str(buf);
        av_freep(&buf);
        return str;
    }

private:
    std::shared_ptr<AVDictionary *> m_avDict = std::shared_ptr<AVDictionary *>(new AVDictionary *{}, [](auto p) {
        av_dict_free(p);
        delete p;
    });
};

class QmlAVOptions
{
public:
    enum FindControl {
        SingleKey,
        MultiKey
    };

    QmlAVOptions() { }
    QmlAVOptions(const QVariantMap &avOptions);

    operator AVDictionaryPtr() const;

    LIBAVFORMAT_CONST AVInputFormat *avInputFormat() const;
    AVHWDeviceType avHWDeviceType() const;
    std::shared_ptr<QmlAVHWOutput> hwOutput() const;
    const AVCodec *avCodec(const AVStream *avStream) const;
    uint32_t demuxerTimeout() const;
    bool videoDisable() const;
    bool audioDisable() const;

protected:
    template<typename T> T sTo(std::string value) const { return value; }
    template<typename Callback> int find(std::string opt, Callback cb) const;
    template<typename Callback> int find(std::vector<std::string> opts, Callback cb) const;

private:
    std::vector<std::pair<std::string, std::string>> m_avOptions;
};

#endif // QMLAVOPTIONS_H

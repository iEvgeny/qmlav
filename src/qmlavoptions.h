#ifndef QMLAVOPTIONS_H
#define QMLAVOPTIONS_H

extern "C" {
#include <libavformat/avformat.h>
}

#include <memory>

#include <QVariantMap>

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
    });
};

class QmlAVOptions
{
public:
    enum FindControl {
        SingleKey,
        MultiKey
    };

    QmlAVOptions(const QVariantMap &avOptions);

    operator AVDictionaryPtr() const;

    AVInputFormat *avInputFormat() const;
    AVHWDeviceType avHWDeviceType() const;
    std::shared_ptr<QmlAVHWOutput> hwOutput() const;

protected:
    template<typename Predicate> int find(std::string arg, Predicate p) const;

private:
    std::vector<std::pair<std::string, std::string>> m_avOptions;
};

#endif // QMLAVOPTIONS_H

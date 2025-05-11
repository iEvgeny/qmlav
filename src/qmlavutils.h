#ifndef QMLAVUTILS_H
#define QMLAVUTILS_H

extern "C" {
#include <libavutil/cpu.h>
#include <libavformat/avformat.h>
}

#include <cxxabi.h>

#include <QLoggingCategory>

// Compat
#if (LIBAVFORMAT_VERSION_MAJOR < 59)
#define LIBAVFORMAT_CONST
#else
#define LIBAVFORMAT_CONST const
#endif

#define FFMPEG_ALIGNMENT (av_cpu_max_align())
#define QMLAV_NUM_DATA_POINTERS (4)

#undef av_err2str
#define av_err2str(errnum) \
    av_make_error_string(static_cast<char *>(alloca(AV_ERROR_MAX_STRING_SIZE)), AV_ERROR_MAX_STRING_SIZE, errnum)

// FIXME: Deprecated since Qt 6.4 and later (Use built-in QMap::asKeyValueRange())
// https://stackoverflow.com/questions/8517853/iterating-over-a-qmap-with-for/77994379#77994379
template<typename T> class KeyValueRange {
public:
    KeyValueRange(T &iterable) : iterable(iterable) { }
    auto begin() const { return iterable.keyValueBegin(); }
    auto end() const { return iterable.keyValueEnd(); }
private:
    T iterable;
};
template <typename T> auto asKeyValueRange(const T &iterable) { return KeyValueRange<const T &>(iterable); }

template<typename T,
         std::memory_order StoreOrder = std::memory_order_seq_cst,
         std::memory_order LoadOrder = StoreOrder>
struct QmlAVAtomic
{
    constexpr QmlAVAtomic(T value) noexcept : m_value(value) { }

    T get() const noexcept { return m_value.load(LoadOrder); }
    operator T() const noexcept { return get(); }

    T operator=(T i) noexcept { m_value.store(i, StoreOrder); return i; }
    T operator++(int) noexcept { return m_value.fetch_add(1, StoreOrder); }
    T operator--(int) noexcept { return m_value.fetch_sub(1, StoreOrder); }
    T operator++() noexcept { return m_value.fetch_add(1, StoreOrder) + 1; }
    T operator--() noexcept { return m_value.fetch_sub(1, StoreOrder) - 1; }

    // TODO: Implement other members as needed

private:
    std::atomic<T> m_value;
};

template<typename T, typename __Super = QmlAVAtomic<T, std::memory_order_relaxed>>
struct QmlAVRelaxedAtomic : __Super
{
    constexpr QmlAVRelaxedAtomic(T value) noexcept : __Super(value) { }

    using __Super::operator=;
};

template<typename T, typename __Super = QmlAVAtomic<T, std::memory_order_release, std::memory_order_acquire>>
struct QmlAVReleaseAcquireAtomic : __Super
{
    constexpr QmlAVReleaseAcquireAtomic(T value) noexcept : __Super(value) { }

    using __Super::operator=;
};

template<typename T>
class QMLAVSoftLimit
{
public:
    QMLAVSoftLimit() : QMLAVSoftLimit(std::numeric_limits<T>::max()) { }
    explicit QMLAVSoftLimit(T limit, T alpha = 0.01)
        : m_alpha(alpha)
        , m_limit(limit)
        , m_average(0) { }

    T average() const { return m_average; }
    T limit() const { return m_limit; }
    void setLimit(T limit) { m_limit = limit; }
    bool addValue(T value) {
        m_average = ema(value);
        return m_average < m_limit;
    }

protected:
    // EMA (Exponential Moving Average)
    T ema(T value) {
        return m_alpha * value + (1 - m_alpha) * m_average;
    }

private:
    T m_alpha;
    T m_limit;
    T m_average;

#ifndef QT_NO_DEBUG_STREAM
    friend QDebug operator<<(QDebug dbg, const QMLAVSoftLimit &softLimit) {
        return dbg.nospace() << "QMLAVSoftLimit(average=" << softLimit.m_average
                             << ", alpha=" << softLimit.m_alpha
                             << ", limit=" << softLimit.m_limit << ")";
    }
#endif
};

template<typename T>
class AVSmartPtr
{
public:
    AVSmartPtr() { m_ref = av_alloc(); }
    AVSmartPtr(const T *ref) noexcept { m_ref = ref; assert(m_ref); }
    AVSmartPtr(const AVSmartPtr &other) {
        m_ref = av_alloc();
        if (m_ref && other.m_ref) {
            av_ref(m_ref, other.m_ref);
        }
    }
    AVSmartPtr(AVSmartPtr &&other) {
        m_ref = av_alloc();
        if (m_ref && other.m_ref) {
            av_move_ref(m_ref, other.m_ref);
        }
    }
    virtual ~AVSmartPtr() {
        av_free(&m_ref);
    }

    T *get() const noexcept { return m_ref; }
    AVSmartPtr move_ref() {
        AVSmartPtr tmp;
        if (m_ref) {
            av_move_ref(tmp.m_ref, m_ref);
        }
        return tmp;
    }
    void unref() { av_unref(m_ref); }

    explicit operator bool() const noexcept { return m_ref && m_ref->data; }
    operator T *() const noexcept { return m_ref; }
    T *operator->() const noexcept { return m_ref; }
    T &operator*() const noexcept { return *m_ref; }
    AVSmartPtr &operator=(const AVSmartPtr &other) {
        if (m_ref && other.m_ref && this != std::addressof(other)) {
            av_unref(m_ref);
            av_ref(m_ref, other.m_ref);
        }
        return *this;
    }
    AVSmartPtr &operator=(AVSmartPtr &&other) {
        if (m_ref && other.m_ref && this != std::addressof(other)) {
            av_unref(m_ref);
            av_move_ref(m_ref, other.m_ref);
        }
        return *this;
    }

    // NOTE: Using static polymorphism to ensure that methods can be called in the constructor and destructor.
    // These prototypes must be implemented in derived classes.
    static T *av_alloc();
    static void av_ref(T *dst, T *src);
    static void av_unref(T *ref);
    static void av_move_ref(T *dst, T *src);
    static void av_free(T **ref);

private:
    T *m_ref;
};

using AVPacketPtr = AVSmartPtr<AVPacket>;
template<> inline AVPacket *AVSmartPtr<AVPacket>::av_alloc() { return av_packet_alloc(); }
template<> inline void AVSmartPtr<AVPacket>::av_ref(AVPacket *dst, AVPacket *src) { av_packet_ref(dst, src); }
template<> inline void AVSmartPtr<AVPacket>::av_unref(AVPacket *ref) { av_packet_unref(ref); }
template<> inline void AVSmartPtr<AVPacket>::av_move_ref(AVPacket *dst, AVPacket *src) { av_packet_move_ref(dst, src); }
template<> inline void AVSmartPtr<AVPacket>::av_free(AVPacket **ref) { av_packet_free(ref); }

using AVFramePtr = AVSmartPtr<AVFrame>;
template<> inline AVFrame *AVSmartPtr<AVFrame>::av_alloc() { return av_frame_alloc(); }
template<> inline void AVSmartPtr<AVFrame>::av_ref(AVFrame *dst, AVFrame *src) { av_frame_ref(dst, src); }
template<> inline void AVSmartPtr<AVFrame>::av_unref(AVFrame *ref) { av_frame_unref(ref); }
template<> inline void AVSmartPtr<AVFrame>::av_move_ref(AVFrame *dst, AVFrame *src) { av_frame_move_ref(dst, src); }
template<> inline void AVSmartPtr<AVFrame>::av_free(AVFrame **ref) { av_frame_free(ref); }

namespace QmlAV
{
enum LogControl {
    Space,
    NoSpace,
    Quote,
    NoQuote
};

inline QTextStream &Hex(QTextStream &s) {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    return ::hex(s);
#else
    return Qt::hex(s);
#endif
}
}

#ifndef QT_NO_DEBUG_STREAM
inline QDebug operator<<(QDebug dbg, QmlAV::LogControl logCtrl)
{
    switch (logCtrl) {
    case QmlAV::Space:
        return dbg.space();
    case QmlAV::NoSpace:
        return dbg.nospace();
    case QmlAV::Quote:
        return dbg.quote();
    case QmlAV::NoQuote:
        return dbg.noquote();
    }

    return dbg;
}
inline QDebug operator<<(QDebug dbg, const std::string& str)
{
    return dbg << QString::fromStdString(str);
}
#endif

class QmlAVUtils
{
public:
    template<typename T>
    using EnableForNonVoid = std::enable_if_t<!std::is_same_v<T, void>>;

    template<typename ...Types>
    using DecayedTuple = std::tuple<std::decay_t<Types>...>;

    template<typename Callable>
    struct FunctorTraits;

    template<typename Callable>
    struct FunctionTraits
        // Lambda/Functors support
        // NOTE: operator () overloading is not supported. See std::invoke/std::apply implementation
        : public FunctorTraits<decltype(&std::remove_reference_t<Callable>::operator())> { };

    template<typename Ret, typename ...Args>
    struct FunctionTraits<Ret (Args...)> {
        using ArgsTuple = DecayedTuple<Args...>;
        using Result = Ret;

        // NOTE: std::tuple is decayed into arguments as a sequence of lvalue or rvalue references,
        // depending on how it was passed to std::apply (See std::get and std::apply implementation).
        template<typename Callable>
        constexpr static auto invoke(Callable callable, std::decay_t<Args> &...args) {
            // NOTE: Here we do not use the idiom of perfect forwarding.
            // It is a casting of argument types according to the signature of the called object.
            return std::invoke(callable, std::forward<Args>(args)...);
        }
    };

    template<typename Ret, typename ...Args>
    struct FunctionTraits<Ret (*)(Args...)>
        : public FunctionTraits<Ret (Args...)> { };

    template<typename Ret, typename Type, typename ...Args>
    struct FunctionTraits<Ret (Type::*)(Args...)>
        : public FunctionTraits<Ret (Type*, Args...)> { };

    template<typename Ret, typename Type, typename ...Args>
    struct FunctionTraits<Ret (Type::*)(Args...) const>
        : public FunctionTraits<Ret (Type*, Args...)> { };

    template<typename Ret, typename Type, typename ...Args>
    struct FunctorTraits<Ret (Type::*)(Args...)>
        : public FunctionTraits<Ret (Args...)> { };

    template<typename Ret, typename Type, typename ...Args>
    struct FunctorTraits<Ret (Type::*)(Args...) const>
        : public FunctionTraits<Ret (Args...)> { };

    template<typename Callable>
    using InvokeArgsTuple = typename FunctionTraits<std::remove_reference_t<Callable>>::ArgsTuple;

    template<typename Callable>
    using InvokeResult = typename FunctionTraits<std::remove_reference_t<Callable>>::Result;

    // Logging tools
    static QLoggingCategory &loggingCategory() { return m_loggingCategory; }
    template<typename T>
    static QString rttiTypeName([[maybe_unused]] const T &type) {
        char *n = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
        QString ret(n);
        free(n);
        return ret;
    }
    template<typename T>
    static QString logPrefix(const T *sender) {
        return QString("[%1 @ 0x%2] ").arg(rttiTypeName(*sender), QString().number(reinterpret_cast<uintptr_t>(sender), 16));
    }
    template<typename T>
    static QDebug log(const T *sender, QDebug logger) {
        return logger.nospace().noquote() << QmlAVUtils::logPrefix(sender);
    }

private:
    static QLoggingCategory m_loggingCategory;
};

#define logDebug() QmlAVUtils::log(this, QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC).debug(QmlAVUtils::loggingCategory()))
#define logInfo() QmlAVUtils::log(this, QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC).info(QmlAVUtils::loggingCategory()))
#define logWarning() QmlAVUtils::log(this, QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC).warning(QmlAVUtils::loggingCategory()))
#define logCritical() QmlAVUtils::log(this, QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC).critical(QmlAVUtils::loggingCategory()))

#endif // QMLAVUTILS_H

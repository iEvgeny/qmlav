#ifndef QMLAVPROPERTYHELPERS_H
#define QMLAVPROPERTYHELPERS_H

#include <type_traits>

#include <QVideoFrame>

template<typename T>
struct QmlAVPropertyTypeImpl {
    static constexpr bool byValue =
        std::is_fundamental_v<T> ||
        std::is_enum_v<T>        ||
        std::is_pointer_v<T>     ||
        // Heuristics for classes/structures
        (std::is_class_v<T> && std::is_trivially_copyable_v<T>);

#if QT_VERSION_MAJOR > 5
    using Type = std::conditional_t<byValue, T, const T&>;
#else
    // MOC Qt5 generates code that is incompatible with referenceable types
    using Type = T;
#endif
};

// Example of specialization for the "heavy" type
template<> struct QmlAVPropertyTypeImpl<QVideoFrame> { using Type = const QVideoFrame; };

template<typename T>
using QmlAVPropertyType = typename QmlAVPropertyTypeImpl<T>::Type;

#define QMLAV_PROPERTY_SETTER_IMPL(type, name, write, notify) \
    void write(QmlAVPropertyType<type> val) { \
        if (m_##name == val) { \
            return; \
        } \
        m_##name = val; \
        Q_EMIT notify(val); \
    }

#define QMLAV_PROPERTY_SETTER_DECL(type, write) \
    void write(QmlAVPropertyType<type>);

// NOTE: These macros must end with a terminator or initializer
#define QMLAV_PROPERTY_CONST(type, name) \
    Q_PROPERTY(type name READ name CONSTANT FINAL) \
public: \
    QmlAVPropertyType<type> name() const { return m_##name; } \
private: \
    type m_##name

#define QMLAV_PROPERTY_READONLY(type, name, notify) \
    Q_PROPERTY(type name READ name NOTIFY notify FINAL) \
public: \
    QmlAVPropertyType<type> name() const { return m_##name; } \
Q_SIGNALS: \
    void notify(QmlAVPropertyType<type>); \
private: \
    type m_##name

#define QMLAV_PROPERTY(type, name, write, notify) \
    Q_PROPERTY(type name READ name WRITE write NOTIFY notify FINAL) \
public: \
    QmlAVPropertyType<type> name() const { return m_##name; } \
public Q_SLOTS: \
    QMLAV_PROPERTY_SETTER_IMPL(type, name, write, notify) \
 Q_SIGNALS: \
    void notify(QmlAVPropertyType<type>); \
private: \
    type m_##name

#define QMLAV_PROPERTY_DECL(type, name, write, notify) \
    Q_PROPERTY(type name READ name WRITE write NOTIFY notify FINAL) \
public: \
    QmlAVPropertyType<type> name() const { return m_##name; } \
public Q_SLOTS: \
    QMLAV_PROPERTY_SETTER_DECL(type, write) \
Q_SIGNALS: \
    void notify(QmlAVPropertyType<type>); \
private: \
    type m_##name

#endif // QMLAVPROPERTYHELPERS_H

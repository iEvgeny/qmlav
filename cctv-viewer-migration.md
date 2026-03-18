# cctv-viewer Qt6 Migration Guide

After updating the qmlav submodule to the Qt5/Qt6 compatible version, the following changes are needed in cctv-viewer.

## 1. CMakeLists.txt

QuickCompiler is a separate Qt5 component but is integrated into Qt6 Core. Gate it:

```cmake
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Quick Multimedia LinguistTools REQUIRED)
if (${QT_VERSION_MAJOR} GREATER 5)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Quick Multimedia LinguistTools REQUIRED)
else()
    find_package(Qt${QT_VERSION_MAJOR} 5.12 COMPONENTS Core Quick Multimedia QuickCompiler LinguistTools REQUIRED)
endif()
```

Resource compilation also differs — `qtquick_compiler_add_resources` does not exist in Qt6:

```cmake
if (${QT_VERSION_MAJOR} GREATER 5)
    qt_add_resources(RESOURCES cctv-viewer.qrc)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    qt5_add_resources(RESOURCES cctv-viewer.qrc)
else()
    qtquick_compiler_add_resources(RESOURCES cctv-viewer.qrc)
endif()
```

## 2. VideoOutput binding (QML)

**File:** `src/Player.qml`

Qt6 removed `VideoOutput.source`. Both properties can be set simultaneously — `source` is silently ignored on Qt6, and `videoSink` is silently ignored on Qt5:

```qml
VideoOutput {
    id: videoOutput

    source: qmlAvPlayer
    anchors.fill: parent
}

QmlAVPlayer {
    id: qmlAvPlayer

    videoSink: videoOutput.videoSink
}
```

## 3. QQmlListProperty (C++)

**Files:** `src/viewportslayoutscollectionmodel.h`, `src/viewportslayoutscollectionmodel.cpp`

Qt6 changed callback signatures from `int` to `qsizetype`:

```cpp
// viewportslayoutscollectionmodel.h
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static qsizetype modelsCount(QQmlListProperty<ViewportsLayoutModel> *list);
static ViewportsLayoutModel *model(QQmlListProperty<ViewportsLayoutModel> *list, qsizetype index);
#else
static int modelsCount(QQmlListProperty<ViewportsLayoutModel> *list);
static ViewportsLayoutModel *model(QQmlListProperty<ViewportsLayoutModel> *list, int index);
#endif
```

```cpp
// viewportslayoutscollectionmodel.cpp
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
qsizetype ViewportsLayoutsCollectionModel::modelsCount(QQmlListProperty<ViewportsLayoutModel> *list)
#else
int ViewportsLayoutsCollectionModel::modelsCount(QQmlListProperty<ViewportsLayoutModel> *list)
#endif
{
    return reinterpret_cast<ViewportsLayoutsCollectionModel *>(list->data)->count();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
ViewportsLayoutModel *ViewportsLayoutsCollectionModel::model(QQmlListProperty<ViewportsLayoutModel> *list, qsizetype index)
#else
ViewportsLayoutModel *ViewportsLayoutsCollectionModel::model(QQmlListProperty<ViewportsLayoutModel> *list, int index)
#endif
{
    QObject *obj = reinterpret_cast<ViewportsLayoutsCollectionModel *>(list->data)->get(index);
    return reinterpret_cast<ViewportsLayoutModel *>(obj);
}
```

## 4. AA_EnableHighDpiScaling (C++)

**File:** `src/main.cpp`

This attribute is default-on and deprecated in Qt6:

```cpp
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
```

## 5. QT_DISABLE_DEPRECATED_BEFORE (CMake)

**File:** `CMakeLists.txt`

Gate the deprecation suppression to Qt5 only so it doesn't hide Qt6-specific deprecations:

```cmake
if (${QT_VERSION_MAJOR} LESS 6)
    add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
endif()
```

## 6. QtGraphicalEffects (QML)

**File:** `src/SideBarItem.qml`

`QtGraphicalEffects` was removed in Qt6. The two `ColorOverlay` instances (icon tinting at lines 157 and 200) need replacement. Options:

**Option A:** Use a `Loader` to pick the right component at runtime:

```qml
// ColorOverlayCompat.qml — resolved via Qt-version-specific import path
// Qt5 version:
import QtGraphicalEffects 1.12
ColorOverlay { }

// Qt6 version:
import QtQuick.Effects
MultiEffect {
    colorization: 1.0
}
```

**Option B:** Replace with `ShaderEffect` that works on both versions (no extra imports):

```qml
ShaderEffect {
    property var source: icon
    property color overlayColor: root.color
    fragmentShader: "qrc:/shaders/color_overlay.frag"
}
```

**Option C:** Use `layer.effect` with a simple opacity/color approach if exact color overlay behavior is not critical.

## 7. QML versioned imports

**Files:** All 20+ QML files in `src/` and `src/imports/`

Qt6 uses versionless imports. Versioned imports still work in Qt6 for core modules but may warn. Versionless imports are supported from Qt 5.15+.

Since cctv-viewer currently targets Qt 5.12+, versioned imports must be kept for now. Once the minimum Qt5 version is raised to 5.15, all imports can be made versionless:

```qml
// Before (Qt 5.12+)
import QtQuick 2.12
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.12

// After (Qt 5.15+ / Qt6)
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
```

## Summary

| Change | Files | Effort |
|--------|-------|--------|
| CMakeLists.txt QuickCompiler | `CMakeLists.txt` | Already done |
| VideoOutput binding | `src/Player.qml` | Trivial — add one line |
| QQmlListProperty signatures | `src/viewportslayoutscollectionmodel.{h,cpp}` | Small — `#if` guards |
| AA_EnableHighDpiScaling | `src/main.cpp` | Trivial — wrap one line |
| QT_DISABLE_DEPRECATED_BEFORE | `CMakeLists.txt` | Trivial — wrap one line |
| QtGraphicalEffects | `src/SideBarItem.qml` | Moderate — needs replacement strategy |
| QML versioned imports | 20+ QML files | Deferred until min version is 5.15 |

# mkPolarisEdge: window decoration plugin for KWin (KDecoration3), based on the decoration of Breeze 6.8.1
cmake_minimum_required(VERSION 3.16)
project(mkpolarisedge VERSION 1.0.0)

set(KF6_MIN_VERSION "6.30.0")
set(QT_MIN_VERSION "6.11.0")
set(KDE_COMPILERSETTINGS_LEVEL "5.82")   # like Breeze: keeps the Qt keywords (emit, signals) the sources use

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_NO_CREATE_VERSIONLESS_TARGETS ON)
set(QT_NO_CREATE_VERSIONLESS_FUNCTIONS ON)
set(QT_MAJOR_VERSION 6)

include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED CONFIG COMPONENTS Core Gui Widgets DBus Svg)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    ColorScheme
    Config
    ConfigWidgets
    CoreAddons
    GuiAddons
    I18n
    IconThemes
    KCMUtils
    WindowSystem)
find_package(KDecoration3 REQUIRED)

add_subdirectory(common)
add_subdirectory(src)

# translations: po/<language>/mkpolarisedge_kwin_deco.po (template: tools/extract-messages.py)
ki18n_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

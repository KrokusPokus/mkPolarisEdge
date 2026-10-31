add_definitions(-DTRANSLATION_DOMAIN="mkauroraenext_kwin_deco")

################# includes #################
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_BINARY_DIR}/common)

################# newt target #################
### plugin classes
set(breezedecoration_SRCS
    breezebutton.cpp
    breezedecoration.cpp
    breezesettingsprovider.cpp
)

### build library
kconfig_add_kcfg_files(breezedecoration_SRCS breezesettings.kcfgc)
qt6_add_resources(breezedecoration_SRCS buttons.qrc)
add_library(breezedecoration MODULE
    ${breezedecoration_SRCS}
    ${breezedecoration_config_PART_FORMS_HEADERS})

set_target_properties(breezedecoration PROPERTIES
    OUTPUT_NAME org.kde.mkauroraenext
)

target_link_libraries(breezedecoration
    PRIVATE
        breezecommon6
        Qt6::DBus
        Qt6::Svg
        KF6::CoreAddons
        KF6::ConfigGui
        KF6::GuiAddons
        KF6::I18n
        KF6::IconThemes
        KDecoration3::KDecoration
        KF6::ColorScheme
)

install(TARGETS breezedecoration DESTINATION ${KDE_INSTALL_PLUGINDIR}/${KDECORATION_PLUGIN_DIR})
install(FILES breezesettingsdata.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR} RENAME mkauroraenext-decoration.kcfg)

add_subdirectory(config)


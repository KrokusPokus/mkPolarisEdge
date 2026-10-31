set(breezedecoration_config_SRCS
    breezeconfigwidget.cpp
)
ki18n_wrap_ui(breezedecoration_config_SRCS
   ui/breezeconfigurationui.ui
)

kcoreaddons_add_plugin(kcm_mkpolarisedge SOURCES kcm_mkpolarisedge.cpp ${breezedecoration_config_SRCS} INSTALL_NAMESPACE "${KDECORATION_KCM_PLUGIN_DIR}")
kconfig_add_kcfg_files(kcm_mkpolarisedge ../breezesettings.kcfgc)
target_include_directories(kcm_mkpolarisedge PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src/)
target_link_libraries(kcm_mkpolarisedge
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
    PRIVATE
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::GuiAddons
        KF6::I18n
        KF6::IconThemes
        KF6::KCMUtils
)
kcmutils_generate_desktop_file(kcm_mkpolarisedge)

# kcmshell6 < 6.27.0 doesn't know the --smoke-test param, so skip this test for
# now.
# TODO: Drop this when KF6_MIN_VERSION >= 6.27.0
if(KF6KCMUtils_VERSION VERSION_GREATER_EQUAL "6.27.0")
    add_test(NAME kcm_mkpolarisedge_smoketest COMMAND kcmshell6 -style Breeze --smoke-test ${KDECORATION_KCM_PLUGIN_DIR}/kcm_mkpolarisedge)
    set_tests_properties(kcm_mkpolarisedge_smoketest PROPERTIES
        ENVIRONMENT_MODIFICATION QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin
    )
endif()

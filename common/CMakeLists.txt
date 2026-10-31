add_library(breezecommon6 OBJECT breezeboxshadowrenderer.cpp)

set_target_properties(breezecommon6 PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_link_libraries(breezecommon6
    PUBLIC
        Qt${QT_MAJOR_VERSION}::Core
        Qt${QT_MAJOR_VERSION}::Gui)

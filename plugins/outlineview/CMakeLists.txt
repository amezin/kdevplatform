add_definitions(-DTRANSLATION_DOMAIN=\"kdevoutlineview\")
set(kdevoutlineview_SRCS
    outlineviewplugin.cpp
    outlinenode.cpp
    outlinemodel.cpp
    outlinewidget.cpp
)

ecm_qt_declare_logging_category(kdevoutlineview_SRCS
    HEADER debug.h
    IDENTIFIER PLUGIN_OUTLINE
    CATEGORY_NAME "kdevplatform.plugins.outline"
)
kdevplatform_add_plugin(kdevoutlineview JSON kdevoutlineview.json SOURCES ${kdevoutlineview_SRCS})
target_link_libraries(kdevoutlineview
    KDev::Interfaces
    KDev::Language
    KF5::CoreAddons
    KF5::I18n
    KF5::ItemModels
    KF5::TextEditor
)

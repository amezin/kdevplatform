kdevplatform_add_file_templates(flatpak-template)

add_definitions(-DTRANSLATION_DOMAIN=\"kdevflatpak\")

ecm_qt_declare_logging_category(flatpakplugin_SRCS
    HEADER debug_flatpak.h
    IDENTIFIER FLATPAK
    CATEGORY_NAME kdevplatform.plugins.flatpak
)

qt5_add_resources(flatpakplugin_SRCS kdevflatpakplugin.qrc)
kdevplatform_add_plugin(kdevflatpak JSON kdevflatpak.json SOURCES flatpakplugin.cpp flatpakruntime.cpp ${flatpakplugin_SRCS})
target_link_libraries(kdevflatpak
    KF5::CoreAddons
    KDev::Interfaces
    KDev::Util
    KDev::OutputView
    KDev::Project
)

install(FILES replicate.sh DESTINATION ${KDE_INSTALL_DATAROOTDIR}/kdevelop/kdevflatpak/)

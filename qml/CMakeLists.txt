# Separate static QML module: keeps the QML type registration in this
# directory scope (attaching the module to the exe from a subdirectory
# breaks the AUTOMOC metatypes dependency).
set_source_files_properties(Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(pepetrack_qml
    STATIC
    URI PepeTrack
    VERSION 1.0
    QML_FILES
        Main.qml
        Theme.qml
        TopBar.qml
        SideNav.qml
        NavItem.qml
        VideoSurface.qml
        PlayerDropdown.qml
        VideoView.qml
        HomographyView.qml
        MetadataView.qml
        RosterTable.qml
        TrackingView.qml
        StatCard.qml
        EditField.qml
)

target_link_libraries(pepetrack_qml PRIVATE Qt6::Quick)

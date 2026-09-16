QT       += core gui svg network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# Wifi / Camera modules need these:
contains(QT_MAJOR_VERSION, 5): QT += concurrent

TARGET = hotel_terminal

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    darktoast.cpp \
    systemintegration.cpp \
    wifi/wifi_manager.cpp \
    wifi/softkeyboard.cpp \
    wifi/wifi_settings_widget.cpp \
    face/camerathread.cpp \
    face/face_camera_widget.cpp \
    face/face_engine.cpp \
    face/lbph_engine.cpp \
    time/ntpsync.cpp \
    cloud/hwcloud_client.cpp \
    cloud/hwcloud_settings_widget.cpp

HEADERS += \
    mainwindow.h \
    darktoast.h \
    systemintegration.h \
    wifi/wifiinfo.h \
    wifi/wifi_manager.h \
    wifi/softkeyboard.h \
    wifi/wifi_settings_widget.h \
    face/camerathread.h \
    face/face_camera_widget.h \
    face/face_engine.h \
    face/lbph_engine.h \
    time/ntpsync.h \
    cloud/hwcloud_client.h \
    cloud/hwcloud_settings_widget.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    icons.qrc

# Windows application icon (also shown in title bar and taskbar)
win32: RC_ICONS = icons/app-icon.ico

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

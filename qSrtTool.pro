QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
win32: LIBS += -lpdh

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
    src/Core/dependencymanager.cpp \
    src/Modules/Loader/embeddedffmpegplayer.cpp \
    src/Widgets/logconsole.cpp \
    src/Widgets/progressbox.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/Modules/OutputMgr/outputmanagement.cpp \
    src/Modules/Burner/subtitleburning.cpp \
    src/Modules/Whisper/subtitleextraction.cpp \
    src/Modules/Translator/subtitletranslation.cpp \
    src/Modules/Downloder/videodownloader.cpp \
    src/Modules/Loader/videoloader.cpp

HEADERS += \
    src/Core/dependencymanager.h \
    src/Modules/Loader/embeddedffmpegplayer.h \
    src/Widgets/logconsole.h \
    src/Widgets/progressbox.h \
    src/mainwindow.h \
    src/Modules/OutputMgr/outputmanagement.h \
    src/Modules/Burner/subtitleburning.h \
    src/Modules/Whisper/subtitleextraction.h \
    src/Modules/Translator/subtitletranslation.h \
    src/Modules/Downloder/videodownloader.h \
    src/Modules/Loader/videoloader.h

FORMS += \
    src/mainwindow.ui \
    src/Modules/OutputMgr/outputmanagement.ui \
    src/Modules/Burner/subtitleburning.ui \
    src/Modules/Whisper/subtitleextraction.ui \
    src/Modules/Translator/subtitletranslation.ui \
    src/Modules/Downloder/videodownloader.ui \
    src/Modules/Loader/videoloader.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources/style.qrc

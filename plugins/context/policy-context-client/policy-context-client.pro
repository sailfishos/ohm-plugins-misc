TEMPLATE     = app
TARGET       = policy-context-tracker
MOC_DIR      = .moc
OBJECTS_DIR  = .obj
DEPENDPATH  += .
QT           = core
PKGCONFIG   += contextsubscriber-1.0
CONFIG      += console link_pkgconfig
CONFIG      -= app_bundle

INCLUDEPATH += $${LIBRESOURCEINC}
QMAKE_CXXFLAGS += -Wall


# Input
HEADERS     = context-tracker.h
SOURCES    += context-client.cpp context-tracker.cpp

QMAKE_DISTCLEAN += -r .moc .obj

# Install options
target.path = /usr/bin/
INSTALLS    = target

#-------------------------------------------------------------------------
# EEN1071 - Pitch Dashboard
#
# Qt widgets app that subscribes to the Pi's accelerometer topic and plots
# the pitch value live.
#
# Only one .pro file lives in this directory: two of them make qmake and
# QtCreator ambiguous about which is the project.
#-------------------------------------------------------------------------

QT       += core gui widgets

TARGET   = een1071-dashboard
TEMPLATE = app

CONFIG  += c++17

# printsupport is no longer needed: that was a QCustomPlot requirement.
# No FORMS either - the UI is built in code with layout managers, so the
# window resizes properly and there is no .ui file to keep in step.

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    plotwidget.cpp

HEADERS += \
    mainwindow.h \
    plotwidget.h

# Paho MQTT C client (apt: libpaho-mqtt-dev, or build from source)
LIBS    += -lpaho-mqtt3c

# Warn about anything Qt has deprecated
DEFINES += QT_DEPRECATED_WARNINGS

# Deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

#-------------------------------------------------
#
# Project created by QtCreator 2013-04-05T21:08:10
#
#-------------------------------------------------

QT       -= gui

TARGET = diagnoseBasic
TEMPLATE = lib

CONFIG += plugins
CONFIG += dll

DEFINES += DIAGNOSEBASIC_LIBRARY
DEFINES += NOMINMAX

SOURCES += diagnosebasic.cpp

HEADERS += diagnosebasic.h

include(../plugin_template.pri)

INCLUDEPATH +=  "$(BOOSTPATH)"

#CONFIG += dll

#DEFINES += INIEDITOR_LIBRARY

OTHER_FILES += \
    diagnosebasic.json

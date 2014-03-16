#-------------------------------------------------
#
# Project created by QtCreator 2013-04-05T21:08:10
#
#-------------------------------------------------

TARGET = diagnoseBasic
TEMPLATE = lib

CONFIG += plugins
CONFIG += dll

contains(QT_VERSION, "^5.*") {
	QT += widgets
}

DEFINES += DIAGNOSEBASIC_LIBRARY
DEFINES += NOMINMAX

# suppress a few warnings caused by boost vs vc++ paranoia
DEFINES += _SCL_SECURE_NO_WARNINGS

SOURCES += diagnosebasic.cpp

HEADERS += diagnosebasic.h

include(../plugin_template.pri)

INCLUDEPATH += "$(BOOSTPATH)"

#CONFIG += dll

#DEFINES += INIEDITOR_LIBRARY

OTHER_FILES += \
    diagnosebasic.json

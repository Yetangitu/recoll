TEMPLATE	= app
LANGUAGE	= C++

CONFIG	+= qt warn_on thread release debug

HEADERS	+= rclmain.h

SOURCES	+= main.cpp \
	rclmain.cpp \
	idxthread.cpp \
	plaintorich.cpp \
	guiutils.cpp

FORMS	= reslistb.ui \
	recollmain.ui \
	advsearch.ui \
	preview/preview.ui \
	sort.ui \
	uiprefs.ui \
	ssearchb.ui

IMAGES	= images/asearch.png \
	images/history.png \
	images/d_nextpage.png \
	images/nextpage.png \
	images/d_prevpage.png \
	images/prevpage.png \
	images/sortparms.png

unix {
  UI_DIR = .ui
  MOC_DIR = .moc
  OBJECTS_DIR = .obj

  DEFINES += RECOLL_DATADIR=\"/usr/local/share\"
  LIBS += ../lib/librcl.a ../bincimapmime/libmime.a \
            $(BSTATIC) -L/usr/local/lib -lxapian -L/usr/local/lib -liconv $(BDYNAMIC) \
           -lkdecore -lz

  INCLUDEPATH += ../common ../index ../query ../unac ../utils 
  POST_TARGETDEPS = ../lib/librcl.a
}

UNAME = $$system(uname -s)
contains( UNAME, [lL]inux ) {
          LIBS -= -liconv
}

TRANSLATIONS = recoll_fr.ts

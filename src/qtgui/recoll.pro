TEMPLATE	= app
LANGUAGE	= C++

CONFIG	+= qt warn_on thread release

SOURCES	+= main.cpp \
	idxthread.cpp

FORMS	= recollmain.ui

IMAGES	= images/filenew \
	images/fileopen \
	images/filesave \
	images/print \
	images/undo \
	images/redo \
	images/editcut \
	images/editcopy \
	images/editpaste \
	images/searchfind

unix {
  UI_DIR = .ui
  MOC_DIR = .moc
  OBJECTS_DIR = .obj
  LIBS += ../lib/librcl.a -L/usr/local/lib -lxapian -liconv \
       -lfontconfig -lfreetype -lexpat -lz
  INCLUDEPATH += ../common ../index ../query ../unac ../utils 
  #QMAKE_LFLAGS_SHAPP += -static
}

UNAME = $$system(uname -s)
contains( UNAME, [lL]inux ) {
	  LIBS -= -liconv
}

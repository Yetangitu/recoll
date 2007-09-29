#ifndef _confgui_h_included_
#define _confgui_h_included_
/* @(#$Id: confgui.h,v 1.3 2007-09-29 09:06:53 dockes Exp $  (C) 2007 J.F.Dockes */

#include <string>

#include <qstring.h>
#include <qwidget.h>

#include "refcntr.h"

using std::string;

class QHBoxLayout;
class QLineEdit;
class QListBox;
class RclConfig;
class QSpinBox;
class QComboBox;
class QCheckBox;

namespace confgui {

    // A class to isolate the gui widget from the config storage mechanism
    class ConfLinkRep {
    public:
	virtual bool set(const string& val) = 0;
	virtual bool get(string& val) = 0;
    };
    typedef RefCntr<ConfLinkRep> ConfLink;

    class ConfLinkNullRep : public ConfLinkRep {
    public:
	virtual bool set(const string&)
	{
	    //fprintf(stderr, "Setting value to [%s]\n", val.c_str());
	    return true;
	}
	virtual bool get(string& val) {val = ""; return true;}
    };

    // A widget to let the user change one configuration
    // parameter. Subclassed for specific parameter types
    class ConfParamW : public QWidget {
	Q_OBJECT
    public:
	ConfParamW(QWidget *parent, ConfLink cflink)
	    : QWidget(parent), m_cflink(cflink)
	{
	}
	virtual void loadValue() = 0;

    protected:
	ConfLink     m_cflink;
	QHBoxLayout *m_hl;

	virtual bool createCommon(const QString& lbltxt,
				  const QString& tltptxt);

    protected slots:
        void setValue(const QString& newvalue);
        void setValue(int newvalue);
        void setValue(bool newvalue);
    };


    // Widgets for setting the different types of configuration parameters:

    // Int
    class ConfParamIntW : public ConfParamW {
    public:
	ConfParamIntW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt,
		      int minvalue = INT_MIN, 
		      int maxvalue = INT_MAX);
	virtual void loadValue();
    protected:
	QSpinBox *m_sb;
    };

    // Arbitrary string
    class ConfParamStrW : public ConfParamW {
    public:
	ConfParamStrW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
	virtual void loadValue();
    protected:
	QLineEdit *m_le;
    };

    // Constrained string: choose from list
    class ConfParamCStrW : public ConfParamW {
    public:
	ConfParamCStrW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt, const QStringList& sl);
	virtual void loadValue();
    protected:
	QComboBox *m_cmb;
    };

    // Boolean
    class ConfParamBoolW : public ConfParamW {
    public:
	ConfParamBoolW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
	virtual void loadValue();
    protected:
	QCheckBox *m_cb;
    };

    // File name
    class ConfParamFNW : public ConfParamW {
	Q_OBJECT
    public:
	ConfParamFNW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		     const QString& tltptxt, bool isdir = false);
	virtual void loadValue();
    protected slots:
	void showBrowserDialog();
    protected:
	QLineEdit *m_le;
	bool       m_isdir;
    };

    // String list
    class ConfParamSLW : public ConfParamW {
	Q_OBJECT
    public:
	ConfParamSLW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
	virtual void loadValue();
	QListBox *getListBox() {return m_lb;}
	
    protected slots:
	virtual void showInputDialog();
	void deleteSelected();
    signals:
        void entryDeleted(QString);
    protected:
	QListBox *m_lb;
	void listToConf();
	
    };

    // Dir name list
    class ConfParamDNLW : public ConfParamSLW {
	Q_OBJECT
    public:
	ConfParamDNLW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt)
	    : ConfParamSLW(parent, cflink, lbltxt, tltptxt)
	    {
	    }
    protected slots:
	virtual void showInputDialog();
    };

    // Constrained string list (chose from predefined)
    class ConfParamCSLW : public ConfParamSLW {
	Q_OBJECT
    public:
	ConfParamCSLW(QWidget *parent, ConfLink cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt,
		      const QStringList& sl)
	    : ConfParamSLW(parent, cflink, lbltxt, tltptxt), m_sl(sl)
	    {
	    }
    protected slots:
	virtual void showInputDialog();
	const QStringList m_sl;
    };
}

#endif /* _confgui_h_included_ */

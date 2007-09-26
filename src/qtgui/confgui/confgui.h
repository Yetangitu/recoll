#ifndef _confgui_h_included_
#define _confgui_h_included_
/* @(#$Id: confgui.h,v 1.1 2007-09-26 12:16:48 dockes Exp $  (C) 2007 J.F.Dockes */

#include <string>

#include <qstring.h>
#include <qwidget.h>

using std::string;

class QHBoxLayout;
class QLineEdit;
class QListBox;

namespace confgui {

    // A class to isolate the gui widget from the config storage mechanism
    class ConfLink {
    public:
	virtual bool set(const string& val) = 0;
	virtual bool get(string& val) = 0;
    };

    // A widget to let the user change a configuration parameter
    class ConfParamW : public QWidget {
	Q_OBJECT
    public:
	ConfParamW(QWidget *parent, ConfLink &cflink)
	    : QWidget(parent), m_cflink(cflink)
	{
	}
    protected:
	ConfLink& m_cflink;
	QHBoxLayout *m_hl;
	virtual bool createCommon(const QString& lbltxt,
				  const QString& tltptxt);

    protected slots:
        void setValue(const QString& newvalue);
        void setValue(int newvalue);
        void setValue(bool newvalue);
    };


    // Widgets for setting the different types of configuration parameters:
    class ConfParamIntW : public ConfParamW {
    public:
	ConfParamIntW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt,
		      int minvalue = INT_MIN, 
		      int maxvalue = INT_MAX);
    };

    // Arbitrary string
    class ConfParamStrW : public ConfParamW {
    public:
	ConfParamStrW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
    };

    // Constrained string: choose from list
    class ConfParamCStrW : public ConfParamW {
    public:
	ConfParamCStrW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt, const QStringList& sl);
    };

    class ConfParamBoolW : public ConfParamW {
    public:
	ConfParamBoolW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
    };

    class ConfParamFNW : public ConfParamW {
	Q_OBJECT
    public:
	ConfParamFNW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
    protected slots:
	void showBrowserDialog();
    private:
	QLineEdit *m_le;
    };

    // String list
    class ConfParamSLW : public ConfParamW {
	Q_OBJECT
    public:
	ConfParamSLW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt);
    protected slots:
	virtual void showInputDialog();
	void deleteSelected();
    protected:
	QListBox *m_lb;
	void listToConf();
    };

    // File/Dir name list
    class ConfParamFNLW : public ConfParamSLW {
	Q_OBJECT
    public:
	ConfParamFNLW(QWidget *parent, ConfLink& cflink, 
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
	ConfParamCSLW(QWidget *parent, ConfLink& cflink, 
		      const QString& lbltxt,
		      const QString& tltptxt,
		      const QStringList& sl)
	    : ConfParamSLW(parent, cflink, lbltxt, tltptxt), m_sl(sl)
	    {
	    }
    protected slots:
	virtual void showInputDialog();
	const QStringList &m_sl;
    };

}

#endif /* _confgui_h_included_ */

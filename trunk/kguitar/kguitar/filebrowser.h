#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <qwidget.h>
#include <qpushbutton.h>
#include <qcombobox.h>
#include <qlistview.h>
#include <qlabel.h>

#include <kdialog.h>

#include "application.h"


//----------------------------------------------
#include <qstring.h>
#include <qfile.h>

class Directory: public QListViewItem
{
public:
    Directory(QListView *parent);
    Directory(Directory *parent, const char *filename);

    QString text(int column) const;

    QString fullName();

    void setOpen(bool);
    void setup();

private:
    QFile f;
    Directory *p;
    bool readable;
};

//----------------------------------------------

class FileBrowser : public KDialog  {
   Q_OBJECT
public:
   FileBrowser(QWidget *parent=0, const char *name=0);
    ~FileBrowser();

    QLabel *tlabel;
    QLabel *alabel;
    QLabel *tslabel;

public slots:
    void closeDlg();
    void scanDir();
    void playSong();
    void fillFileView(QListViewItem* item);
    void scanSubDirs(QString path);
    void loadSong(QListViewItem* item);

protected:
    QWidget *widget_1;
    QPushButton *btnclose;
    QPushButton *btnscan;
    QPushButton *btnplay;
    QComboBox *jumpcombo;
    QListView *dirlist;
    Directory *directory;
    QListView *fileview;
    QWidget *widget_2;
    QLabel *titlelabel;
    QLabel *authorlabel;
    QLabel *translabel;
    QLabel *messagelabel;

private:
    QString getFullPath(QListViewItem* item);
    QWidget *p;
    bool m_haveMidi;  // ALINXFIX: Will be removed when Midi is implemented !!

signals:
    void loadFile(const KURL& url);

};

#endif

#ifndef CONVERTTEX_H
#define CONVERTTEX_H

#include "global.h"
#include "convertbase.h"

class TabSong;
class QTextStream;

/**
 * Converter to/from TEX tabulature format.
 *
 * Manages conversions of tabulatures between internal KGuitar
 * structures and TEX tabulature formats.
 */
class ConvertTex: public ConvertBase {
public:
	/**
	 * Prepares converter to work, reads all the options, etc.
	 */
	ConvertTex(TabSong *);

	/**
	 * Called to save current data from TabSong into TEX tabulature
	 * format file named fileName.
	 */
	virtual bool save(QString fileName);

	/**
	 * Called to load data from TEX tabulature format file named
	 * fileName into TabSong.
	 */
	virtual bool load(QString fileName);

private:
	bool saveToTab(QTextStream &s);
	bool saveToNotes(QTextStream &s);

	/**
	 * Insert control sequence
	 */
	QString cleanString(QString str);
	QString tab(bool chord, int string, int fret);
	QString getNote(QString note, int duration, bool dot);
};

#endif

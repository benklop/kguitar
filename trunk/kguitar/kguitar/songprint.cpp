/***************************************************************************
 * songprint.cpp: implementation of SongPrint class
 *
 * This file is part of KGuitar, a KDE tabulature editor
 *
 * copyright (C) 2002-2003 the KGuitar development team
 ***************************************************************************/

/***************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * See the file COPYING for more information.
 ***************************************************************************/

// LVIFIX: this file needs to be redesigned

// Notes on vertical spacing:
// Assuming we can accomodate notes one octave above (stem up) and below
// (stem down) the staff, height required is 4 * ystepst (staff)
// + 2 * 7 * ystepst (space above and below). Vertical spacing between
// staffs of between staff and tabbar is 3 * ystepst.
// The tabbar needs (nstrings - 1) * ysteptb + 3 * ysteptb (space below).
// Line spacing is 2 * ysteptb.
//
// Total space required for staff + tabbar + spacing is:
// 21 * ystepst + (nstrings + 4) * ysteptb

// Notes on horizontal spacing:
// If the first note in a measure has an accidental, then space before
// first note must be increased.
// Space between notes depends (especially for short notes) on both
// accidentals and flags/beams. Therefore accidentals and beams need to be
// determined before calculating horizontal note spacing.

// LVIFIX: fix horizontal spacing

// LVIFIX: check width of lower part note stems (seems a bit too thin)

// LVIFIX: "link with previous" doesn't work well
//         - prints rest instead of note
//         - if at start of bar, links to left margin

// LVIFIX: "ringing" and "link with previous" don't work well together
//         doesn't work in midi export either

// LVIFIX: rests in lower voice are not supported

#include <qstring.h>			// required for globaloptions.h :-(
#include "globaloptions.h"

#include "accidentals.h"
#include "songprint.h"
#include "tabsong.h"
#include "tabtrack.h"

#include <kprinter.h>
#include <qmemarray.h>
#include <qpainter.h>
#include <qpaintdevicemetrics.h>

using namespace std;

#include <iostream>

static const QString notes[7] = {"C", "D", "E", "F", "G", "A", "B"};


/***************************************************************************
 * class SongPrint
 ***************************************************************************/

// SongPrint constructor

SongPrint::SongPrint()
{
	p = new QPainter;
}

// SongPrint destructor

SongPrint::~SongPrint()
{
	delete p;
}

// return expandable width in pixels of bar bn in track trk
// this part of the bar is expanded to fill a line completely
// extra space will be added between notes

int SongPrint::barExpWidth(int bn, TabTrack *trk)
{
	int w = 0;
	for (uint t = trk->b[bn].start; (int) t <= trk->lastColumn(bn); t++)
		w += colWidth(t, trk);
	return w;
}

// return width in pixels of bar br in track trk

int SongPrint::barWidth(int bn, TabTrack *trk)
{
	int w = 0;
	for (uint t = trk->b[bn].start; (int) t <= trk->lastColumn(bn); t++)
		w += colWidth(t, trk);
	if (trk->showBarSig(bn))
		w += tsgfw;				// space for timesig
	w += nt0fw;					// space before first note
	w += ntlfw;					// space after last note
	w += 1;						// LVIFIX: the trailing vertical line
	return w;
}

// return width in pixels of column cl in track trk
// depends on note length, font and effect
// magic number "21" scales quarter note to about one centimeter
// LVIFIX: make logarithmic ???

int SongPrint::colWidth(int cl, TabTrack *trk)
{
	int w;
	w = trk->c[cl].l;
	w *= br8w;
	w /= 21;
	// adjust for dots and triplets
	if (trk->c[cl].flags & FLAG_DOT)
		w = (int) (w * 1.5);
	if (trk->c[cl].flags & FLAG_TRIPLET)
		w = (int) (w * 2 / 3);
	// make sure column is wide enough
	if (w < 2 * br8w)
		w = 2 * br8w;
	// make sure effects fit in column
	const int lstStr = trk->string - 1;
	for (int i = 0; i < lstStr + 1; i++) {
		if (   trk->c[cl].e[i] == EFFECT_ARTHARM
			|| trk->c[cl].e[i] == EFFECT_HARMONIC
			|| trk->c[cl].e[i] == EFFECT_LEGATO
			|| trk->c[cl].e[i] == EFFECT_SLIDE)
			if (w < 2 * ysteptb)
				w = 2 * ysteptb;
	}
	if (trk->c[cl].flags & FLAG_PM) {
			if (w < 2 * ysteptb)
				w = 2 * ysteptb;
	}
	return w;
}

// determine if column t in track trk is a rest

static bool isRest(int t, TabTrack *trk)
{
	for (int i = 0; i < trk->string; i++) {
		if (trk->c[t].a[i] > -1) {
			return false;
		}
	}
	return true;
}

// determine starting time of note t in bar bn of track trk

static int tStart(int t, int bn, TabTrack *trk)
{
	int tstart = 0;
	for (uint i = trk->b[bn].start; (int) i < t; i++) {
		// add real duration including dots/triplets
		tstart += trk->c[i].fullDuration();
	}
	return tstart;
}

// determine if beam needs to be broken at note t in track trk for bar bn
// break beam if t does not end in the beat where it started
// results in drawing three beams for six 1/8 notes in a 3/4 measure
// this is independent of voice allocation

static bool mustBreakBeam(int t, int bn, TabTrack *trk)
{
	int bv = 1;					// this bar's beat value as duration
	switch (trk->b[bn].time2) {
	case  1: bv = 480; break;
	case  2: bv = 240; break;
	case  4: bv = 120; break;
	case  8: bv =  60; break;
	case 16: bv =  30; break;
	case 32: bv =  15; break;
	default: bv =   1; break;	// safe default
	}
	int ts = tStart(t, bn, trk);			// note start
	int te = ts + trk->c[t].fullDuration();	// note end
	int bs = ts / bv;						// beat where note starts
	int be = te / bv;						// beat where note ends
	return (bs != be);
}

// determine L1 beam for note t in voice v in bar bn of track trk
// returns: c (continue) e (end) n (none) s (start)
// LVIFIX: save results ?

static char beamL1(int t, int v, int bn, TabTrack *trk)
{
	// if column is a rest, then no beam
	if (isRest(t, trk)) {
		return 'n';
	}
	// if no note in this voice, then no beam
	int dt;						// dots (not used)
	int tp;						// note type
	bool tr;					// triplet (not used)
	if (!trk->getNoteTypeAndDots(t, v, tp, dt, tr)) {
		return 'n';
	}
	// if note is 1/4 or longer, then no beam
	if (tp >= 120) {
		return 'n';
	}

	int f = trk->b[bn].start;	// first note of bar
	int l = trk->lastColumn(bn);// last note of bar
	int p = 0;					// previous note
	int n = 0;					// next nnote
	p = (t == f) ? -1 : (t - 1);
	n = (t == l) ? -1 : (t + 1);
	int ptp = 480;				// previous note type, default to 1/1
	int ntp = 480;				// next note type, default to 1/1
	if ((p == -1) || !trk->getNoteTypeAndDots(p, v, ptp, dt, tr)) {
		// no previous note (or not in this voice),
		// therefore pretend 1/1 (which has no stem)
		ptp = 480;
	}
	if ((n == -1) || !trk->getNoteTypeAndDots(n, v, ntp, dt, tr)) {
		// no previous note (or not in this voice),
		// therefore pretend 1/1 (which has no stem)
		ntp = 480;
	}
	if (mustBreakBeam(t, bn, trk)) {
		// note ends at n * divisor
		if ((p != -1) && (ptp <= 60) && !mustBreakBeam(p, bn, trk)
			&& !isRest(p, trk)) {
			// previous note exists which has beam to this one
			return 'e';
		} else {
			return 'n';
		}
	} else {
		// note does not end at n * divisor
		bool left = false;		// beam at left side ?
		bool right = false;		// beam at right side ?
		if ((p != -1) && (ptp <= 60) && !mustBreakBeam(p, bn, trk)
			&& !isRest(p, trk)) {
			// previous note exists which has beam to this one
			left = true;
		}
		if ((n != -1) && (ntp <= 60) && !isRest(n, trk)) {
			// next note exists to draw beam to
			right = true;
		}
		// test all possible combinations of left and right
		if (left && right) {
			return 'c';
		}
		if (left && !right) {
			return 'e';
		}
		if (!left && right) {
			return 's';
		}
		if (!left && !right) {
			return 'n';
		}
	}
	return 'n';
}

// determine beam for note t in voice v in bar bn in track trk
// at beam level lvl
// returns: b (backward) c (continue) e (end) f (forward) n (none) s (start)
// note: no need to check for rests (done in beamL1)
// LVIFIX: save results ?

static char beamL2plus(int t, int v, int bn, int lvl, TabTrack *trk)
{
	// if no note in this voice, then no beam
	int dt;						// dots (not used)
	int tp;						// note type
	bool tr;					// triplet (not used)
	if (!trk->getNoteTypeAndDots(t, v, tp, dt, tr)) {
		return 'n';
	}
	// determine duration for this level
	int dur = 0;
	if (lvl == 2) {
		// if note is 1/8 or longer, then no beam
		if (tp >= 60) {
			return 'n';
		} else {
			dur = 30;
		}
	} else if (lvl == 3) {
		// if note is 1/16 or longer, then no beam
		if (tp >= 30) {
			return 'n';
		} else {
			dur = 15;
		}
	} else {
		return 'n';
	}
	int f = trk->b[bn].start;	// first note of bar
	int l = trk->lastColumn(bn);// last note of bar
	int p = 0;					// previous note
	int n = 0;					// next next
	p = (t == f) ? -1 : (t - 1);
	n = (t == l) ? -1 : (t + 1);
	int ptp = 480;				// previous note type, default to 1/1
	int ntp = 480;				// next note type, default to 1/1
	if ((p == -1) || !trk->getNoteTypeAndDots(p, v, ptp, dt, tr)) {
		// no previous note (or not in this voice),
		// therefore pretend 1/1 (which has no stem)
		ptp = 480;
	}
	if ((n == -1) || !trk->getNoteTypeAndDots(n, v, ntp, dt, tr)) {
		// no previous note (or not in this voice),
		// therefore pretend 1/1 (which has no stem)
		ntp = 480;
	}
	char L1 = beamL1(t, v, bn, trk);
	if (L1 == 's') {
		if ((n != -1) && (ntp <= dur)) {
			return 's';
		} else {
			return 'f';
		}
	} else if (L1 == 'c') {
		bool left = false;		// beam at left side ?
		bool right = false;		// beam at right side ?
		if ((p != -1) && (ptp <= dur) && !(mustBreakBeam(p, bn, trk))) {
			// previous note exists which has beam to this one
			left = true;
		}
		if ((n != -1) && (ntp <= dur)) {
			// next note exists to draw beam to
			right = true;
		}
		// test all possible combinations of left and right
		if (left && right) {
			return 'c';
		}
		if (left && !right) {
			return 'e';
		}
		if (!left && right) {
			return 's';
		}
		if (!left && !right) {
			return 'f';
		}
	} else if (L1 == 'e') {
		if ((p != -1) && (ptp <= dur)) {
			// previous note exists which has beam to this one
			return 'e';
		} else {
			return 'b';
		}
	} else {
		return 'n';
	}
	return 'n';
}

// draw bar bn's contents starting at xpos,ypostb adding extra space es

void SongPrint::drawBar(int bn, TabTrack *trk, int es)
{
	TabTrack *curt = trk;		// LVIFIX

	int lastxpos = 0;			// fix compiler warning
	int extSpAftNote = 0;		// extra space, divided over the notes
	int xdelta = 0;				// used for drawing beams, legato and slide
	bool ringing[MAX_STRINGS];
	uint s = curt->string - 1;
	int i = 0;
	
	for (uint i = 0; i <= s; i++) {
		ringing[i] = FALSE;
	}

	// print timesig if necessary
	// LVIFIX: may need to center horizontally
	if (trk->showBarSig(bn)) {
		int brth;
		QFontMetrics fm = p->fontMetrics();
		QString time;
		int y;
		if (stNts) {
			// staff
			p->setFont(fFetaNr);
			fm = p->fontMetrics();
			// calculate vertical position:
			// exactly halfway between top and bottom string
			y = yposst - ystepst * 2;
			// center the timesig at this height
			// use spacing of 0.2 * char height
			time.setNum(trk->b[bn].time1);
			brth = fm.boundingRect(time).height();
			y -= (int) (0.1 * brth);
			p->drawText(xpos + tsgpp, y, time);
			time.setNum(trk->b[bn].time2);
			y += (int) (1.2 * brth);
			p->drawText(xpos + tsgpp, y, time);
		}
		if (stTab) {
			// tab bar
			p->setFont(fTSig);
			fm = p->fontMetrics();
			// calculate vertical position:
			// exactly halfway between top and bottom string
			y = ypostb - ysteptb * (trk->string - 1) / 2;
			// center the timesig at this height
			// use spacing of 0.2 * char height
			time.setNum(trk->b[bn].time1);
			brth = fm.boundingRect(time).height();
			y -= (int) (0.1 * brth);
			p->drawText(xpos + tsgpp, y, time);
			time.setNum(trk->b[bn].time2);
			y += (int) (1.2 * brth);
			p->drawText(xpos + tsgpp, y, time);
			p->setFont(fTBar1);
		}
		if (stNts || stTab) {
			xpos += tsgfw;
		}
	}

	// space before first note
	xpos += nt0fw;

	// init expandable space left for space distribution calculation
	int barExpWidthLeft = barExpWidth(bn, trk);

	// loop t over all columns in this bar and calculate beams
	for (uint t = trk->b[bn].start; (int) t <= trk->lastColumn(bn); t++) {
		stl[t].bp.setX(0);
		stl[t].bp.setY(0);
		stl[t].l1 = beamL1(t, 0,bn, trk);
		stl[t].l2 = beamL2plus(t, 0, bn, 2, trk);
		stl[t].l3 = beamL2plus(t, 0, bn, 3, trk);
		stu[t].bp.setX(0);
		stu[t].bp.setY(0);
		stu[t].l1 = beamL1(t, 1,bn, trk);
		stu[t].l2 = beamL2plus(t, 1, bn, 2, trk);
		stu[t].l3 = beamL2plus(t, 1, bn, 3, trk);
	}

	// loop t over all columns in this bar and print them
	for (uint t = trk->b[bn].start; (int) t <= trk->lastColumn(bn); t++) {

		// LVIFIX: indentation
		if (stTab) {

		// Drawing duration marks
		// Draw connection with previous, if applicable
		if ((t > 0) && (t > (unsigned) curt->b[bn].start)
					&& (curt->c[t-1].l == curt->c[t].l))
			xdelta = lastxpos;
		else
			xdelta = xpos + ysteptb / 2;

		p->setPen(pLnBl);
		switch (curt->c[t].l) {
		case 15:  // 1/32
			p->drawLine(xpos,   (int) (ypostb + 1.6 * ysteptb),
						xdelta, (int) (ypostb + 1.6 * ysteptb));
		case 30:  // 1/16
			p->drawLine(xpos,   (int) (ypostb + 1.8 * ysteptb),
						xdelta, (int) (ypostb + 1.8 * ysteptb));
		case 60:  // 1/8
			p->drawLine(xpos,   ypostb + 2 * ysteptb,
						xdelta, ypostb + 2 * ysteptb);
		case 120: // 1/4 - a long vertical line, so we need to find the highest note
			for (i = s;((i >= 0) && (curt->c[t].a[i] == -1)); i--);

			// If it's an empty measure at all - draw the vertical line from bottom
			if (i < 0)  i = 1;

			p->drawLine(xpos, ypostb - i * ysteptb + ysteptb / 2,
						xpos, ypostb + 2 * ysteptb);
			break;		// required to prevent print preview artefact
		case 240: // 1/2
			p->drawLine(xpos, ypostb + 1 * ysteptb,
						xpos, ypostb + 2 * ysteptb);
		case 480: // whole
			break;
		} // end switch (curt->c[t].l)

		// Draw dot is not here, see: "Draw the number column"

		// Length of interval to next column - adjusted if dotted
		// calculated here because it is required by triplet code

		xdelta = colWidth(t, trk);

		// Draw triplet - GREYFIX: ugly code, needs to be fixed
		// somehow... Ideally, triplets should be drawn in a second
		// loop, after everything else would be done.

		if (curt->c[t].flags & FLAG_TRIPLET) {
 			if ((curt->c.size() >= t + 1) && (t) &&
 				(curt->c[t - 1].flags & FLAG_TRIPLET) &&
 				(curt->c[t + 1].flags & FLAG_TRIPLET) &&
				(curt->c[t - 1].l == curt->c[t].l) &&
				(curt->c[t + 1].l == curt->c[t].l)) {
				p->setFont(fTBar2);
				drawStrCntAt(xpos, -3, "3");
				p->setFont(fTBar1);
				extSpAftNote = (colWidth(t, trk) * es) / barExpWidthLeft;
				p->drawLine(xpos + xdelta + extSpAftNote,
							(int) (ypostb + 2.3 * ysteptb),
							xpos + xdelta + extSpAftNote,
							(int) (ypostb + 2.5 * ysteptb));
				p->drawLine(xpos + xdelta + extSpAftNote,
							(int) (ypostb + 2.5 * ysteptb),
							lastxpos,
							(int) (ypostb + 2.5 * ysteptb));
				p->drawLine(lastxpos,
							(int) (ypostb + 2.3 * ysteptb),
							lastxpos,
							(int) (ypostb + 2.5 * ysteptb));
 			} else {
				if (!(((curt->c.size() >= t + 2) &&
					   (curt->c[t + 1].flags & FLAG_TRIPLET) &&
					   (curt->c[t + 2].flags & FLAG_TRIPLET) &&
					   (curt->c[t + 1].l == curt->c[t].l) &&
					   (curt->c[t + 2].l == curt->c[t].l)) ||
					  ((t >= 2) &&
					   (curt->c[t - 1].flags & FLAG_TRIPLET) &&
					   (curt->c[t - 2].flags & FLAG_TRIPLET) &&
					   (curt->c[t - 1].l == curt->c[t].l) &&
					   (curt->c[t - 2].l == curt->c[t].l)))) {
					p->setFont(fTBar2);
					drawStrCntAt(xpos, -3, "3");
					p->setFont(fTBar1);
				}
			}
		}

		// Draw arcs to backward note

		if (curt->c[t].flags & FLAG_ARC)
			p->drawArc(lastxpos, ypostb + 2 * ysteptb + 1,
					   xpos - lastxpos, ysteptb / 2 + 1, 0, -180 * 16);

		// Draw palm muting

		/* moved to "draw effects" ...
		if (curt->c[t].flags & FLAG_PM) {
			p->setFont(fTBar2);
			QString pm = "PM";
			drawStrCntAt(xpos, trk->string, pm);
			p->setFont(fTBar1);
		}
		*/

		} // end if (stTab ...

		// start drawing notes

		if (stNts) {
	
			// print notes
			int ln = 0;				// line where note is printed
			int nhPrinted = 0;		// # note heads printed
			int yl = 0;				// ypos (line) lowest note head
			int yh = 0;				// ypos (line) highest note head
			// cout << "SongPrint::drawBar() draw column" << endl;
			for (int i = 0; i < 2; i++) {
				int dt;
				int tp;
				bool tr;
				bool res;
				res = trk->getNoteTypeAndDots(t, i, tp, dt, tr);
				/*
				cout
					<< "getNoteTypeAndDots()"
					<< " i=" << i
					<< " res=" << res
					<< " tp=" << tp
					<< " dt=" << dt
					<< endl;
				*/
			}
			for (int i = 0; i < 2; i++) {
				bool res;
				res = findHiLo(t, i, trk, yh, yl);
				/*
				cout
					<< "findHiLo()"
					<< " i=" << i
					<< " res=" << res
					<< " yh=" << yh
					<< " yl=" << yl
					<< endl;
				*/
			}
			int dt;
			bool res1;
			bool res2;
			int tp;
			bool tr;
			// print voice 0
			res1 = trk->getNoteTypeAndDots(t, 0, tp, dt, tr);
			res2 = findHiLo(t, 0, trk, yh, yl);
			if (res1 && res2) {
				// voice 0 found
				for (int i = 0; i < trk->string; i++) {
					if ((trk->c[t].a[i] > -1) && (trk->c[t].v[i] == 0)) {
						ln = line((QChar) trk->c[t].stp[i], trk->c[t].oct[i]);
						drawNtHdCntAt(xpos, ln, tp, trk->c[t].acc[i]);
						nhPrinted++;
						// Draw dot, must be at odd line -> set lsbit
						// LVIFIX: add support for double dot
						if (dt) {
							QString s;
							s = QChar(0xA7);
							int y = ln | 1;
							p->setFont(fFeta);
							p->drawText((int) (xpos + 0.8 * wNote),
										yposst - ystepst * y / 2, s);
						}
					}
				}
				if (stl[t].l1 != 'n') {
					// note is beamed, don't draw lower stem and flag
					drawNtStmCntAt(xpos, yl, yh, 0, 'd');
					// remember position
					stl[t].bp.setX((int) (xpos - 0.45 * wNote));
					int yhd = yposst - (int) (ystepst * ((-0.4 + yl) / 2));
					stl[t].bp.setY(yhd);
				} else {
					drawNtStmCntAt(xpos, yl, yh, tp, 'd');
				}
			}
			// print voice 1
			res1 = trk->getNoteTypeAndDots(t, 1, tp, dt, tr);
			res2 = findHiLo(t, 1, trk, yh, yl);
			if (res1 && res2) {
				// voice 1 found
				for (int i = 0; i < trk->string; i++) {
					if ((trk->c[t].a[i] > -1) && (trk->c[t].v[i] == 1)) {
						ln = line((QChar) trk->c[t].stp[i], trk->c[t].oct[i]);
						drawNtHdCntAt(xpos, ln, tp, trk->c[t].acc[i]);
						nhPrinted++;
						// Draw dot, must be at odd line -> set lsbit
						// LVIFIX: add support for double dot
						if (dt) {
							QString s;
							s = QChar(0xA7);
							int y = ln | 1;
							p->setFont(fFeta);
							p->drawText((int) (xpos + 0.8 * wNote),
										yposst - ystepst * y / 2, s);
						}
					}
				}
				if (stu[t].l1 != 'n') {
					// note is beamed, don't draw upper stem and flag
					drawNtStmCntAt(xpos, yl, yh, 0, 'u');
					// remember position
					stu[t].bp.setX((int) (xpos + 0.45 * wNote));
					int yhd = yposst - (int) (ystepst * ((0.4 + yh) / 2));
					stu[t].bp.setY(yhd);
				} else {
					drawNtStmCntAt(xpos, yl, yh, tp, 'u');
				}
			}
			/*
			// original print code
			for (int i = 0; i < trk->string; i++) {
				if (trk->c[t].a[i] > -1) {
					ln = line((QChar) trk->c[t].stp[i], trk->c[t].oct[i]);
					drawNtHdCntAt(xpos, ln, trk->c[t].l, trk->c[t].acc[i]);
					// Draw dot, must be at odd line -> set lsbit
					if (curt->c[t].flags & FLAG_DOT) {
						QString s;
						s = QChar(0xA7);
						int y = ln | 1;
						p->setFont(fFeta);
						p->drawText((int) (xpos + 0.8 * wNote),
									yposst - ystepst * y / 2, s);
					}
					nhPrinted++;
					if (nhPrinted == 1) {
						// first note printed, yl = yh;
						yl = yh = ln;
					} else {
						// more than one note printed, determine lowest/highest
						if (ln < yl) {
							yl = ln;
						}
						if (ln > yh) {
							yh = ln;
						}
					}
				} // end if (trk->c[t].a[i] > -1) {
			} // end for (int i = 0; i < trk->string; i++) {
			*/
	
			// if no note printed, print rest
			if (nhPrinted == 0) {
				drawRstCntAt(xpos, 4, trk->c[t].l);
			}

		} // end if (stNts ...

		// end drawing notes

		// LVIFIX: indentation
		if (stTab) {

		// Draw the number column including effects
		p->setFont(fTBar1);
		int ew_2 = 0;			// used for positioning effects
		QString note = "";
		for (int i = 0; i < trk->string; i++) {
			if (trk->c[t].a[i] != -1) {
				if (curt->c[t].a[i] == DEAD_NOTE)
					note = "X";
				else
					note.setNum(trk->c[t].a[i]);
				// Draw dot
				if (curt->c[t].flags & FLAG_DOT)
					note += ".";
				drawStrCntAt(xpos, i, note);
				// cell width is needed later
				ew_2 = eraWidth(note) / 2;
				if (ringing[i]) {
					drawLetRing(xpos - ew_2, i);
					ringing[i] = FALSE;
				}
			}
			if ((curt->c[t].a[i] == -1)
			     && (curt->c[t].e[i] == EFFECT_STOPRING)) {
				if (ringing[i]) {
					int ew_3 = eraWidth("0") / 4;
					drawLetRing(xpos - ew_3, i);
					ringing[i] = FALSE;
				}
			}

			// Draw effects
			// GREYFIX - use lastxpos, not xdelta

			switch (curt->c[t].e[i]) {
			case EFFECT_HARMONIC:
				{
					QPointArray a(4);
					// size of diamond
					int sz_2 = ysteptb / 4;
					// leftmost point of diamond
					int x = xpos + ew_2;
					int y = ypostb - i * ysteptb;
					// initialize diamond shape
					a.setPoint(0, x,        y     );
					a.setPoint(1, x+sz_2,   y+sz_2);
					a.setPoint(2, x+2*sz_2, y     );
					a.setPoint(3, x+sz_2,   y-sz_2);
					// erase tab line
					p->setPen(pLnWh);
					p->drawLine(x, y, x+2*sz_2, y);
					p->setPen(pLnBl);
					// draw (empty) diamond
					p->drawPolygon(a);
				}
				break;
			case EFFECT_ARTHARM:
				{
					QPointArray a(4);
					// size of diamond
					int sz_2 = ysteptb / 4;
					// leftmost point of diamond
					int x = xpos + ew_2;
					int y = ypostb - i * ysteptb;
					// initialize diamond shape
					a.setPoint(0, x,        y     );
					a.setPoint(1, x+sz_2,   y+sz_2);
					a.setPoint(2, x+2*sz_2, y     );
					a.setPoint(3, x+sz_2,   y-sz_2);
					// draw filled diamond
					QBrush blbr(Qt::black);
					p->setBrush(blbr);
					p->drawPolygon(a);
					p->setBrush(Qt::NoBrush);
				}
				break;
			case EFFECT_LEGATO:
				// draw arc to next note
				// the arc should be as wide as the line between
				// this note and the next. see drawStrCntAt.
				// extra space between notes must also be added
				if ((t < curt->c.size() - 1) && (curt->c[t + 1].a[i] >= 0)) {
					extSpAftNote = (colWidth(t, trk) * es) / barExpWidthLeft;
					p->drawArc(xpos + ew_2, ypostb - i * ysteptb - ysteptb / 2,
							   xdelta + extSpAftNote - 2 * ew_2, ysteptb / 2,
							   0, 180 * 16);
				}
				break;
			case EFFECT_SLIDE:
				// the slide symbol should be as wide as the line
				// between this note and the next. see drawStrCntAt.
				// extra space between notes must also be added
				if ((t < curt->c.size() - 1) && (curt->c[t + 1].a[i] >= 0)) {
					extSpAftNote = (colWidth(t, trk) * es) / barExpWidthLeft;
					if (curt->c[t + 1].a[i] > curt->c[t].a[i]) {
						p->drawLine(xpos + ew_2,
									ypostb - i * ysteptb + ysteptb / 3 - 2,
									xpos + xdelta + extSpAftNote - ew_2,
									ypostb - i * ysteptb - ysteptb / 3 + 2);
					} else {
						p->drawLine(xpos + ew_2,
									ypostb - i * ysteptb - ysteptb / 3 + 2,
									xpos + xdelta + extSpAftNote - ew_2,
									ypostb - i * ysteptb + ysteptb / 3 - 2);
					}
				}
				break;
			case EFFECT_LETRING:
				ringing[i] = TRUE;
				break;
			} // end switch (curt->c[t].e[i])

			// draw palm muting as little cross behind note
			if (curt->c[t].flags & FLAG_PM
				&& trk->c[t].a[i] != -1) {
				int sz_2 = ysteptb / 4;
				int x    = xpos + ew_2;
				int y    = ypostb - i * ysteptb;
				p->drawLine(x, y - sz_2, x + sz_2, y + sz_2);
				p->drawLine(x, y + sz_2, x + sz_2, y - sz_2);
			}
			
		} // end for (int i = 0 ... (end draw the number column ...)

		} // end if (stTab ...
		
		lastxpos = xpos;
		xpos += colWidth(t, trk);

		// calculate and add extra space
		int extSpAftNote = (colWidth(t, trk) * es) / barExpWidthLeft;
		xpos += extSpAftNote;
		es -= extSpAftNote;
		barExpWidthLeft -= colWidth(t, trk);

	} // end for (uint t ... (end loop t over all columns ...)

	// draw beams
	if (stNts) {
		drawBeams(bn, stl, 'd', trk);
		drawBeams(bn, stu, 'u', trk);
	}

	// space after last note
	xpos += ntlfw;

	// end bar
	if (stTab) {
		// show notes still ringing at end of bar
		for (int i = 0; i <= s; i++) {
			if (ringing[i]) {
				int ew_3 = eraWidth("0") / 4;
				drawLetRing(xpos - ew_3, i);
				ringing[i] = FALSE;
			}
		}
		// draw vertical line
		p->drawLine(xpos, ypostb,
		            xpos, ypostb - (trk->string - 1) * ysteptb);
	}
	if (stNts) {
		// draw vertical line
		p->drawLine(xpos, yposst,
		            xpos, yposst - 4 * ystepst);
	}
	// LVIFIX
	xpos += 1;
}

// draw bar lines at xpos,ypostb width w for all strings of track trk

void SongPrint::drawBarLns(int w, TabTrack *trk)
{
	const int lstStr = trk->string - 1;
	// vertical lines at xpos and xpos+w-1
	p->setPen(pLnBl);
	p->drawLine(xpos, ypostb, xpos, ypostb - lstStr * ysteptb);
	p->drawLine(xpos + w - 1, ypostb, xpos + w - 1, ypostb - lstStr * ysteptb);
	// horizontal lines from xpos to xpos+w-1
	for (int i = 0; i < lstStr+1; i++) {
		p->drawLine(xpos, ypostb - i * ysteptb,
					xpos + w - 1, ypostb - i * ysteptb);
	}
}

void SongPrint::drawBeam(int x1, int x2, int y, char tp, char dir)
{
	int yh;
	int yl;
	if (dir != 'd') {
		yh = y;
		yl = y - (int) (0.4 * ystepst);
	} else {
		yh = y + (int) (0.4 * ystepst);
		yl = y;
	}
	QPointArray a;
	QBrush brush(Qt::black, Qt::SolidPattern);
	p->setBrush(brush);
	switch (tp) {
	case 'b':
		x2 = x1;
		x1 = x1 - (int) (0.6 * ystepst);
		break;
	case 'f':
		x2 = x1 + (int) (0.6 * ystepst);
		break;
	case 'c':
	case 's':
		// nothing to be done for 'c' and 's'
		break;
	default:
		return;
	}
	a.setPoints(4,
		x1, yh,
		x2, yh,
		x2, yl,
		x1, yl
	);
	p->drawPolygon(a);
}

// draw beams of bar bn, all other info to be found in StemInfo std/stu

void SongPrint::drawBeams(int bn, QMemArray<StemInfo> & stx, char dir,
							TabTrack *trk)
{
	// cout << "SongPrint::drawBeams(" << bn << ")" << endl;
	for (uint t = trk->b[bn].start; (int) t <= trk->lastColumn(bn); t++) {
		/*
		cout
			<< "t=" << t
			<< " l1..3=" << stx[t].l1 << stx[t].l2 << stx[t].l3 << endl;
		*/
	}
	int yextr = 0;
	for (uint t = trk->b[bn].start; (int) t <= trk->lastColumn(bn); t++) {
		if (stx[t].l1 == 's') {
			// determine beam height: depends on highest/lowest note
			// LVIFIX: support angled beams
			uint i = t;
			yextr = stx[i].bp.y();
			i++;
			while ((int) i <= trk->lastColumn(bn)) {
				if (dir != 'd') {
					if (stx[i].bp.y() < yextr) {
						yextr = stx[i].bp.y();
					}
				} else {
					if (stx[i].bp.y() > yextr) {
						yextr = stx[i].bp.y();
					}
				}
				if (stx[i].l1 == 'e') {
					break;
				}
				i++;
			}
		}
		if (stx[t].l1 != 'n') {
			// draw stem
			int x1 = stx[t].bp.x();
			int x2 = 0;
			if ((int) t < trk->lastColumn(bn)) {
				x2 = stx[t+1].bp.x();
			}
			int ydir;
			int yh;
			int yl;
			if (dir != 'd') {
				ydir = 1;
				yh = yextr - ydir * (int) (3.5 * ystepst);
				yl = stx[t].bp.y();
			} else {
				ydir = -1;
				yh = stx[t].bp.y();
				yl = yextr - ydir * (int) (3.5 * ystepst);
			}
			p->setPen(pLnBl);
			p->drawLine(x1, yl, x1, yh);
			// draw beams
			if (dir != 'd') {
				drawBeam(x1, x2, yh, stx[t].l1, dir);
				yh = yh + (int) (0.8 * ystepst);
				drawBeam(x1, x2, yh, stx[t].l2, dir);
				yh = yh + (int) (0.8 * ystepst);
				drawBeam(x1, x2, yh, stx[t].l3, dir);
			} else {
				drawBeam(x1, x2, yl, stx[t].l1, dir);
				yl = yl - (int) (0.8 * ystepst);
				drawBeam(x1, x2, yl, stx[t].l2, dir);
				yl = yl - (int) (0.8 * ystepst);
				drawBeam(x1, x2, yl, stx[t].l3, dir);
			}
		}
	}
}

// draw clef at xpos,yposst
// draw key at xpos,ypostb for all strings of track trk
// at the first line (l == 0), string names are printed
// at all other lines the text "TAB"
// note: print drum names instead in case of drumtrack

void SongPrint::drawKey(int l, TabTrack *trk)
{
	if (stNts) {
		// draw clef
		QString s;
		s = QChar(0x6A);
		p->setFont(fFeta);
		// LVIFIX: determine correct location (both clef and key)
		p->drawText(xpos + tabpp, yposst - ystepst, s);
	}

	if (stTab) {
		p->setFont(fTBar1);
		const int lstStr = trk->string - 1;
		if (l == 0) {
			for (int i = 0; i < lstStr + 1; i++) {
				if (trk->trackMode() == DrumTab) {
					drawStrCntAt(xpos + tabpp + 3 * br8w / 2,
								 i,
								 drum_abbr[trk->tune[i]]);
				} else {
					drawStrCntAt(xpos + tabpp + br8w / 2,
								 i,
								 note_name(trk->tune[i] % 12));
				}
			}
		} else {
			// calculate vertical position:
			// exactly halfway between top and bottom string
			// center "TAB" at this height, use spacing of 0.25 * char height
			QFontMetrics fm  = p->fontMetrics();
			int y = ypostb - ysteptb * lstStr / 2;
			int br8h = fm.boundingRect("8").height();
			y -= (int) ((0.5 + 0.25) * br8h);
			p->drawText(xpos + tabpp, y, "T");
			y += (int) ((1.0 + 0.25) * br8h);
			p->drawText(xpos + tabpp, y, "A");
			y += (int) ((1.0 + 0.25) * br8h);
			p->drawText(xpos + tabpp, y, "B");
		}
	}
}

// draw "let ring" with point of arrowhead at x on string y
// LVIFIX: use xpos too ?

void SongPrint::drawLetRing(int x, int y)
{
	p->drawLine(x,               ypostb - y * ysteptb,
				x - ysteptb / 3, ypostb - y * ysteptb - ysteptb / 3);
	p->drawLine(x,               ypostb - y * ysteptb,
				x - ysteptb / 3, ypostb - y * ysteptb + ysteptb / 3);
}

// draw notehead of type t centered at x on staff line y
// note: lowest = 0, highest = 8
// uses yposst but ignores xpos
// LVIFIX: use xpos too ?

// LVIFIX: move 1/2 note head "a little bit" to the left

void SongPrint::drawNtHdCntAt(int x, int y, int t, Accidentals::Accid a)
{
	// draw auxiliary lines
	int xdl = (int) (0.8 * wNote);	// x delta left of origin
	int xdr = (int) (0.8 * wNote);	// x delta right of origin
	p->setPen(pLnBl);
	int auxLine = y / 2;
	while (auxLine < 0) {
		p->drawLine(x - xdl, yposst - auxLine * ystepst,
		            x + xdr, yposst - auxLine * ystepst);
		auxLine++;
	}
	while (auxLine > 4) {
		p->drawLine(x - xdl, yposst - auxLine * ystepst,
		            x + xdr, yposst - auxLine * ystepst);
		auxLine--;
	}
	// draw note head
	int noteHead = 0;
	if (t == 480) {
		// whole
		noteHead = 0x22;
	} else if (t == 240) {
		// 1/2
		noteHead = 0x23;
	} else {
		// others
		noteHead = 0x24;
	}
	QString s;
	s = QChar(noteHead);
	p->setFont(fFeta);
	p->drawText(x - wNote / 2, yposst - ystepst * y / 2, s);
	// draw accidentals
	int acc = 0;
	if (a == Accidentals::Sharp) {
		acc = 0x201c;
	} else if (a == Accidentals::Flat) {
		acc = 0x201e;
	} else if (a == Accidentals::Natural) {
		acc = 0x201d;
	}
	s = QChar(acc);
	// LVIFIX: optimize position accidental
	p->drawText((int) (x - 1.4 * wNote), yposst - ystepst * y / 2, s);
}

// draw notestem and flag of type t and direction dir centered at x
// for notes on staff lines yl .. yh
// note: lowest = 0, highest = 8
// uses yposst but ignores xpos
// if t==0, draws only notestem between notes
// LVIFIX: use xpos too ?

// LVIFIX: lower stem doesn't touch upper stem
// LVIFIX: draw stem "a little bit" more to the left

void SongPrint::drawNtStmCntAt(int x, int yl, int yh, int t, char dir)
{
	int flagCh = 0;
	int w = 0;
	int yoffset = 0;						// y offset flags
	switch (t) {
	case 0:   // none
		break;
	case 15:  // 1/32
		flagCh = (dir != 'd') ? 0x5C :   0x61;
		yoffset = (int) (-1.3 * ystepst);
		break;
	case 30:  // 1/16
		flagCh = (dir != 'd') ? 0x5B : 0x2018;
		yoffset = (int) (-0.5 * ystepst);
		break;
	case 60:  // 1/8
		flagCh = (dir != 'd') ? 0x5A :   0x5F;
		break;
	case 120: // 1/4
		break;
	case 240: // 1/2
		break;
	case 480: // whole
		return;
	default:
		; // do nothing
	} // end switch (t)
	p->setPen(pLnBl);
	// draw stem (lower part)
	int xs;
	if (dir != 'd') {
		xs = (int) (x + 0.45 * wNote);		// x pos stem
	} else {
		xs = (int) (x - 0.45 * wNote);		// x pos stem
	}
	if (yl != yh) {
		int yld = yposst - (int) (ystepst * ((0.2 + yl) / 2));
		int yhd = yposst - (int) (ystepst * ((0.4 + yh) / 2));
		p->drawLine(xs, yld,
					xs, yhd);
	}
	if (dir != 'd') {
		// up
		if (t != 0) {
			QString s;
			// draw stem (upper part)
			s = QChar(0x64);
			p->drawText(xs, yposst - ystepst * yh / 2, s);
			// draw flag(s)
			s = QChar(flagCh);
			int yFlag = yposst - ystepst * yh / 2
						- (int) (3.5 * ystepst)
						+ yoffset;
			p->drawText(xs, yFlag, s);
		}
	} else {
		// down
		if (t != 0) {
			QString s;
			// draw stem (lower part)
			s = QChar(0x65);
			p->drawText(xs, yposst - ystepst * yl / 2, s);
			// draw flag(s)
			s = QChar(flagCh);
			int yFlag = yposst - ystepst * yl / 2
						+ (int) (3.5 * ystepst)
						+ yoffset;
			p->drawText(xs, yFlag, s);
		}
	}
}

// draw header of song song, page n

void SongPrint::drawPageHdr(int n, TabSong *song)
{
	p->setFont(fHdr1);
	p->drawText(0, hdrh1, song->title + " - " + song->author);
	QString pgNr;
	pgNr.setNum(n);
	QFontMetrics fm  = p->fontMetrics();
	int brnw = fm.boundingRect(pgNr).width();
	p->setFont(fHdr2);
	p->drawText(pprw - brnw, hdrh1, pgNr);
	p->setFont(fHdr3);
	p->drawText(0, hdrh1 + hdrh2, "Transcribed by " + song->transcriber);
	ypostb = hdrh1 + hdrh2 + hdrh3;
}

// draw rest of type t centered at x on staff line y
// note: lowest = 0, highest = 8
// uses yposst but ignores xpos
// LVIFIX: use xpos too ?

void SongPrint::drawRstCntAt(int x, int y, int t)
{
	int restSym = 0;
	int yoffset = 0;
	switch (t) {
	case 15:  // 1/32
		restSym = 0x02D9;
		break;
	case 30:  // 1/16
		restSym = 0xAF;
		break;
	case 60:  // 1/8
		restSym = 0x02D8;
		break;
	case 120: // 1/4
		restSym = 0x02C7;
		break;
	case 240: // 1/2
		restSym = 0xB4;
		break;
	case 480: // whole
		restSym = 0x60;
		yoffset = 2;
		break;
	default:
		return; // do nothing
	} // end switch (t)
	QString s;
	s = QChar(restSym);
	p->setFont(fFeta);
	p->drawText(x - wNote / 2, yposst - ystepst * (y + yoffset) / 2, s);
}

// draw staff lines at xpos,yposst width w

void SongPrint::drawStLns(int w)
{
	const int lstStL = 4;
	// vertical lines at xpos and xpos+w-1
	p->setPen(pLnBl);
	p->drawLine(xpos, yposst,
				xpos, yposst - lstStL * ystepst);
	p->drawLine(xpos + w - 1, yposst,
				xpos + w - 1, yposst - lstStL * ystepst);
	// horizontal lines from xpos to xpos+w-1
	for (int i = 0; i < lstStL+1; i++) {
		p->drawLine(xpos, yposst - i * ystepst,
					xpos + w - 1, yposst - i * ystepst);
	}
	if (stTab) {
		p->drawLine(xpos, yposst,
					xpos, yposst + (7 + 3) * ystepst);
		p->drawLine(xpos + w - 1, yposst,
					xpos + w - 1, yposst + (7 + 3) * ystepst);
	}
}

// draw string s centered at x on string n
// erase both tab and possible vertical line at location of string
// uses ypostb but ignores xpos
// LVIFIX: use xpos too ?

// As most characters don't start at the basepoint, we need to center
// the bounding rectangle, i.e. offset the character in the x direction
// by (left + right) / 2.
// Strictly speaking this needs to be done in the y dir too, but here
// the error is very small.

void SongPrint::drawStrCntAt(int x, int n, const QString s)
{
	QFontMetrics fm = p->fontMetrics();
	const int yOffs = fm.boundingRect("8").height() / 2;
	const QRect r   = fm.boundingRect(s);
	int xoffs       = - (r.left() + r.right()) / 2;
	p->setPen(pLnWh);
	int ew_2 = eraWidth(s) / 2;
	p->drawLine(x - ew_2, ypostb - n * ysteptb,
				x + ew_2, ypostb - n * ysteptb);
	p->drawLine(x, ypostb - n * ysteptb - ysteptb / 2,
				x, ypostb - n * ysteptb + ysteptb / 2);
	p->setPen(pLnBl);
	p->drawText(x + xoffs, ypostb - n * ysteptb + yOffs, s);
}

// return width (of barline) to erase for string s

int SongPrint::eraWidth(const QString s)
{
	QFontMetrics fm = p->fontMetrics();
	const int brw8  = fm.boundingRect("8").width();
	const int brws  = fm.boundingRect(s).width();
	return (int) (brws + 0.4 * brw8);
}

// helper function to initialize font and check exactly this font was found
// return true on succes

// LVIFIX: according to the documentation, the following should
// supply the font family actually used.
// p->setFont(fFeta);
// QFontInfo info = p->fontInfo();
// QString actualFamily = info.family();
// Unfortunately, instead it returns the font family requested.
// This prevents checking if the correct font is used.
// Workaround is to use the rawName, which starts with
// "<family>-<pointsize>:"
// Another issue is that font "TeX feta-nummer10" cannot be found.
// Tested on RedHat 8.0 with Qt 3.0.5.

static bool initExactFont(const QString fn, int fs, QFont & fnt)
{
	QString an;					// actual name
	QString rn;					// requested name

	rn.setNum(fs);
	rn = fn + "-" + rn;
	fnt = QFont(fn, fs);
	an = fnt.rawName().section(':', 0, 0);
	// LVIFIX: debug
	// cout << "rn='" << rn << "'" << endl;
	// cout << "an='" << an << "'" << endl;
	return (rn == an);
}

// find line of highest/lowest note in column cl for voice v in tabtrack trk
// returns false if not found
// precondition: calcStepAltOct() and calcVoices() must have been called

bool SongPrint::findHiLo(int cl, int v, TabTrack *trk, int & hi, int & lo)
{
	bool found = false;
	hi = 0;						// prevent uninitialized variable
	lo = 0;						// prevent uninitialized variable
	// loop over all strings
	/*
	cout << "v=" << v;
	*/
	for (int i = 0; i < trk->string; i++) {
	/*
		cout
			<< " i=" << i
			<< " v[i]=" << (int) trk->c[cl].v[i]
			<< endl;
	*/
		if (trk->c[cl].v[i] == v) {
			int ln = line((QChar) trk->c[cl].stp[i], trk->c[cl].oct[i]);
			if (found) {
				// found note in this voice, but not the first
				if (ln < lo) lo = ln;
				if (ln > hi) hi = ln;
			} else {
				// found first note in this voice
				lo = ln;
				hi = ln;
			}
			found = true;
		}
	}
	return found;
}

// initialize fonts

void SongPrint::initFonts()
{
	fHdr1  = QFont("Helvetica", 12, QFont::Bold);
	fHdr2  = QFont("Helvetica", 10, QFont::Normal);
	fHdr3  = QFont("Helvetica",  8, QFont::Normal);
	fTBar1 = QFont("Helvetica",  8, QFont::Bold);
	fTBar2 = QFont("Helvetica",  7, QFont::Normal);
	fTSig  = QFont("Helvetica", 12, QFont::Bold);

	// following fonts must be found: if not, printing of notes is disabled
	QString fn;
	fFetaFnd = true;
	fn = "TeX feta19";
	if (!initExactFont(fn, 18, fFeta)) {
		cout << "KGuitar: could not find font '" << fn << "'" << endl;
		fFetaFnd = false;
	}
	fn = "TeX feta-nummer10";
	if (!initExactFont(fn, 10, fFetaNr)) {
		// LVIFIX: font is not found (don't know why), but acceptable
		// replacement is, therefore suppress error
		// cout << "KGuitar: could not find font '" << fn << "'" << endl;
		// fFetaFnd = false;
	}
}

// initialize paper format and font dependent metrics

void SongPrint::initMetrics(KPrinter *printer)
{
	// determine width/height of printer surface
	QPaintDeviceMetrics pdm(printer);
	pprh  = pdm.height();
	pprw  = pdm.width();
	// determine font-dependent bar metrics
	p->setFont(fTBar1);
	QFontMetrics fm  = p->fontMetrics();
	br8h = fm.boundingRect("8").height();
	br8w = fm.boundingRect("8").width();
	ysteptb = (int) (0.9 * fm.ascent());
	tabfw = 4 * br8w;
	tabpp =     br8w;
	tsgfw = 5 * br8w;
	tsgpp = 2 * br8w;
	nt0fw = 2 * br8w;
	ntlfw =     br8w / 2;
	// determine font-dependent page header metrics
	p->setFont(fHdr1);
	fm  = p->fontMetrics();
	hdrh1 = fm.ascent();
	p->setFont(fHdr3);
	fm  = p->fontMetrics();
	hdrh2 = (int) (1.5 *fm.height());
	hdrh3 = 2 * ysteptb;
	p->setFont(fHdr2);
	fm  = p->fontMetrics();
	hdrh4 = 2 * fm.height();
	// determine font-dependent staff metrics
	QChar c;
	QRect r;
	p->setFont(fFeta);
	fm  = p->fontMetrics();
	c = 0x24;
	r = fm.boundingRect(c);
	ystepst = (int) (0.95 * r.height());
	wNote = r.width();
}

// initialize pens
// LVIFIX: which penwidth ?
// penwidth 2 is OK on my deskjet for printing quality = normal
// penwidth 3 is OK on my deskjet for printing quality = presentation

void SongPrint::initPens()
{
	const int lw = 2;
	pLnBl = QPen(Qt::black, lw);
	pLnWh = QPen(Qt::white, lw);
}

// init printing style variables

void SongPrint::initPrStyle()
{
	// check wat was configured
	switch (globalPrSty) {
	case 0:
		// (full) tab only
		stNts = false;
		stTab = true;
		break;
	case 1:
		// notes
		stNts = true;
		stTab = false;
		break;
	case 2:
		// notes + (full) tab
		stNts = true;
		stTab = true;
		break;
	case 3:
		// notes + (minimum) tab
		// not implemented yet, fall through to default
		// break;
	default:
		stNts = false;
		stTab = true;
	}
	// no notes if feta fonts not found
	if (!fFetaFnd) {
		stNts = false;
	}
}

// return staffline where note must be drawn (lowest = 0, highest = 8)

int SongPrint::line(const QString step, int oct)
{
	int cn = 0;				// if note not found, default to "C"
	for (int i = 0; i < 7; i++) {
		if (notes[i] == step) {
			cn = i;
		}
	}
	// magic constant "30" maps G4 to the second-lowest staffline
	return cn + 7 * oct - 30;
}

// print song song on printer printer

void SongPrint::printSong(KPrinter *printer, TabSong *song)
{
	// start painting on printer
	if (!p->begin(printer))
		return;

	// initialize fonts, must be done before initMetrics
	// (metrics depend on fonts)
	initFonts();

	// initialize metrics
	initMetrics(printer);

	// initialize pens
	initPens();
	p->setPen(pLnBl);

	// init printing style variables
	initPrStyle();

	// print page header
	int pgNr = 1;
	drawPageHdr(pgNr, song);

	// ypostb now is where either the empty space above the staff,
	// the top barline of the first tab bar, or the top of the track name
	// should be
	
	uint trkPr = 0;				// tracks printed

	// loop while tracks left in the song
	while (trkPr < (song->t).count()) {

		TabTrack *trk = (song->t).at(trkPr);

		// LVIFIX: may fail on out of memory
		(void) stl.resize(trk->c.size());
		(void) stu.resize(trk->c.size());

		// Determine voices for each note
		trk->calcVoices();

		// Determine step/alter/octave/accidental for each note
		trk->calcStepAltOct();

	// LVIFIX: start debug only, remove
	/*
	{
		cout << "SongPrint::printSong()" << endl;
		uint bn = 0;						// Drawing only this bar
		int s = trk->string - 1;
		for (int t = 0; t < trk->c.size(); t++) {
			cout << "t=" << t;
			cout << " b=" << trk->barNr(t);
			cout << " l=" << trk->c[t].l;
			cout << " a[i]=";
			for (int i=0; i<=s; i++) cout << (int) trk->c[t].a[i] << " ";
			cout << "e[i]=";
			for (int i=0; i<=s; i++) cout << (int) trk->c[t].e[i] << " ";
			cout << "ncols[i]=";
			for (int i=0; i<=s; i++)
				cout << trk->noteNrCols(t, i) << " ";
			cout << "l[i]=";
			for (int i=0; i<=s; i++)
				cout << trk->noteDuration(t, i) << " ";
			cout << " v[i]=";
			for (int i=0; i<=s; i++) cout << (int) trk->c[t].v[i] << " ";
			cout << "flags=" << trk->c[t].flags;
			cout << endl;
		}
	}
	*/
	// LVIFIX: end debug only, remove

		// print the track header
		if ((song->t).count() > 1)
		{
			p->setFont(fHdr2);
			QFontMetrics fm  = p->fontMetrics();
			p->drawText(0, ypostb + fm.ascent(), trk->name);
			ypostb += hdrh4;
		}

		int l = 0;					// line nr in the current track
		uint brsPr = 0;				// bars printed
		int bn = 0;					// current bar nr

		// precalculate bar widths
		QMemArray<int> bew(trk->b.size());
		QMemArray<int> bw(trk->b.size());
		for (uint bn = 0; bn < trk->b.size(); bn++) {
			bew[bn] = barExpWidth(bn, trk);
			bw[bn]  = barWidth(bn, trk);
		}

		// loop while bars left in the track
		while (brsPr < trk->b.size()) {

			if (stNts) {
				// move yposst to the bottom staff line
				// i.e. move down (7 + 4) staff linesteps
				yposst = ypostb + (7 + 4) * ystepst;
				xpos = 0;
				// draw empty staff at xPos, yPosst
				drawStLns(pprw - 1);
				if (stTab) {
					// move ypostb to the top bar line
					// (where it would be if there as no staff)
					// i.e. move down 7 (undershoot) + 3 (linefeed)
					// staff linesteps
					ypostb = yposst + (7 + 3) * ystepst;
				}
			} else {
				yposst = ypostb;
			}
			if (stTab) {
				// move ypostb to the bottom bar line
				// i.e. move down (nstrings - 1) * tab bar linesteps
				ypostb = ypostb + (trk->string - 1) * ysteptb;
				xpos = 0;
				// draw empty tab bar at xPos, yPos
				drawBarLns(pprw - 1, trk);
			}

			xpos += 1;				// LVIFIX: first vertical line
			drawKey(l, trk);
			xpos += tabfw;			// "TAB"

			// determine # bars fitting on this line
			// must be at least 1 (very long bar will be truncated)
			uint nBarsOnLine = 1;
			int totWidth = bw[bn];
			// while bars left and next bar also fits
			while (bn + nBarsOnLine < trk->b.size()
				   && totWidth + bw[bn + nBarsOnLine] < pprw - xpos) {
				totWidth += bw[bn + nBarsOnLine];
				nBarsOnLine++;
			}

			// print without extra space on last line,
			// with extra space on all others
			if (bn + nBarsOnLine >= trk->b.size()) {
				// last line, no extra space
				for (uint i = 0; i < nBarsOnLine; i++) {
					drawBar(bn, trk, 0);
					bn++;
				}
			} else {
				// not the last line, add extra space
				// calculate extra space left to distribute divided by bars left,
				// to prevent accumulation of round-off errors
				int extSpLeft = pprw - xpos - totWidth - 1;
				for (uint i = 0; i < nBarsOnLine; i++) {
					int extSpInBar = extSpLeft / (nBarsOnLine - i);
					drawBar(bn, trk, extSpInBar);
					extSpLeft -= extSpInBar;
					bn++;
				}
			}
		
			brsPr += nBarsOnLine;

			// move to top of next line
			// tab, when printed, is below staff and determines the linefeed
			// required: 3 ysteptb (undershoot) + 2 ysteptb (linefeed)
			// if no tab, assume only staff:
			// required: 7 ystepst (undershoot) + 3 ystepst (linefeed)
			l++;
			if (stTab) {
				ypostb += (3 + 2) * ysteptb;
			} else {
				ypostb += (7 + 3) * ysteptb;
			}

			// determine height required for next line
			// LVIFIX: correct for track name when necessary
			int yreq = 0;
			if (stNts) {
				// add staff height
				yreq += (7 + 4 + 7) * ystepst;
			}
			if (stTab) {
				// add tab bar height
				yreq += (trk->string - 1 + 3) * ysteptb;
			}
			if (stNts && stTab) {
				// add staff to tab bar spacing
				yreq += 3 * ystepst;
			}

			// move to next page if necessary
			if (ypostb + yreq > pprh) {
				printer->newPage();
				pgNr++;
				drawPageHdr(pgNr, song);
				// print the track header
				if ((song->t).count() > 1)
				{
					p->setFont(fHdr2);
					QFontMetrics fm  = p->fontMetrics();
					p->drawText(0, ypostb + fm.ascent(), trk->name);
					ypostb += hdrh4;
				}
			}

		} // end while (brsPr ...

		// move to the next track
		trkPr++;

	} // end while (trkPr ...
			
	p->end();			// send job to printer
}

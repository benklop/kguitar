#include "fingerlist.h"
#include "global.h"

#include <qpainter.h>

FingerList::FingerList(QWidget *parent,const char *name): QTableView(parent,name)
{
  setTableFlags(Tbl_autoVScrollBar | Tbl_smoothScrolling);
  setFrameStyle(Panel | Sunken);
  num=0;
  setCellWidth(ICONCHORD);
  setCellHeight(ICONCHORD);

  repaint();
}

void FingerList::addFingering(const int a[MAX_STRINGS])
{
  for (int i=0;i<MAX_STRINGS;i++)
    appl[num][i]=a[i];
  num++;
  // num is overral number of chord applicatures. If it's 0 - then there are no applicatures.
  // In the appl array, indexes should be ranged from 0 to (num-1)
}

void FingerList::resizeEvent(QResizeEvent *e)
{
  perRow = width()/ICONCHORD;
  setNumCols(perRow);
  setNumRows(num/perRow+1);
}

void FingerList::paintCell(QPainter *p, int row, int col)
{
  int n = row*perRow+col;

  p->drawRect(0,0,ICONCHORD-1,ICONCHORD-1);

  if (n<num) {
    p->drawLine(0,0,ICONCHORD-1,ICONCHORD-1);
    p->drawLine(ICONCHORD-1,0,0,ICONCHORD-1);
  }
}

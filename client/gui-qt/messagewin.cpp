/**********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// gui-qt
#include "fc_client.h"
#include "qtg_cxxside.h"

#include "messagewin.h"

/**************************************************************************
  Display the message dialog.  Optionally raise it.
  Typically triggered by F10.
**************************************************************************/
void meswin_dialog_popup(bool raise)
{
  /* PORTME */
}

/**************************************************************************
  Return whether the message dialog is open.
**************************************************************************/
bool meswin_dialog_is_open(void)
{
  /* PORTME */
  return TRUE;
}

/**************************************************************************
  Do the work of updating (populating) the message dialog.
**************************************************************************/
void real_meswin_dialog_update(void)
{
  int  i, num;
  const struct message *pmsg;

  gui()->messages_window->clearContents();
  gui()->messages_window->setRowCount(0);

  num = meswin_get_num_messages();

  for (i = 0; i < num; i++) {
    QTableWidgetItem *item;
    item = new QTableWidgetItem();
    pmsg = meswin_get_message(i);
    gui()->messages_window->insertRow(i);
    item->setText(QString::fromUtf8(pmsg->descr));
    gui()->messages_window->setItem(i, 0, item);
  }

  gui()->messages_window->resizeColumnToContents(0);
  gui()->messages_window->resizeRowsToContents();
}

/********************************************************************** 
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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
#include <config.h>
#endif

// client
#include "client_main.h"

#include "fc_client.h"

/****************************************************************************
  Constructor
****************************************************************************/
fc_client::fc_client() : QObject()
{
  main_window = new QMainWindow;
  central_wdg = new QGraphicsView;
  output_window = new QTextEdit;

  output_window->setReadOnly(true);

  // main_window->setCentralWidget(central_wdg);
  main_window->setCentralWidget(output_window);
  main_window->setVisible(true);
}

/****************************************************************************
  Destructor
****************************************************************************/
fc_client::~fc_client()
{
  delete main_window;
}

/****************************************************************************
  Main part of gui-qt
****************************************************************************/
void fc_client::main(QApplication *qapp)
{
  set_client_state(C_S_DISCONNECTED);

  startTimer(TIMER_INTERVAL);

  qapp->exec();
}

/****************************************************************************
  New line of text for output window received
****************************************************************************/
void fc_client::append_output_window(const QString &str)
{
  output_window->append(str);
}

/****************************************************************************
  Timer event handling
****************************************************************************/
void fc_client::timerEvent(QTimerEvent *event)
{
  // Prevent current timer from repeating with possibly wrong interval
  killTimer(event->timerId());

  // Call timer callback in client common code and
  // start new timer with correct interval
  startTimer(real_timer_callback() * 1000);
}

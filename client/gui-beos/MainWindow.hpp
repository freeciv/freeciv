/*
	MainWindow.hpp
	Copyright (c) 1997 Be Do Have Software. All rights reserved.
 */
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP 1
#include "Defs.hpp"

#include "BdhLib.h"

class RadarView;
class SummaryView;
class StatusView;
class UnitInfoView;
class UnitsBelowView;
class MapCanvas;
class InputView;
class OutputView;

class MainWindow : public BdhMenuedWindow
{
public:
	MainWindow();
	~MainWindow();
	void MessageReceived( BMessage* );
	bool QuitRequested(void);
	void	Init(void);
	void	GotoDocUrl( const char *str );
private:
	void	setup_menus(void);
	void	update_menus(void);
private:
    RadarView*      radar;
    SummaryView*    summary;
    StatusView*     status;
    UnitInfoView*   unitInfo;
    UnitsBelowView* unitsBelow;
    MapCanvas*      map;
    InputView*      input;
    OutputView*     output;
};

#endif // MAINWINDOW_HPP

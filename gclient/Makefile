# Makefile for Linux 2.0.29

CC      = gcc
PROF    =
DEBUG   =
INCLUDE = -I. -I../client -I../common -I../ai
O_FLAGS = -O2
C_FLAGS = $(O_FLAGS) -Wall $(INCLUDE) $(DEBUG) $(PROF)	`gtk-config --cflags`
L_FLAGS = $(O_FLAGS)       $(DEBUG) $(PROF)	`gtk-config --libs`

O_FILES = gmain.o menu.o game.o help.o log.o climisc.o graphics.o colors.o \
          civclient.o clinet.o connect.o dialogs.o packhand.o mapview.o \
          mapctrl.o repodlgs.o\
          ../common/libcivcommon.a

all: civ

civ: $(O_FILES)
	rm -f civclient
	$(CC) $(L_FLAGS) -o civclient $(O_FILES)

.c.o:
	$(CC) -c $(C_FLAGS) $<

clean:
	rm -f *.o civclient

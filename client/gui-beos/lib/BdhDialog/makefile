## All necessary macros for defining one target binary
## (taken directly from BeOS R4 template makefile)
##	Sorry, no cross-compilation yet

# specify the name of the binary
NAME= libBdhDialog.so

# specify the type of binary
#	APP:	Application
#	SHARED:	Shared library or add-on
#	STATIC:	Static library archive
#	DRIVER: Kernel Driver - not implemented yet
TYPE= SHARED

# specify the source files to use
SRCS=	BdhDialog.cpp \
		BdhEntryDialog.cpp

# specify the resource files to use
RSRCS=

#specify additional libraries to link against
#	if libName.so or libName.a is the name of the library to link against
# 	then simply specify Name in the LIBS list
# 	if there is another naming scheme use the full binary
#	name: my_library.so or my_lib.a
LIBS=	Bdh be

#	specify the paths to directories where additional
# 	libraries are to be found.  /boot/develop/lib/PLATFORM/ is
#	already set.  The paths can be full or relative to this
#	makefile.  The paths included may not be recursive, so
#	specify all of the needed paths explicitly
LIBPATHS= ..

#	specify additional directories where header files can be found
# 	directories where sources are found are included automatically
SYSTEM_INCLUDE_PATHS =
LOCAL_INCLUDE_PATHS  = ../include

#	specify the level of optimization that you desire
#	NONE, SOME, FULL
OPTIMIZE= NONE

#	specify any symbols to be defined.  The symbols will be
#	set to a value of 1.  For example specify DEBUG if you want
#	DEBUG=1 to be set when compiling.
DEFINES=

#	specify special warning levels
#	if unspecified default warnings will be used
#	NONE = supress all warnings
#	ALL = enable all warnings
WARNINGS= ALL

#	specify debugging support
SYMBOLS=	#TRUE# puts symbols into target
DEBUGGER = 	#TRUE# allows debugger to be run on target

#	specify any other compiler and linker flags
COMPILER_FLAGS =
LINKER_FLAGS   =


all :: default

BUILDHOME = /boot/develop
include $(BUILDHOME)/etc/makefile-engine


#### Add any other targets you want here
relink : rmapp $(TARGET)

install :
	if [ ! -d ../include ]; then mkdir ../include ; fi
	cp *.h ../include
	cp $(OBJ_DIR)/*.so ..


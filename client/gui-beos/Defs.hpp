// Defs.hpp -- Messages and their types, and other definitions
#ifndef DEFS_HPP
#define DEFS_HPP	1

#include "About.hpp"

class App;
extern App *app;
class Backend;
extern Backend *backend;

// parameters governing final areas covered

// Helpful debugging and reminder
#define NOT_FINISHED(routine)   \
	{ BAlert *a = new BAlert( APP_NAME, routine, "NOT FINISHED" ); a->Go(); }

// Helpful little character define
#define ELLIPSIS        "\xE2\x80\xA6"


#endif // DEFS_HPP

#ifndef _rtlsdr_skyline_logger_h_
#define _rtlsdr_skyline_logger_h_
#include <CtrlLib/CtrlLib.h>
using namespace Upp;
#include <String>

void log_this(const char* msg);
void set_logger_callback( void(*log_callback)(const char*) );

#endif

#include "logger.h"

void(*logger_callback)(const char*) = NULL;

void set_logger_callback( void(*log_callback)(const char*) )
{
	logger_callback = log_callback;
	return;
}

void log_this(const char* msg)
{
	if (logger_callback != NULL)
		logger_callback(msg);
	return;
}
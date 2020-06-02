#include "handler.h"
#include <sstream>


LogFileHandler::LogFileHandler(const char *filename, Log::Level handler_level)
    : output(filename, std::ios::app)
{
    if (output.bad()) throw std::runtime_error("Cannot open log file!");
    level = handler_level;
}

void LogFileHandler::write(const std::stringstream &log) {
    output << log.str() << '\n';
    output.flush();
}

LogStreamHandler::LogStreamHandler(std::ostream &stream, Log::Level handler_level)
    : output(stream)
{
    level = handler_level;
}

void LogStreamHandler::write(const std::stringstream &log) {
    output << log.str() << '\n';
    output.flush();
}

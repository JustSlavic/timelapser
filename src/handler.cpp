#include "handler.h"
#include <sstream>


namespace Log {

FileHandler::FileHandler(const char *filename, Level handler_level)
    : output(filename, std::ios::app)
{
    if (output.bad()) throw std::runtime_error("Cannot open log file!");
    level = handler_level;
}

void FileHandler::write(const std::stringstream &log) {
    output << log.str() << '\n';
    output.flush();
}

StreamHandler::StreamHandler(std::ostream &stream, Level handler_level)
    : output(stream)
{
    level = handler_level;
}

void StreamHandler::write(const std::stringstream &log) {
    output << log.str() << '\n';
    output.flush();
}

}

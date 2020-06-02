#include "logging.h"
#include "handler.h"
#include <iostream>
#include <ctime>
#include <iomanip>

LogLocalContext::LogLocalContext() noexcept : name(nullptr) {}

LogLocalContext::LogLocalContext(const char *name) noexcept : name(name) {}

Log::Log(LogLocalContext ctx) : context(ctx) {}

Log::~Log() {
    LogGlobalContext::instance().write(log, level, context);
}

Log &Log::error() {
    this->level = Level::Error;
    return *this;
}

Log &Log::warning() {
    this->level = Level::Warning;
    return *this;
}

Log &Log::info() {
    this->level = Level::Info;
    return *this;
}

Log &Log::debug() {
    this->level = Level::Debug;
    return *this;
}

const char *log_level_to_cstr(Log::Level level) {
    switch (level) {
    case Log::Level::Error: return "ERROR";
    case Log::Level::Warning: return "WARNING";
    case Log::Level::Info: return "INFO";
    case Log::Level::Debug: return "DEBUG";
        default: return "";
    }
}

LogGlobalContext &LogGlobalContext::instance() {
    static LogGlobalContext instance;
    return instance;
}

void LogGlobalContext::write(std::stringstream &log, Log::Level log_level, LogLocalContext ctx) const {
    if (log_level < level) return;

    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);

    // Could not push parts of the log directly, because handler is a pointer to a base class
    // and pure virtual functions cannot be template
    std::stringstream temporary_log;
    temporary_log
            << std::put_time(&tm, "%F %T ")
            << std::setw(8) << log_level_to_cstr(log_level);
    if (ctx.name) temporary_log << " [" << ctx.name << "]";
    temporary_log << " " << std::left << std::setw(25) << log.str();

    for (auto& handler : outputs) {
        if (log_level < handler->level) continue;
        handler->write(temporary_log);
    }
}

LogGlobalContext &LogGlobalContext::set_level(Log::Level new_level) {
    this->level = new_level;
    return *this;
}

LogGlobalContext &LogGlobalContext::attach(std::ostream &os, Log::Level handler_level) {
    outputs.push_back(std::make_unique<LogStreamHandler>(os, handler_level));
    return *this;
}

LogGlobalContext &LogGlobalContext::attach(const char *filename, Log::Level handler_level) {
    outputs.push_back(std::make_unique<LogFileHandler>(filename, handler_level));
    return *this;
}

LogGlobalContext &LogGlobalContext::reset() {
    level = Log::Level::Debug;
    outputs.clear();
    return *this;
}

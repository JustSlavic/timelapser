#include "logging.h"
#include "handler.h"
#include <iostream>
#include <ctime>
#include <iomanip>

namespace Log {

LocalContext::LocalContext() noexcept : name(nullptr) {}

LocalContext::LocalContext(const char *name) noexcept : name(name) {}

Log::Log(LocalContext ctx) : context(ctx) {}

Log::~Log() {
    GlobalContext::instance().write(log, level, context);
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

const char *log_level_to_cstr(Level level) {
    switch (level) {
    case Level::Error: return "ERROR";
    case Level::Warning: return "WARNING";
    case Level::Info: return "INFO";
    case Level::Debug: return "DEBUG";
        default: return "";
    }
}

GlobalContext &GlobalContext::instance() {
    static GlobalContext instance;
    return instance;
}

void GlobalContext::write(std::stringstream &log, Level log_level, LocalContext ctx) const {
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

GlobalContext &GlobalContext::set_level(Level new_level) {
    this->level = new_level;
    return *this;
}

GlobalContext &GlobalContext::attach(std::ostream &os, Level handler_level) {
    outputs.push_back(std::make_unique<StreamHandler>(os, handler_level));
    return *this;
}

GlobalContext &GlobalContext::attach(const char *filename, Level handler_level) {
    outputs.push_back(std::make_unique<FileHandler>(filename, handler_level));
    return *this;
}

GlobalContext &GlobalContext::reset() {
    level = Level::Debug;
    outputs.clear();
    return *this;
}

}

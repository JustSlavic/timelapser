#ifndef GIR1_LOGGING_H
#define GIR1_LOGGING_H

#include <sstream>
#include <ostream>
#include <vector>
#include <memory>

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

#ifdef _DEBUG
#define LOG_CONTEXT(name) static LogLocalContext log_ctx_(name)
#define LOG_DEBUG Log(LogLocalContext(__FILE__ ":" STRINGIFY(__LINE__))).debug()
#define LOG_INFO Log(LogLocalContext(__FILE__ ":" STRINGIFY(__LINE__))).info()
#define LOG_WARNING Log(LogLocalContext(__FILE__ ":" STRINGIFY(__LINE__))).warning()
#define LOG_ERROR Log(LogLocalContext(__FILE__ ":" STRINGIFY(__LINE__))).error()
#else
#define LOG_CONTEXT(name) struct __SEMICOLON
struct DevNullSink {};
template <typename T>
DevNullSink const& operator<<(DevNullSink const& sink, T const&) { return sink; }
#define LOG_DEBUG DevNullSink()
#define LOG_INFO DevNullSink()
#define LOG_WARNING DevNullSink()
#define LOG_ERROR DevNullSink()
#endif

struct LogLocalContext {
    const char *name;

    LogLocalContext() noexcept;
    explicit LogLocalContext(const char *name) noexcept;
};

struct Log {
    enum class Level {
        Debug,
        Info,
        Warning,
        Error,
        Disabled,
    };

    LogLocalContext context;
    Level level = Level::Disabled;
    std::stringstream log;

    explicit Log(LogLocalContext ctx = LogLocalContext());
    ~Log();

    Log &error();
    Log &warning();
    Log &info();
    Log &debug();
};

template <typename T>
Log &operator<<(Log &logger, T&& data) {
    logger.log << data;
    return logger;
}

struct LogHandler;
struct LogGlobalContext {
    Log::Level level = Log::Level::Debug;
    std::vector<std::unique_ptr<LogHandler>> outputs;

    static LogGlobalContext &instance();
    LogGlobalContext &set_level(Log::Level level);
    LogGlobalContext &attach(std::ostream &os, Log::Level level = Log::Level::Debug);
    LogGlobalContext &attach(const char *filename, Log::Level level = Log::Level::Debug);
    LogGlobalContext &reset();

    void write(std::stringstream &log, Log::Level log_level, LogLocalContext ctx) const;

private:
    LogGlobalContext() = default;
};

#endif //GIR1_LOGGING_H

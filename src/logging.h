#ifndef TIMELAPSER_LOGGING_H
#define TIMELAPSER_LOGGING_H

#include <sstream>
#include <ostream>
#include <vector>
#include <memory>

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

#ifdef _DEBUG
#define LOG_CONTEXT(name) static Log::LocalContext log_ctx_(name)
#define LOG_DEBUG Log::Log(Log::LocalContext(__FILE__ ":" STRINGIFY(__LINE__))).debug()
#define LOG_INFO Log::Log(Log::LocalContext(__FILE__ ":" STRINGIFY(__LINE__))).info()
#define LOG_WARNING Log::Log(Log::LocalContext(__FILE__ ":" STRINGIFY(__LINE__))).warning()
#define LOG_ERROR Log::Log(Log::LocalContext(__FILE__ ":" STRINGIFY(__LINE__))).error()
#else
#define LOG_CONTEXT(name) struct SEMICOLON__
struct DevNullSink {};
template <typename T>
DevNullSink const& operator<<(DevNullSink const& sink, T const&) { return sink; }
#define LOG_DEBUG DevNullSink()
#define LOG_INFO DevNullSink()
#define LOG_WARNING DevNullSink()
#define LOG_ERROR DevNullSink()
#endif

namespace Log {

enum class Level {
    Debug,
    Info,
    Warning,
    Error,
    Disabled,
};

struct LocalContext {
    const char *name;

    LocalContext() noexcept;
    explicit LocalContext(const char *name) noexcept;
};

struct Log {
    LocalContext context;
    Level level = Level::Disabled;
    std::stringstream log;

    explicit Log(LocalContext ctx = LocalContext());
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

struct Handler;
struct GlobalContext {
    Level level = Level::Debug;
    std::vector<std::unique_ptr<Handler>> outputs;

    static GlobalContext &instance();
    GlobalContext &set_level(Level level);
    GlobalContext &attach(std::ostream &os, Level level = Level::Debug);
    GlobalContext &attach(const char *filename, Level level = Level::Debug);
    GlobalContext &reset();

    void write(std::stringstream &log, Level log_level, LocalContext ctx) const;

private:
    GlobalContext() = default;
};

}

#endif //TIMELAPSER_LOGGING_H

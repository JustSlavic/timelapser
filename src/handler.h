#ifndef GIR1_HANDLER_H
#define GIR1_HANDLER_H

#include <fstream>
#include "logging.h"

namespace Log {

struct Handler {
    Level level = Level::Debug;
    virtual void write(const std::stringstream& log) = 0;
};

struct FileHandler : public Handler {
    std::ofstream output;

    explicit FileHandler(const char *filename, Level handler_level = Level::Debug);
    void write(const std::stringstream& log) override;
};

struct StreamHandler : public Handler {
    std::ostream &output;

    explicit StreamHandler(std::ostream &stream, Level handler_level = Level::Debug);
    void write(const std::stringstream& log) override;
};

}

#endif //GIR1_HANDLER_H

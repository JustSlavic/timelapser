#pragma once

#include <cstddef>


namespace my {

struct Frame {
    void *data{nullptr};
    size_t size{0};

    Frame(void *data, size_t size);
    Frame(const Frame &);
    Frame(Frame &&);
    ~Frame();
};

}

#pragma once

#include <cstddef>
#include <cstdint>


namespace my {

struct Frame {
    uint8_t *data{nullptr};
    size_t size{0};

    Frame(uint8_t *data, size_t size);
    Frame(const Frame &);
    Frame(Frame &&);
    ~Frame();
};

}

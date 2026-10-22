#pragma once
#include <cstdint>
namespace input { struct Pad; }
namespace shaolin::keyboard {
bool Message(void*, uint32_t, uintptr_t, intptr_t, intptr_t*);
void Poll(int port, input::Pad& pad);
}

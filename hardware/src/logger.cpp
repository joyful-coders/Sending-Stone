/**
 * @file logger.cpp
 *
 * @brief Storage for the debug log queue declared in logger.h.
 *
 * @details
 * The Nicla's mbed core compiles as C++14, which has no inline variables, so
 * the queue's shared state is defined once here instead of in the header.
 *
 */

#include "logger.h"

namespace debug_logs {
LogMessage queue[kMaxMessages];
uint8_t head = 0;
uint8_t tail = 0;
uint16_t dropped = 0;
} // namespace debug_logs

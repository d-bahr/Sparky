#ifndef OPTIONS_H_
#define OPTIONS_H_

#include <stdbool.h>
#include <stdint.h>

// Enable/Disable debug mode (info string in uci).
extern bool g_optionDebugMode;

// Additional overhead for transmitting a move to the GUI, in milliseconds.
extern uint32_t g_optionMoveOverhead;

#endif // OPTIONS_H_

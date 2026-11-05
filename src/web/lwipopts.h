#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// Settings for the panel web server (Wi-Fi access point mode).
// Bigger heap than the SDK examples: the panel page and JSON replies are
// copied into lwIP buffers.
#define MEM_SIZE 16000

#include "lwipopts_examples_common.h"

#endif

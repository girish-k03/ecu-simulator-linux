#ifndef GATEWAY_H
#define GATEWAY_H

#include "can_frame.h"
#include "ipc_utils.h"

// Main gateway function
void run_gateway(int engine_fd);

// Thread function to handle engine data
void *engine_thread(void *arg);

#endif

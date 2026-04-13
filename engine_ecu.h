#ifndef ENGINE_ECU_H
#define ENGINE_ECU_H

#include "can_frame.h"
#include "ipc_utils.h"

void *rpm_thread(void *arg);
void *temp_thread(void *arg);
void run_engine_ecu(int write_fd);

#endif

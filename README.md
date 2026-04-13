Vehicle ECU Simulator
Linux System Programming Project
=====================================

ABOUT
-----
This project simulates a Vehicle ECU system using Linux.
It has 4 processes that communicate with each other using
different IPC methods. Engine data is generated, passed
through the system, faults are detected, and the operator
can send commands while the system is running.


FILES
-----
include/can_frame.h   - structs and sensor constants
include/ipc_utils.h   - IPC keys and paths
include/engine_ecu.h  - engine declarations
include/gateway.h     - gateway declarations
include/dashboard.h   - dashboard declarations

src/main.c            - starts all processes, handles shutdown
src/engine_ecu.c      - generates RPM and temperature data
src/gateway.c         - receives data, detects faults, logs
src/dashboard.c       - shows live RPM and temperature
src/ecu_client.c      - sends commands to gateway

Makefile

Structure: 
main (supervisor)
    ├── engine_ecu  [fork + exec]  -> sends data via pipe
    └── gateway     [fork + exec]  -> receives pipe, manages IPC
          └── dashboard [fork + exec]  -> reads shared memory


IPC USED
--------
pipe           - engine_ecu sends CAN frames to gateway
shared memory  - gateway writes live data, dashboard reads it
semaphore      - protects shared memory from concurrent access
message queue  - gateway sends fault alerts to main
FIFO           - operator sends commands to gateway
signals        - SIGINT, SIGTERM, SIGUSR1, SIGCHLD, SIGALRM


HOW TO RUN
----------
make 
./main

Other terminal:
./ecu_client STATUS
./ecu_client RESET
kill -USR1 <main_pid>    <- saves snapshot.txt

Ctrl+C to stop.

make ipcclean   <- only if program crashed, to clear IPC resources
make clean      <- removes binaries and log FAULT SCENARIOS SIMULATED

Fault Scenario 
--------------------------------------------------------------------------------
  Engine overheat  : coolant temp exceeds 105 C  -> CRITICAL alert
  RPM redline      : RPM exceeds 6500            -> WARNING alert
  ECU silent       : no data for 10 seconds      -> WARNING via SIGALRM


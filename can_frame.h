#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include <time.h>

#define ECU_ID 1  // ECU ID

// Data  for shared memory
#define DATA_RPM 1  
#define DATA_TEMP 2

//thresholds
#define MAX_COOLANT_TEMPR 105.0
#define MIN_FUEL_LEVEL 10
#define MAX_RPM 6500

struct can_frame{
   int ecu_id;
   int data_type;
   float value;
   time_t timestamp;
};

struct live_status{
   float rpm;
   float coolant_temp;
   int fault_overheat;
   int fault_rpm;
   time_t last_update;
};


struct aler_msg{
   long mtype;
   int ecu_id;
   float value;
   char severity[16];
   char description[64];
};

#endif

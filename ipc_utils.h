#ifndef IPC_UTILS_H
#define IPC_UTILS_H

//#define UNUSED(x) (void)(x)

#define READ_END 0  //for pipe read end
#define WRITE_END 1  //for pipe write end 
#define SHM_KEY 011  //semaphore key
#define MSG_KEY 022  //message queue key 
#define SEM_NAME "/ecu_sem"   // semaphore name
#define FIFO_PATH "/tmp/ecu_cmd"  //fifo path 

//message types
#define MSG_ENGINE_FAULT 1
#define MSG_ABS_FAULT 2

#define HEALTH_CHECK_SECS 10
#define DASHBOARD_REFRESH 3

#endif 


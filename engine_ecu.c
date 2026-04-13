#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <pthread.h>
#include "ipc_utils.h"
#include "can_frame.h"
#include "engine_ecu.h"

int pipe_write_fd;
volatile int engine_running = 1;
float current_rpm  = 0.0;
float current_temp = 0.0;
pthread_mutex_t sensor_mutex = PTHREAD_MUTEX_INITIALIZER;


// engine ecu signal handler
void handle_engine_sigterm(int sig){
    (void)sig;
    engine_running = 0;
}


// function to write updated can frame on pipe
void send_can_frame(int data_type, float value){
    struct can_frame frame;

    frame.ecu_id    = ECU_ID;
    frame.data_type = data_type;
    frame.value     = value;
    time(&frame.timestamp);

    // write on pipe to send to gateway
    if(write(pipe_write_fd, &frame, sizeof(frame)) == -1){
        perror("[ECU] Error while writing CAN Frame");
        _exit(1);
    }
}


// thread 1 - rpm sensor thread => generate rpm sensor values
void *rpm_thread(void *arg){
    (void)arg;

    while(engine_running){
        int rpm;

        if(rand() % 10 == 0)
            rpm = 6500 + rand() % 500;
        else
            rpm = 600 + rand() % 5900;

        pthread_mutex_lock(&sensor_mutex);
        current_rpm = rpm;
        pthread_mutex_unlock(&sensor_mutex);

        // send to can frame
        send_can_frame(DATA_RPM, rpm);
        printf("[ENGINE ECU] RPM = %d\n", rpm);

        usleep(500000);
    }
    return NULL;
}


// thread 2 - temp sensor thread to create temp values
void *temp_thread(void *arg){
    (void)arg;

    while(engine_running){
        float temp;

        if(rand() % 10 == 0)
            temp = 106.0 + rand() % 5;
        else
            temp = 70.0 + rand() % 35;

        pthread_mutex_lock(&sensor_mutex);
        current_temp = temp;
        pthread_mutex_unlock(&sensor_mutex);

        // send to can frame
        send_can_frame(DATA_TEMP, temp);
        printf("[ENGINE ECU] TEMP = %.2f\n", temp);

        usleep(600000);
    }
    return NULL;
}


// run engine ecu
void run_engine_ecu(int write_fd){
    signal(SIGTERM, handle_engine_sigterm);

    pipe_write_fd = write_fd;

    pthread_t t1, t2;
    pthread_create(&t1, NULL, rpm_thread, NULL);
    pthread_create(&t2, NULL, temp_thread, NULL);

    printf("[ENGINE ECU] Started. PID = %d\n", getpid());
    printf("[ENGINE ECU] Threads running.\n");

    while(engine_running){
        sleep(1);
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    close(write_fd);

    printf("[ENGINE ECU] Clean shutdown.\n");
    _exit(0);
}


int main(int argc, char *argv[]){
    if(argc < 2){
        fprintf(stderr, "[ENGINE ECU] Usage: engine_ecu <write_fd>\n");
        return 1;
    }

    int write_fd = atoi(argv[1]);
    run_engine_ecu(write_fd);
    return 0;
}

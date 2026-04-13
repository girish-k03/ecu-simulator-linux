#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/shm.h>
#include "dashboard.h"

// globals
volatile int dash_running = 1;
struct live_status *live_data = NULL;
sem_t *dash_sem = NULL;


// signal handler
void handle_dash_sigterm(int sig){
    (void)sig;
    dash_running = 0;
}


// dashboard main loop
void run_dashboard(int shm_id){
    signal(SIGTERM, handle_dash_sigterm);

    // attach to shared memory created by gateway
    live_data = (struct live_status *)shmat(shm_id, NULL, 0);
    if(live_data == (void *)-1){
        perror("shmat failed");
        exit(1);
    }

    // open existing semaphore (created by gateway)
    dash_sem = sem_open(SEM_NAME, 0);
    if(dash_sem == SEM_FAILED){
        perror("sem_open failed");
        exit(1);
    }

    printf("[DASHBOARD] Started.\n");

    while(dash_running){
        // read shared memory safely
        sem_wait(dash_sem);
        float rpm  = live_data->rpm;
        float temp = live_data->coolant_temp;
        int ov_fault = live_data->fault_overheat;
        int rpm_fault = live_data->fault_rpm;
        sem_post(dash_sem);

        // print dashboard
        printf("\033[2J\033[H");  // clear screen
        printf("==== VEHICLE DASHBOARD ====\n");
        printf("  RPM         : %.0f\n",  rpm);
        printf("  Temperature : %.2f C\n", temp);

        if(ov_fault || rpm_fault)
            printf("  Status      : *** FAULT DETECTED ***\n");
        else
            printf("  Status      : Normal\n");

        printf("===========================\n");

        sleep(DASHBOARD_REFRESH);
    }

    // cleanup
    shmdt(live_data);
    sem_close(dash_sem);

    printf("[DASHBOARD] Exiting.\n");
    _exit(0);
}


int main(int argc, char *argv[]){
    if(argc < 2){
        fprintf(stderr, "[DASHBOARD] Usage: dashboard <shm_id>\n");
        return 1;
    }

    int shm_id = atoi(argv[1]);
    run_dashboard(shm_id);
    return 0;
}

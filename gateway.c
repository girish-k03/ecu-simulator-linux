#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "gateway.h"
#include "can_frame.h"
#include "ipc_utils.h"

// global variables
volatile int gateway_running = 1;

int engine_fd;
int can_log_fd  = -1;
int fault_log_fd = -1;
int total_frames = 0;
int total_faults = 0;
int msg_queue_id = -1;

struct live_status *live_data = NULL;
int shm_id  = -1;
sem_t *shm_sem = NULL;

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;

pid_t dashboard_pid = -1;

// command flags set by FIFO command thread
volatile int cmd_status_requested = 0;
volatile int cmd_reset_requested  = 0;


// SIGNAL HANDLERS
void handle_gateway_sigterm(int sig){
    (void)sig;
    gateway_running = 0;
}

void handle_gateway_sigalrm(int sig){
    (void)sig;
    printf("[GATEWAY] Health check: system running.\n");
    alarm(HEALTH_CHECK_SECS);
}


// LOGGING FUNCTIONS
void open_log_files(void){
    can_log_fd = open("can_bus.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(can_log_fd == -1){
        perror("can log open failed");
        return;
    }
    write(can_log_fd, "FRAMES:000000\n", 14);
    printf("[LOG] can_bus.log created.\n");

    fault_log_fd = open("fault_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fault_log_fd == -1){
        perror("fault log open failed");
        return;
    }
    write(fault_log_fd, "FAULTS:000000\n", 14);
    printf("[LOG] fault_log.txt created.\n");
}

void write_can_log(struct can_frame *frame){
    if(can_log_fd == -1) return;

    char line[128];
    char header[20];

    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strlen(ts) - 1] = '\0';

    snprintf(line, sizeof(line),
        "[%s] ECU=%d TYPE=%d VAL=%.2f\n",
        ts, frame->ecu_id, frame->data_type, frame->value);

    lseek(can_log_fd, 0, SEEK_END);
    write(can_log_fd, line, strlen(line));

    total_frames++;
    snprintf(header, sizeof(header), "FRAMES:%06d\n", total_frames);
    lseek(can_log_fd, 0, SEEK_SET);
    write(can_log_fd, header, 14);
    lseek(can_log_fd, 0, SEEK_END);
}

void write_fault_log(char *desc, char *severity, int ecu_id, float val){
    if(fault_log_fd == -1) return;

    char line[200];
    char header[20];

    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strlen(ts) - 1] = '\0';

    snprintf(line, sizeof(line),
        "[%s] ECU=%d SEV=%s VAL=%.2f DESC=%s\n",
        ts, ecu_id, severity, val, desc);

    lseek(fault_log_fd, 0, SEEK_END);
    write(fault_log_fd, line, strlen(line));

    total_faults++;
    snprintf(header, sizeof(header), "FAULTS:%06d\n", total_faults);
    lseek(fault_log_fd, 0, SEEK_SET);
    write(fault_log_fd, header, 14);
    lseek(fault_log_fd, 0, SEEK_END);
}


// ALERT FUNCTION
void send_alert_to_main(char *severity, char *desc, int ecu_id, float val){
    struct aler_msg msg;

    msg.mtype  = MSG_ENGINE_FAULT;
    msg.ecu_id = ecu_id;
    msg.value  = val;
    strncpy(msg.severity,    severity, sizeof(msg.severity) - 1);
    strncpy(msg.description, desc,     sizeof(msg.description) - 1);

    msgsnd(msg_queue_id, &msg, sizeof(msg) - sizeof(long), 0);
    printf("[GATEWAY] Alert sent: %s %s\n", severity, desc);
}


// ENGINE THREAD - reads CAN frames from pipe, logs and checks faults
void *engine_thread(void *arg){
    (void)arg;

    struct can_frame frame;

    while(gateway_running){
        int n = read(engine_fd, &frame, sizeof(frame));
        if(n <= 0) break;

        write_can_log(&frame);

        // check temp fault
        if(frame.data_type == DATA_TEMP && frame.value > MAX_COOLANT_TEMPR){
            write_fault_log("Overheat", "CRITICAL", frame.ecu_id, frame.value);
            send_alert_to_main("CRITICAL", "Overheat", frame.ecu_id, frame.value);
        }

        // check rpm fault
        if(frame.data_type == DATA_RPM && frame.value > MAX_RPM){
            write_fault_log("Overspeed", "CRITICAL", frame.ecu_id, frame.value);
            send_alert_to_main("CRITICAL", "Overspeed", frame.ecu_id, frame.value);
        }

        // update live status in shared memory
        pthread_mutex_lock(&data_mutex);
        if(live_data){
            if(frame.data_type == DATA_RPM)  live_data->rpm          = frame.value;
            if(frame.data_type == DATA_TEMP) live_data->coolant_temp = frame.value;
            if(frame.data_type == DATA_TEMP && frame.value > MAX_COOLANT_TEMPR)
                live_data->fault_overheat = 1;
            else if(frame.data_type == DATA_TEMP)
                live_data->fault_overheat = 0;
            if(frame.data_type == DATA_RPM && frame.value > MAX_RPM)
                live_data->fault_rpm = 1;
            else if(frame.data_type == DATA_RPM)
                live_data->fault_rpm = 0;
            live_data->last_update = time(NULL);
        }
        pthread_cond_signal(&data_ready);
        pthread_mutex_unlock(&data_mutex);
    }
    return NULL;
}


// COMMAND THREAD - reads operator commands from FIFO
void *command_thread(void *arg){
    (void)arg;

    // create FIFO if not already there
    if(mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST){
        perror("mkfifo failed");
        return NULL;
    }

    printf("[GATEWAY] FIFO ready. Run: ./ecu_client STATUS|RESET|STOP\n");

    while(gateway_running){
        // non-blocking open - if no client yet, wait and retry
        int fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
        if(fd == -1){
            sleep(1);
            continue;
        }

        // read commands until client disconnects
        while(gateway_running){
            char cmd[32];
            int n = read(fd, cmd, sizeof(cmd) - 1);

            if(n == 0) break;  // client closed - wait for next one
            if(n == -1){
                if(errno == EAGAIN){
                    usleep(200000);
                    continue;
                }
                break;
            }

            cmd[n] = '\0';
            // trim trailing newline if any
            if(n > 0 && cmd[n-1] == '\n') cmd[n-1] = '\0';

            printf("[GATEWAY] Command received: %s\n", cmd);

            if(strcmp(cmd, "STATUS") == 0)
                cmd_status_requested = 1;
            else if(strcmp(cmd, "RESET") == 0)
                cmd_reset_requested = 1;
            else if(strcmp(cmd, "STOP") == 0)
                gateway_running = 0;
            else
                printf("[GATEWAY] Unknown command: %s\n", cmd);
        }

        close(fd);
    }
    return NULL;
}


// SPAWN DASHBOARD
void spawn_dashboard(void){
    pid_t pid = fork();
    if(pid == -1){
        perror("fork failed for dashboard");
        return;
    }

    if(pid == 0){
        // child process
        char shm_str[10];
        snprintf(shm_str, sizeof(shm_str), "%d", shm_id);
        execl("./dashboard", "dashboard", shm_str, NULL);
        perror("execl dashboard failed");
        _exit(1);
    }

    dashboard_pid = pid;
    printf("[GATEWAY] Dashboard started. PID = %d\n", dashboard_pid);
}


// RUN GATEWAY
void run_gateway(int fd){
    signal(SIGTERM, handle_gateway_sigterm);
    signal(SIGALRM, handle_gateway_sigalrm);

    engine_fd = fd;

    open_log_files();

    // connect to message queue created by main
    msg_queue_id = msgget(MSG_KEY, 0666);
    if(msg_queue_id == -1){
        perror("msgget failed in gateway");
        _exit(1);
    }

    // create shared memory for dashboard
    shm_id = shmget(SHM_KEY, sizeof(struct live_status), IPC_CREAT | 0666);
    if(shm_id == -1){
        perror("shmget failed");
        _exit(1);
    }

    live_data = (struct live_status *)shmat(shm_id, NULL, 0);
    if(live_data == (void *)-1){
        perror("shmat failed");
        _exit(1);
    }

    // zero out shared memory
    memset(live_data, 0, sizeof(struct live_status));

    // create named semaphore to protect shared memory
    shm_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if(shm_sem == SEM_FAILED){
        perror("sem_open failed");
        _exit(1);
    }

    spawn_dashboard();
    alarm(HEALTH_CHECK_SECS);

    // start engine data thread and command thread
    pthread_t t_engine, t_cmd;
    pthread_create(&t_engine, NULL, engine_thread, NULL);
    pthread_create(&t_cmd,    NULL, command_thread, NULL);

    printf("[GATEWAY] Started. PID = %d\n", getpid());

    // main loop - wake on new data, handle commands
    while(gateway_running){
        pthread_mutex_lock(&data_mutex);
        pthread_cond_wait(&data_ready, &data_mutex);
        pthread_mutex_unlock(&data_mutex);

        if(cmd_status_requested){
            cmd_status_requested = 0;
            if(live_data)
                printf("[GATEWAY] STATUS: RPM=%.0f TEMP=%.1f\n",
                       live_data->rpm, live_data->coolant_temp);
        }

        if(cmd_reset_requested){
            cmd_reset_requested = 0;
            printf("[GATEWAY] RESET acknowledged.\n");
        }
    }

    // shutdown
    pthread_join(t_engine, NULL);
    pthread_join(t_cmd,    NULL);

    // stop dashboard
    if(dashboard_pid > 0){
        kill(dashboard_pid, SIGTERM);
        waitpid(dashboard_pid, NULL, 0);
        printf("[GATEWAY] Dashboard stopped.\n");
    }

    // cleanup IPC
    shmdt(live_data);
    shmctl(shm_id, IPC_RMID, NULL);
    sem_close(shm_sem);
    sem_unlink(SEM_NAME);
    unlink(FIFO_PATH);

    if(can_log_fd  != -1) close(can_log_fd);
    if(fault_log_fd != -1) close(fault_log_fd);
    close(fd);

    printf("[GATEWAY] Clean shutdown.\n");
    _exit(0);
}


int main(int argc, char *argv[]){
    if(argc < 2){
        fprintf(stderr, "[GATEWAY] Usage: gateway <engine_fd>\n");
        return 1;
    }

    int engine_read_fd = atoi(argv[1]);
    run_gateway(engine_read_fd);
    return 0;
}

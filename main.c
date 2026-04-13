#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include "ipc_utils.h"
#include "can_frame.h"

// flags for signals
volatile int shutdown_flag = 0;
volatile int snapshot_flag = 0;

pid_t engine_pid = -1, gateway_pid = -1;

int main_msg_queue_id = -1;  // to clean up message queue
int engine_pipe[2];           // pipe betn engine ecu and gateway

time_t start_time;            // for snapshot uptime


// signal handlers
void handle_sigint(int sig){
    (void)sig;
    shutdown_flag = 1;
}

void handle_sigusr1(int sig){
    (void)sig;
    snapshot_flag = 1;
}

void handle_sigchld(int sig){
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}


// to create engine ecu process
pid_t create_engine_ecu(void){
    if(pipe(engine_pipe) == -1){
        perror("pipe failed");
        exit(1);
    }

    pid_t pid = fork();

    if(pid == -1){
        perror("fork failed");
        exit(1);
    }

    if(pid == 0){
        // child process
        close(engine_pipe[READ_END]);

        char fd_str[10];
        snprintf(fd_str, sizeof(fd_str), "%d", engine_pipe[WRITE_END]);

        execl("./engine_ecu", "engine_ecu", fd_str, NULL);

        perror("exec engine_ecu failed");
        _exit(1);
    }

    // parent process
    close(engine_pipe[WRITE_END]);
    printf("[MAIN] Engine ECU started. PID = %d\n", pid);
    return pid;
}


// to create gateway process
pid_t create_gateway(void){
    pid_t pid = fork();

    if(pid == -1){
        perror("fork failed");
        exit(1);
    }

    if(pid == 0){
        // child process
        close(engine_pipe[WRITE_END]);

        char fd_str[10];
        snprintf(fd_str, sizeof(fd_str), "%d", engine_pipe[READ_END]);

        execl("./gateway", "gateway", fd_str, NULL);

        perror("exec gateway failed");
        _exit(1);
    }

    // parent process
    close(engine_pipe[READ_END]);
    printf("[MAIN] Gateway started. PID = %d\n", pid);
    return pid;
}


// check system fault alerts from gateway via message queue
void check_alerts(void){
    struct aler_msg msg;

    while(1){
        int ret = msgrcv(main_msg_queue_id, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT);

        if(ret == -1){
            // no messages waiting, done for now
            break;
        }

        printf("[MAIN] ALERT: ECU=%d SEV=%s VAL=%.2f: %s\n",
               msg.ecu_id, msg.severity, msg.value, msg.description);
    }
}


// write current system state to snapshot.txt
void write_snapshot(void){
    int fd = open("snapshot.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd == -1){
        perror("snapshot open failed");
        return;
    }

    char buf[256];
    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strlen(ts) - 1] = '\0';

    // write snapshot content
    snprintf(buf, sizeof(buf), "==== ECU SIMULATOR SNAPSHOT ====\n");
    write(fd, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "Taken at   : %s\n", ts);
    write(fd, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "Main PID   : %d\n", getpid());
    write(fd, buf, strlen(buf));

    if(engine_pid > 0)
        snprintf(buf, sizeof(buf), "Engine PID : %d\n", engine_pid);
    else
        snprintf(buf, sizeof(buf), "Engine PID : NOT RUNNING\n");
    write(fd, buf, strlen(buf));

    if(gateway_pid > 0)
        snprintf(buf, sizeof(buf), "Gateway PID: %d\n", gateway_pid);
    else
        snprintf(buf, sizeof(buf), "Gateway PID: NOT RUNNING\n");
    write(fd, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "Uptime     : %ld seconds\n", (long)(now - start_time));
    write(fd, buf, strlen(buf));

    close(fd);
    printf("[MAIN] Snapshot written to snapshot.txt\n");
}


// stop all child processes and free resources
void cleanup(void){
    printf("[MAIN] Cleanup starting...\n");

    if(engine_pid > 0){
        kill(engine_pid, SIGTERM);
        waitpid(engine_pid, NULL, 0);
        printf("[MAIN] Engine ECU stopped.\n");
    }

    if(gateway_pid > 0){
        kill(gateway_pid, SIGTERM);
        waitpid(gateway_pid, NULL, 0);
        printf("[MAIN] Gateway stopped.\n");
    }

    if(main_msg_queue_id != -1){
        msgctl(main_msg_queue_id, IPC_RMID, NULL);
    }

    printf("[MAIN] Cleanup complete.\n");
}


int main(void){
    start_time = time(NULL);

    printf("[MAIN] Vehicle ECU Simulator starting. PID = %d\n", getpid());

    // install signal handlers
    signal(SIGINT,  handle_sigint);
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGCHLD, handle_sigchld);

    // create message queue for receiving gateway alerts
    main_msg_queue_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if(main_msg_queue_id == -1){
        perror("msgget failed");
        exit(1);
    }

    engine_pid  = create_engine_ecu();
    sleep(1);
    gateway_pid = create_gateway();

    printf("[MAIN] System is running. Press Ctrl+C to shutdown.\n");

    while(shutdown_flag == 0){
        check_alerts();

        if(snapshot_flag){
            snapshot_flag = 0;
            write_snapshot();
        }

        sleep(1);
    }

    cleanup();
    return 0;
}

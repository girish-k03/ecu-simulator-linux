#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "ipc_utils.h"

int main(int argc, char *argv[]){
    // default command is STATUS if none given
    const char *cmd = "STATUS";
    if(argc >= 2)
        cmd = argv[1];

    printf("[CLIENT] Sending command: %s\n", cmd);

    // open FIFO for writing - blocks until gateway opens read side
    int fd = open(FIFO_PATH, O_WRONLY);
    if(fd == -1){
        perror("open FIFO failed - is gateway running?");
        return 1;
    }

    write(fd, cmd, strlen(cmd));
    close(fd);

    printf("[CLIENT] Sent: %s\n", cmd);
    return 0;
}

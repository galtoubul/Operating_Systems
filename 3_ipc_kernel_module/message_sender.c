#include <stdio.h>
#include <asm/errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <zconf.h>
#include "message_slot.h"

/**
 * argv[1] = message slot file path.
 * argv[2] = the target message channel id.
 * argv[3] = the message to pass.
 */
int main(int argc, char** argv){
    if(argc != 4){
        fprintf(stderr, "Error: %s\n", strerror(EINVAL));
        exit(1);
    }

    // Open the specified message slot device file
    int fd = open(argv[1], O_RDWR);

    if(fd < 0){
        fprintf(stderr, "Error: couldn't open the given file at path: %s\n", argv[1]);
        exit(1);
    }

    // Set the channel id to the id specified on the command line
    int status = ioctl(fd, MSG_SLOT_CHANNEL, atoi(argv[2]));

    if(status < 0){
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(1);
    }

    // Write the specified message to the message slot file
    status = write(fd, argv[3], strlen(argv[3]));

    if(status < 0){
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(1);
    }

    // Close the device
    status = close(fd);

    if(status < 0){
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(1);
    }

    // if all the above didn't cause an error
    exit(0);
}

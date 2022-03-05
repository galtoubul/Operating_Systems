#ifndef EX3_MESSAGE_SLOT_H
#define EX3_MESSAGE_SLOT_H

#include <linux/ioctl.h>

// ioctl command option (the only one)
#define MSG_SLOT_CHANNEL 101

#define MAJOR_NUM 240
#define DEVICE_RANGE_NAME "message_slot"
#define BUF_LEN 128
#define DEVICE_FILE_NAME "message_slot"

#endif

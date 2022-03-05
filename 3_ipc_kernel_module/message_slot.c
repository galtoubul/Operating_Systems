#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include "message_slot.h"

#define OPEN_CHANNEL_WITHOUT_MESSAGE -1

MODULE_LICENSE("GPL");

/**
        -------------------------------------- Main Ideas --------------------------------------

 - An array for all 256 possible minors = 256 different message slots files = 256 different Linked lists.
 - Each entry at the array is a linked list.
 - Each node at the linked list is a different channel id in his message slots file.
 - Each file will hold its current channel id (if selected), by pointing to the appropriate node at the linked list
   (each node has a channel_id field)

         -------------------------------------- Main Functions --------------------------------------

 - device_open     -       Creating a new linked list if message_slots[minor] == NULL
 - device_ioctl    -    1) Creating a new node for the given channel id (if it wasn't created yet)
                        2) Setting the file's private data to point the appropriate node
                           (which holds the given channel id)
 - device_write    -       Writing the given data to the appropriate node
                           (in according to the current channel id)
 - device_read     -       Reading the data from the appropriate node
                           (in according to the current channel id)
 */

// Data structure for the 256 different message slots - array of linked lists

// struct for a node in our linked list
typedef struct node{

    // the channel id of the node
    unsigned int channel_id;

    // the content of the node - from/to which we can read/write
    char message[BUF_LEN + 1];

    // pointer to the next node
    struct node* next;

    // number of bytes written at the last time we made a write operation
    int bytes;
}node;

// struct for a linked list in our array of message slots
typedef struct list{
    struct node* head;
    int num_of_nodes;
}list;

// message_slots array - entry for each minor
list* message_slots [256];

//================== DEVICE FUNCTIONS ===========================

static int device_open(struct inode* inode, struct file* file){
    unsigned int minor_num;

    if(inode == NULL || file == NULL){
        printk(KERN_ERR "device_open - ERROR: NULL arg at device_open\n");
        return -EINVAL;
    }

    printk("device_open - Invoking device_open(%p)\n", file);

    minor_num = iminor(inode);
    printk("device_open - minor# = %d\n", minor_num);

    // check if the given minor# has a linked list in our array, and if not make one
    if(message_slots[minor_num] == NULL){
        message_slots[minor_num] = kmalloc(sizeof(list), GFP_KERNEL);

        if(!message_slots[minor_num]){
            printk(KERN_ERR "device_open - ERROR: kmalloc failed\n");
            return -ENOMEM;
        }

        message_slots[minor_num] -> head = NULL;
        message_slots[minor_num] -> num_of_nodes = 0;
    }

    return 0;
}

static int device_release (struct inode* inode, struct file* file){
    if(inode == NULL || file == NULL){
        printk(KERN_ERR "device_release - ERROR: NULL arg at device_release\n");
        return -EINVAL;
    }

    printk("device_release - Invoking device_release(%p,%p)\n", inode, file);
    printk("--------------------------------\n");

    return 0;
}

/**
 * Reads the last message written on the channel into the user's buffer
 * @return On success: number of bytes read. OW, error value.
 */
static ssize_t device_read (struct file* file, char __user* buffer, size_t length, loff_t* offset){
    unsigned int   minor_num;
    list*          tmp_message_slot;
    int            bytes_read;
    int            err;

    if(file == NULL || buffer == NULL|| offset == NULL){
        printk(KERN_ERR "device_read - ERROR: NULL arg at device_read\n");
        return -EINVAL;
    }

    // checking if a channel has been set on the file
    if(file -> private_data == NULL){
        printk(KERN_ERR "device_read - ERROR: no channel has been set on the file descriptor\n");
        return -EINVAL;
    }

    minor_num = iminor(file -> f_path.dentry -> d_inode);

    // checking if the message slot file exists
    tmp_message_slot = message_slots[minor_num];

    if(tmp_message_slot == NULL){
        printk(KERN_ERR "device_read - ERROR: file wasn't opened before it was passed to read\n");
        return -EINVAL;
    }

    // checking if the channel id has been set
    if(file -> private_data == NULL){
        printk(KERN_ERR "device_write - ERROR: no channel has been set on the file descriptor\n");
        return -EINVAL;
    }

    // checking if a message has been set on the channel
    if(((node*)file -> private_data) -> bytes == OPEN_CHANNEL_WITHOUT_MESSAGE){
        printk(KERN_ERR "device_read - ERROR: no message has been set on the channel\n");
        return -EWOULDBLOCK;
    }

    // checking if the provided buffer length is too small
    if(((node*)file -> private_data) -> bytes > length){
        printk(KERN_ERR "device_read - ERROR: The provided buffer length is too small to hold the last message written on the channel\n");
        return -ENOSPC;
    }

    // reading the message
    printk("device_read - Invoking device_read(%p,%ld)\n", file, length);

    for(bytes_read = 0; bytes_read < length && bytes_read < BUF_LEN; ++bytes_read){
        err = put_user(((node*)file -> private_data) -> message[bytes_read], &buffer[bytes_read]);

        if(err != 0){
            printk(KERN_ERR "device_write - ERROR: get_user failed\n");
            return err;
        }
    }

    return bytes_read;
}

/**
 * Writes a non-empty message of up to 128 bytes
 * (possibly contains something different than a C string)
 * @return On success: number of bytes written. OW, error value.
 */
static ssize_t device_write (struct file* file, const char __user* buffer, size_t length, loff_t* offset){
    unsigned int   minor_num;
    list*          tmp_message_slot;
    unsigned int   file_cid;
    int            bytes_written;
    int            err;
    char  tmp[BUF_LEN];
    int            i;

    if(buffer == NULL || file == NULL || offset == NULL){
        printk(KERN_ERR "device_write - ERROR: NULL arg at device_write\n");
        return -EINVAL;
    }

    if(length == 0 || length > 128){
        printk(KERN_ERR "device_write - ERROR: passed message size is 0 or more than 128\n");
        return -EMSGSIZE;
    }

    // checking if no channel has been set on the fd
    if(file -> private_data == NULL){
        printk(KERN_ERR "device_write - ERROR: no channel has been set on the file descriptor\n");
        return -EINVAL;
    }

    minor_num = iminor(file -> f_path.dentry -> d_inode);
    printk("device_write - minor_num = %d\n", minor_num);

    // checking if the message slot file exists
    tmp_message_slot = message_slots[minor_num];

    if(tmp_message_slot == NULL){
        printk(KERN_ERR "device_write - ERROR: file wasn't opened before it was passed to write\n");
        return -EINVAL;
    }

    file_cid = ((node*)file -> private_data) -> channel_id;
    printk("device_write - file_cid = %d\n", file_cid);

    // writing the message
    printk("device_write - Invoking device_write (%p,%ld)\n", file, length);
    bytes_written = 0;
    err = 0;

    for(bytes_written = 0; bytes_written < length && bytes_written < BUF_LEN; ++bytes_written){
        err = get_user(tmp[bytes_written], &buffer[bytes_written]);

        if(err != 0){
            printk(KERN_ERR "device_write - ERROR: get_user failed\n");
            return err;
        }
    }

    // copying the data from the tmp space. Ensures the atomic operation of get_user
    for(i = 0; i < length && i < BUF_LEN; i++){
        ((node*)file -> private_data) -> message[i] = tmp[i];
    }

    ((node*)file -> private_data) -> message[bytes_written] = '\0';
    ((node*)file -> private_data) -> bytes = bytes_written;

    return bytes_written;
}

/**
 * Supports a single ioctl command: MSG_SLOT_CHANNEL
 * @param channel_id_para - non zero channel id
 * @return 0 on success. OW, 1.
 */
static long device_ioctl(struct file* file, unsigned int ioctl_command_id, unsigned long channel_id_para){
    unsigned int   minor_num;
    list*          tmp_message_slot;
    int            exist;
    node*          tmp_node;
    node*          ptr;
    int            i;

    if(ioctl_command_id != MSG_SLOT_CHANNEL){
        printk(KERN_ERR "device_ioctl - ERROR: ioctl_command_id is invalid\n");
        return -EINVAL;
    }

    if(channel_id_para == 0){
        printk(KERN_ERR "device_ioctl - ERROR: channel_id_para is 0\n");
        return -EINVAL;
    }

    minor_num = iminor(file -> f_path.dentry -> d_inode);
    printk("device_ioctl - minor_num = %d\n", minor_num);

    // checking if the message slot file exists
    tmp_message_slot = message_slots[minor_num];

    if(tmp_message_slot == NULL){
        printk(KERN_ERR "device_ioctl - ERROR: file wasn't opened\n");
        return -EINVAL;
    }

    // checking if channel id exists
    exist = 0;

    tmp_node = tmp_message_slot -> head;
    while(tmp_node != NULL){
        if(tmp_node -> channel_id == channel_id_para){
            exist = 1;
            break;
        }
        tmp_node = tmp_node -> next;
    }

    if(!exist){
        printk("device_ioctl - channel id doesn't exist\n");

        tmp_node = kmalloc(sizeof(struct node), GFP_KERNEL);

        if(!tmp_node){
            printk(KERN_ERR "device_ioctl - ERROR: kmalloc failed\n");
            return -ENOMEM;
        }

        tmp_node -> next = NULL;
        tmp_node -> channel_id = channel_id_para;
        printk("device_ioctl - tmp_node's channel_id = %d\n", tmp_node -> channel_id);
        tmp_node -> bytes = OPEN_CHANNEL_WITHOUT_MESSAGE;

        if(tmp_message_slot -> head == NULL){
            printk("device_ioctl - tmp_message_slot -> head = NULL\n");
            tmp_message_slot -> head = tmp_node;
        }
        else{
            printk("device_ioctl - tmp_message_slot -> head != NULL\n"
                   "There are currently %d open channels for minor %d\n", tmp_message_slot -> num_of_nodes,
                   tmp_node -> channel_id);

            // go the last element in the list and make his next node point to the new node
            ptr = tmp_message_slot -> head;
            for(i = 0; i < tmp_message_slot -> num_of_nodes - 1; i++)
                ptr = ptr -> next;
            ptr -> next = tmp_node;
        }

        tmp_message_slot -> num_of_nodes++;
    }
    else{
        printk("device_ioctl - channel id exists\n");
    }

    // set the current channel id
    file -> private_data = tmp_node;

    return 0;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
        {
                .owner	        = THIS_MODULE,
                .read           = device_read,
                .write          = device_write,
                .open           = device_open,
                .unlocked_ioctl = device_ioctl,
                .release        = device_release,
        };

// Initialize the module - Register the character device
static int simple_init(void){
    int   status;
    int   i;

    printk("-----------------------------------------------------------------\n");

    status = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops);

    if(status < 0){
        printk(KERN_ERR "%s registration failed for %d\n", DEVICE_FILE_NAME, MAJOR_NUM);
        return status;
    }

    printk( "Registration is successful.\n");

    for(i = 0; i < 256; i++){
        message_slots[i] = NULL;
    }

    return 0;
}

void free_list(node* n){
    if(n == NULL)
        return;

    free_list(n -> next);
    kfree(n);
}

static void __exit simple_cleanup(void){
    int   i;

    for(i = 0; i < 256; i++){
        if(message_slots[i] != NULL){
            free_list(message_slots[i] -> head);
            kfree(message_slots[i]);
        }
    }

    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);

    printk("unloaded module message_slot\n");
    printk("-----------------------------------------------------------------\n");
}

module_init(simple_init);
module_exit(simple_cleanup);

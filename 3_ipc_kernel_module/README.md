The goal of this assignment is to gain experience with kernel programming and, particularly, a better
understanding on the design and implementation of inter-process communication (IPC), kernel
modules, and drivers.
In this assignment, you will implement a kernel module that provides a new IPC mechanism,
called a message slot. A message slot is a character device file through which processes communicate.
A message slot device has multiple message channels active concurrently, which can be used by
multiple processes. After opening a message slot device file, a process uses ioctl() to specify the
id of the message channel it wants to use. It subsequently uses read()/write() to receive/send
messages on the channel. In contrast to pipes, a message channel preserves a message until
it is overwritten, so the same message can be read multiple times.

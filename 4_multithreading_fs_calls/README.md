The goal of this assignment is to gain experience with threads and filesystem system calls. In this
assignment, you will create a program that searches a directory tree for files whose name matches
some search term. The program receives a directory D and a search term T, and finds every file
in D’s directory tree whose name contains T. The program parallelizes its work using threads.
Specifically, individual directories are searched by different threads.

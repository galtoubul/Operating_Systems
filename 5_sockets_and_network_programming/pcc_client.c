#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

// ------------------------ GENERAL ASSISTANCE FUNCTIONS ------------------------ //

int
validate_cli_args (int argc, char** argv)
{
        if (argc != 4) {
                fprintf(stderr, "Error: wrong number of CLI args.\n");
                return -1;
        }
        return 0;
}

uint32_t
get_file_size (char* file_name)
{
        struct stat st;
        if (stat (file_name, &st)) {
                fprintf (stderr, "Error: stat failed. %s\n", strerror (errno));
                return -1;
        }
        return st.st_size;
}

int
file_to_buffer (char* send_buffer, FILE* file, int file_size)
{
        size_t bytes_read = fread (send_buffer, sizeof (char), file_size, file);
        if (bytes_read != file_size) {
                fprintf (stderr, "Error: fread failed. %s\n", strerror (errno));
                return -1;
        }
        return 0;
}

// ------------------------ SOCKETS ASSISTANCE FUNCTIONS ------------------------ //

// Was inspired by: https://stackoverflow.com/questions/9140409/transfer-integer-over-a-socket-in-c
int
send_uint32 (uint32_t num, int sock_fd)
{
        uint32_t network_num = htonl (num);
        char* buffer = (char*) &network_num;
        size_t not_written = sizeof (uint32_t);
        int bytes_written;

        while (not_written > 0) {
                bytes_written = send (sock_fd, buffer, not_written, 0);
                if (bytes_written < 0) {
                        fprintf (stderr, "Error: send failed. %s\n", strerror(errno));
                        return -1;
                } else {
                        not_written -= bytes_written;
                        buffer += bytes_written;
                }
        }

        return 0;
}

// Was inspired by: https://stackoverflow.com/questions/9140409/transfer-integer-over-a-socket-in-c
int
receive_uint32 (uint32_t* num, int sock_fd)
{
        uint32_t host_num;
        char* buffer = (char*) &host_num;
        size_t not_read = sizeof (uint32_t);
        int bytes_read;

        while (not_read > 0) {
                bytes_read = recv (sock_fd, buffer, not_read, 0);
                if (bytes_read < 0) {
                        fprintf (stderr, "Error: recv failed. %s\n", strerror(errno));
                        return -1;
                } else {
                        not_read -= bytes_read;
                        buffer += bytes_read;
                }
        }
        *num = ntohl (host_num);
        return 0;
}

int
serv_addr_init (struct sockaddr_in* serv_addr, socklen_t addr_size, char* port, const char* ip)
{
        memset (serv_addr, 0, addr_size);
        serv_addr -> sin_family = AF_INET;
        serv_addr -> sin_port = htons (atoi (port));
        int rc = inet_pton (AF_INET, ip, &(serv_addr -> sin_addr));
        if (rc <= 0) {
                if (rc == 0)
                        fprintf (stderr, "Error: given ip isn't a valid ip in IPv4 family\n");
                else
                        fprintf (stderr, "Error: %s\n", strerror (errno));
                return -1;
        }
        return 0;
}

int
create_and_setup_socket (struct sockaddr_in* serv_addr, int* sock_fd)
{
        *sock_fd = socket (AF_INET, SOCK_STREAM, 0);
        if (*sock_fd < 0) {
                fprintf (stderr, "Error: Could not create socket: %s.\n", strerror (errno));
                return -1;
        }

        if (connect (*sock_fd, (struct sockaddr*) serv_addr, sizeof (*serv_addr))) {
                fprintf(stderr, "Error: Connect Failed. %s\n", strerror(errno));
                return -1;
        }

        return 0;
}

int
send_N_bytes (FILE* file, int file_size, int sock_fd)
{
        char* send_buffer = malloc (sizeof (char) * file_size);
        if (file_to_buffer (send_buffer, file, file_size))
                return -1;

        uint32_t not_sent = file_size;
        uint32_t bytes_sent;
        int offset = 0;

        while (not_sent > 0) {
                bytes_sent = send (sock_fd, send_buffer + offset, not_sent, 0);
                if(bytes_sent <= 0) {
                        fprintf (stderr, "Error: send failed. %s\n", strerror(errno));
                        return -1;
                } else {
                        not_sent -= bytes_sent;
                        offset += bytes_sent;
                }
        }

        free (send_buffer);
        return 0;
}

// Sends N bytes and receives the pcc for them
int
send_N_recv_C (FILE* file, int N, int sock_fd)
{
        if (send_N_bytes (file, N, sock_fd))
                return -1;

        uint32_t C;
        if (receive_uint32 (&C, sock_fd))
                return -1;

        printf("# of printable characters: %u\n", C);
        return 0;
}

// ------------------------ MAIN ------------------------ //

/**
 * argv[1] = server IP address
 * argv[2] = server's port
 * argv[3] = path of the file to send
 */
int
main (int argc, char **argv)
{
        if (validate_cli_args(argc, argv))
                exit (1);

        FILE* file = fopen (argv[3], "r");
        if (file == NULL) {
                fprintf (stderr, "Error: %s.\n", strerror (errno));
                exit (1);
        }

        struct sockaddr_in serv_addr;
        socklen_t addr_size = sizeof (struct sockaddr_in);
        if (serv_addr_init (&serv_addr, addr_size, argv[2], argv[1]))
                exit (1);

        int sock_fd;
        if (create_and_setup_socket (&serv_addr, &sock_fd))
                exit (1);

        uint32_t N = get_file_size (argv[3]);
        if (send_uint32 (N, sock_fd))
                exit (1);

        if (send_N_recv_C (file, N, sock_fd))
                exit (1);

        close(sock_fd);
        return 0;
}

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <stdatomic.h>

// ------------------------ DEFINITIONS ------------------------ //

#define LISTEN_QUEUE_SIZE 10
#define PCC_NUM           95
#define OUTSIDE_ACCEPT    (-3)
#define UNINITIALIZED     (-1)
#define DONT_EXIT_ERR     (-2)

// ------------------------ GLOBAL VARIABLES ------------------------ //

uint32_t hist [PCC_NUM];
int sigint_accepted = 0;
atomic_int accept_fd = UNINITIALIZED;

// ------------------------ GENERAL ASSISTANCE FUNCTIONS ------------------------ //

int
validate_cli_args (int argc, char** argv)
{
        if (argc != 2) {
                fprintf(stderr, "Error: wrong number of CLI args.\n");
                return -1;
        }
        return 0;
}

uint32_t
calc_pcc(uint32_t N, char* recv_buffer)
{
        uint32_t pcc = 0;
        for (int i = 0; i < N; ++i) {
                if (recv_buffer[i] >= 32 && recv_buffer[i] <= 126)
                        pcc++;
        }
        return pcc;
}

void
update_hist (uint32_t N, char* recv_buffer)
{
        for (int i = 0; i < N; ++i) {
                if (recv_buffer[i] >= 32 && recv_buffer[i] <= 126)
                        hist[recv_buffer[i] - 32]++;
        }
}

void
print_hist ()
{
        for (int i = 0; i < PCC_NUM; ++i)
                printf ("char '%c' : %u times\n", i + 32, hist[i]);
}

// ------------------------ SOCKETS ASSISTANCE FUNCTIONS ------------------------ //

int
check_for_TCP_error (int err_num)
{
        return (err_num == ETIMEDOUT || err_num == ECONNRESET || err_num == EPIPE);
}

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
                        if (check_for_TCP_error(errno))
                                return DONT_EXIT_ERR;
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
                if (bytes_read == 0) {
                        fprintf (stderr, "Error: client shut down earlier than expected.\n");
                        return DONT_EXIT_ERR;
                } else if (bytes_read < 0 || check_for_TCP_error(errno)) {
                        fprintf (stderr, "Error: recv failed. %s\n", strerror(errno));
                        if(check_for_TCP_error(errno))
                                return DONT_EXIT_ERR;
                        return -1;
                } else {
                        not_read -= bytes_read;
                        buffer += bytes_read;
                }
        }
        *num = ntohl (host_num);
        return 0;
}

int recv_N_bytes (uint32_t N, char* recv_buffer, int accept_fd)
{
        memset (recv_buffer, 0, N);
        uint32_t bytes_read;
        uint32_t not_read = N;
        int offset = 0;

        while (not_read > 0) {
                bytes_read = recv (accept_fd, recv_buffer + offset, not_read, 0);
                if (bytes_read == 0) {
                        fprintf (stderr, "Error: client shut down earlier than expected.\n");
                        return DONT_EXIT_ERR;
                } else if(bytes_read < 0 || check_for_TCP_error(errno)) {
                        fprintf (stderr, "Error: recv failed. %s\n", strerror(errno));
                        if (check_for_TCP_error(errno))
                                return DONT_EXIT_ERR;
                        return -1;
                } else {
                        not_read -= bytes_read;
                        offset += bytes_read;
                }
        }

        return 0;
}

// Receives N (the file size that should be sent) bytes
// Calculates the pcc for the N bytes and send it
// Updates the DS for holding the counters of the printable chars
int
recv_N_send_pcc (int accept_fd)
{
        int rc;
        uint32_t N;
        if ((rc = receive_uint32 (&N, accept_fd)) < 0)
                return rc;

        char* recv_buffer = malloc (sizeof (char) * N);
        if ((rc = recv_N_bytes (N, recv_buffer, accept_fd)) < 0) {
                free(recv_buffer);
                return rc;
        }

        uint32_t C = calc_pcc(N, recv_buffer);
        if ((rc = send_uint32 (C, accept_fd)) < 0) {
                free(recv_buffer);
                return rc;
        }

        update_hist (N, recv_buffer);
        free(recv_buffer);
        return 0;
}

void
serv_addr_init (struct sockaddr_in* serv_addr, socklen_t addr_size, char* port)
{
        memset (serv_addr, 0, addr_size);
        serv_addr -> sin_family = AF_INET;
        serv_addr -> sin_addr.s_addr = htonl (INADDR_ANY);
        serv_addr -> sin_port = htons (atoi (port));
}

int
create_and_setup_socket (struct sockaddr_in* serv_addr, socklen_t* addr_size, int *listen_fd)
{
        *listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (*listen_fd < 0) {
                fprintf (stderr, "Error: Could not create socket: %s.\n", strerror (errno));
                return -1;
        }

        int reuse = 1;
        if (setsockopt (*listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof (reuse))) {
                fprintf (stderr, "Error: Could not set socket: %s.\n", strerror (errno));
                return -1;
        }

        if (bind (*listen_fd, (struct sockaddr*) serv_addr, *addr_size)){
                fprintf (stderr, "Error: Bind Failed. %s\n", strerror(errno));
                return -1;
        }

        if (listen (*listen_fd, LISTEN_QUEUE_SIZE)){
                fprintf (stderr,"Error: Listen Failed. %s\n", strerror(errno));
                return -1;
        }

        return 0;
}

// ------------------------ SIGINT HANDLER ASSISTANCE FUNCTIONS ------------------------ //

void
sigint_handler (int signum)
{
        // accept_fd == OUTSIDE_ACCEPT only if sigint was sent outside of accept () ... close (sock_fd) scope
        if (accept_fd == OUTSIDE_ACCEPT) {
                print_hist ();
                _exit (0);
        } else {
                sigint_accepted = 1; // give the socket the time to finish and then exit
        }
}

int
set_sigint_handler ()
{
        struct sigaction sa;
        memset (&sa, 0, sizeof (sa));
        sa.sa_handler = sigint_handler;
        sa.sa_flags = SA_RESTART;

        if (sigaction (SIGINT, &sa, NULL) != 0) {
                fprintf (stderr,"Error: sigaction Failed. %s\n", strerror(errno));
                return -1;
        }

        return 0;
}

int
check_for_sigint ()
{
        if (sigint_accepted) {
                print_hist();
                return -1;
        }

        accept_fd = OUTSIDE_ACCEPT;
        return 0;
}

// ------------------------ MAIN ------------------------ //

/**
 * argv[1] = server's port
 */
int
main (int argc, char **argv)
{
        set_sigint_handler();
        if (validate_cli_args(argc, argv))
                exit (1);

        struct sockaddr_in serv_addr;
        struct sockaddr_in peer_addr;
        socklen_t addr_size = sizeof (struct sockaddr_in);
        serv_addr_init (&serv_addr, addr_size, argv[1]);

        int listen_fd;
        if (create_and_setup_socket (&serv_addr, &addr_size, &listen_fd))
                exit (1);

        while (1) {
                accept_fd = accept (listen_fd, (struct sockaddr*) &peer_addr, &addr_size);
                if (accept_fd < 0) {
                        fprintf (stderr, "Error: Accept Failed. %s\n", strerror(errno));
                        if (check_for_TCP_error(errno))
                                continue;
                        exit (1);
                }

                int rc = recv_N_send_pcc (accept_fd);
                if (rc < 0 && rc != DONT_EXIT_ERR)
                        exit (1);

                close (accept_fd);
                if (check_for_sigint ())
                        exit (0);
        }
}

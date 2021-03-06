#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "client.h"

int create_pub_key()
{
    curve25519_donna(public_key, private_key, basepoint);
}

int init_client(int port, char* hostname)
{
    struct hostent *hostinfo;
    struct sockaddr_in address;
    int sockfd;

#ifdef USE_ENCRYPTION
    create_pub_key();
#endif /* USE_ENCRYPTION */

    debug_printf("* Client program starting\n");

    /* Create a socket for the client */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Name the socket, as agreed with the server */
    hostinfo = gethostbyname(hostname); /* look for host's name */
    address.sin_addr = *(struct in_addr *)*hostinfo -> h_addr_list;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    /* Connect the socket to the server's socket */
    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connecting");
        exit(1);
    }

    debug_printf("* Connected to server\n");

    return sockfd;
}

void send_pal_input(int fd)
{
    FILE* pal_input;
    packet_t* client_packet;
    char msg[MAX_PAYLOAD];
    int num_items;

    debug_printf("* Sending PAL input to server\n");

    pal_input = fopen(pal_input_name, "rb");

    while(!feof(pal_input)) {
        memset(msg, '\0', MAX_PAYLOAD * sizeof(char));
        num_items = fread(msg, sizeof(char), MAX_PAYLOAD, pal_input);
        // fgets(msg, sizeof(char) * MAX_PAYLOAD, pal_input);

        client_packet = (packet_t*)calloc(1, sizeof(packet_t));

        client_packet->hdr_type = CLIENT_SEND_INPUT;
        client_packet->payload_len = num_items * sizeof(char);
        memcpy(client_packet->payload, msg, client_packet->payload_len);

        send_packet(fd, client_packet, shared_key);
        free(client_packet);
    }

    fclose(pal_input);
}

void execute_pal(int fd)
{
    packet_t* packet;

    packet = (packet_t*)calloc(1, sizeof(packet_t));
    packet->hdr_type = CLIENT_EXEC_PAL;

    send_packet(fd, packet, shared_key);
    free(packet);
}

void init_execution(int fd)
{
    FILE* pal;
    packet_t* client_packet;
    char msg[MAX_PAYLOAD];
    int num_items;

    debug_printf("* Sending PAL to server\n");

    pal = fopen(pal_name, "rb");

    while(!feof(pal)) {
        memset(msg, '\0', MAX_PAYLOAD * sizeof(char));
        num_items = fread(msg, sizeof(char), MAX_PAYLOAD, pal);

        client_packet = (packet_t*)calloc(1, sizeof(packet_t));

        client_packet->hdr_type = CLIENT_SEND_PAL;
        client_packet->payload_len = num_items * sizeof(char);
        memcpy(client_packet->payload, msg, client_packet->payload_len);

        send_packet(fd, client_packet, shared_key);
        free(client_packet);
    }

    fclose(pal);

    send_pal_input(fd);

    execute_pal(fd);
}

void handle_server_ready(int fd, packet_t* packet, int len)
{
#ifdef USE_ENCRYPTION
    packet_t* client_packet;

    debug_printf("* Sending public key to server\n");

    client_packet = (packet_t*)calloc(1, sizeof(packet_t));

    client_packet->hdr_type = CLIENT_SEND_PUBKEY;
    client_packet->payload_len = CRYPT_SIZE;
    memcpy(client_packet->payload, public_key, CRYPT_SIZE);

    send_packet(fd, client_packet, shared_key);
    free(client_packet);

#else
    init_execution(fd);
#endif /* USE_ENCRYPTION */
}

void handle_server_pub_key(int fd, packet_t* packet, int len)
{
#ifdef USE_ENCRYPTION
    int i;

    /* Compute the shared key */
    curve25519_donna(shared_key, private_key, packet->payload);

    debug_printf("* Shared key: ");
    for (i = 0;i < CRYPT_SIZE;i++) {
        debug_printf("%02x ", (unsigned int) shared_key[i]);
    }
    debug_printf("\n");

    init_execution(fd);
#endif /* USE_ENCRYPTION */
}

void handle_server_pal_result(int fd, packet_t* packet, int len)
{
    FILE* f;

    f = fopen(PAL_RESULT_NAME, "a");
    fprintf(f, "%s", packet->payload);
    fclose(f);
}

void handle_server_finish_pal(int fd, packet_t* packet, int len)
{
    debug_printf("* Finished receiving PAL result\n");

#ifdef LOGGING
    log_stuff("Program end");
#endif /* LOGGING */

    // exit(0);
}

void __attribute__((noreturn)) client_process(int sockfd)
{
    int fd;
    fd_set clientfds, testfds;
    packet_t packet;
    int len;

    FD_ZERO(&clientfds);
    FD_SET(sockfd, &clientfds);

    /* Now wait for messages from the server */
    while (1) {
        testfds = clientfds;

        select(FD_SETSIZE, &testfds, NULL, NULL, NULL);

        if (FD_ISSET(sockfd, &clientfds)) {
            memset((void*)&packet, '\0', sizeof(packet_t));
            len = recv_packet(sockfd, &packet, shared_key);

            handle_msg[packet.hdr_type](sockfd, &packet, len);
        }
    }
}

int main(int argc, char *argv[])
{
    int port;
    char hostname[100];
    int sockfd;

#ifdef LOGGING
    log_stuff("Program begin");
#endif /* LOGGING */

    port = 7500;
    strcpy(hostname, "127.0.0.1");
    strcpy(pal_name, "../../flicker-0.5/pal/pal.bin");

    // port = atoi(argv[1]);
    // strcpy(hostname,argv[2]);
    strcpy(pal_name, argv[1]);
    strcpy(pal_input_name, argv[2]);

    sockfd = init_client(port, hostname);

    client_process(sockfd);
}

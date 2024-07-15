#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_CLIENTS 3
#define MAX_DESTINATIONS 3
#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct {
    int source_id;
    int broadcast; // 0 for non-broadcast, 1 for broadcast
    int destination_num;
    int destination_id[MAX_DESTINATIONS];   // Fixed-size array
    int ack; // 0 for normal message, 1 for ACK
    char buffer[BUFFER_SIZE]; 
} message_t;

int client_sockets[MAX_CLIENTS];
int client_ids[MAX_CLIENTS];
int client_id_counter = 1;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF; // Initial value
    while (length--) {
        crc ^= (*data++ << 8);
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void send_data(int sock, const uint8_t *data, size_t length) {
    uint8_t buffer[BUFFER_SIZE];
    uint16_t crc = crc16_ccitt(data, length);
    memcpy(buffer, data, length);
    crc = htons(crc); // Ensure CRC is in network byte order
    memcpy(buffer + length, &crc, sizeof(crc));
    write(sock, buffer, length + sizeof(crc));
    printf("Data sent: ");
    for (size_t i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }
    printf("CRC: %04X\n", ntohs(crc));
}

int receive_data(int sock, uint8_t *data, size_t length) {
    uint8_t buffer[BUFFER_SIZE];
    uint16_t received_crc;
    int ret = read(sock, buffer, length + sizeof(received_crc));
    if (ret < 0) {
        return ret;
    }
    if (ret < length + sizeof(received_crc)) {
        // Not enough data received
        return -1;
    }

    memcpy(data, buffer, length);
    memcpy(&received_crc, buffer + length, sizeof(uint16_t)); 
    received_crc = ntohs(received_crc);

    uint16_t computed_crc = crc16_ccitt(data, length);

    if (computed_crc == received_crc) {
        printf("Data received correctly.\n");
    } else {
        printf("Data corrupted. Received CRC: %04X, Computed CRC: %04X\n", received_crc, computed_crc);
    }
    return ret;
}
void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    message_t msg;
    int bytes_read;
    int assigned_id;

    // Send assigned ID to client
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_sockets[i] == client_socket) {
            assigned_id = client_ids[i];
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    while ((bytes_read = receive_data(client_socket, (uint8_t *)&msg, sizeof(msg))) > 0) {
        msg.buffer[BUFFER_SIZE - 1] = '\0'; // Ensure null termination

        // Relay message to the specified destination client
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (client_sockets[i] != 0 && client_sockets[i] != client_socket) {
                for (int j = 0; j < msg.destination_num; ++j) {
                    if (msg.destination_id[j] == -1 || msg.destination_id[j] == client_ids[i]) { // Broadcast if destination_id is -1
                        //write(client_sockets[i], &msg, sizeof(msg));
                        send_data(client_sockets[i], (uint8_t *)&msg, sizeof(msg));

                        // Send ACK to source client
                        if (msg.ack == 0) { // Only send ACK for normal messages
                            message_t ack_msg;
                            ack_msg.source_id = msg.destination_id[j];
                            ack_msg.broadcast = 0;
                            ack_msg.destination_num = 1;
                            ack_msg.destination_id[j] = msg.source_id;
                            ack_msg.ack = 1; // This is an ACK message
                            snprintf(ack_msg.buffer, BUFFER_SIZE, "ACK: Message received by %d", msg.destination_id[j]);
                            //write(client_socket, &ack_msg, sizeof(ack_msg));
                            send_data(client_socket, (uint8_t *)&ack_msg, sizeof(ack_msg));
                        }
                    }
                }
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    // Remove client from list
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_sockets[i] == client_socket) {
            client_sockets[i] = 0;
            client_ids[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(client_socket);
    free(arg);
    pthread_exit(NULL);
}

int main() {
    int server_socket, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize clients sockets array
    memset(client_sockets, 0, sizeof(client_sockets));
    memset(client_ids, 0, sizeof(client_ids));

    if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if(bind(server_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if(listen(server_socket, 3) == -1) {
        perror("listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Relay server listening on port %d\n", PORT);

    while(1) {
        if((new_socket = accept(server_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) == -1) {
            perror("accept failed");
            continue;
        }

        pthread_mutex_lock(&clients_mutex);
        for(int i = 0; i < MAX_CLIENTS; ++i) {
            if(client_sockets[i] == 0) {
                client_sockets[i] = new_socket;
                client_ids[i] = client_id_counter++;
                // Send assigned ID to client
                //write(new_socket, &client_ids[i], sizeof(int));
                send_data(new_socket, (uint8_t *)&client_ids[i], sizeof(int));
                pthread_t tid;
                int *new_sock = malloc(sizeof(int));
                *new_sock = new_socket;
                pthread_create(&tid, NULL, handle_client, (void *)new_sock);
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    return 0;
}

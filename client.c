#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_DESTINATIONS 3
#define MAX_NUMBERS 2
#define MAX_INPUT_SIZE 99
#define PORT 8080
#define BUFFER_SIZE 1024

int client_id;

typedef struct {
    int source_id;
    int broadcast; // 0 for non-broadcast, 1 for broadcast
    int destination_num;
    int destination_id[MAX_DESTINATIONS];   // Fixed-size array
    int ack; // 0 for normal message, 1 for ACK
    char buffer[BUFFER_SIZE];
} message_t;

void *receive_messages(void *arg) {
    int sock = *((int *)arg);
    message_t msg;
    int bytes_read;

    while((bytes_read = read(sock, &msg, sizeof(msg))) > 0) {
        msg.buffer[BUFFER_SIZE - 1] = '\0'; // Ensure null termination
        printf("Received from %d: %s\n", msg.source_id, msg.buffer);
    }

    if (bytes_read == 0) {
        printf("Server closed connection\n");
    } else {
        perror("recv");
    }

    close(sock);
    return NULL;
}

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
    send(sock, buffer, length + sizeof(crc), 0);
    printf("Data sent: ");
    for (size_t i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }
    printf("CRC: %04X\n", ntohs(crc));
}

int main() {
    struct sockaddr_in address;
    int sock = 0;
    struct sockaddr_in serv_addr;
    pthread_t recv_thread;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        exit(EXIT_FAILURE);
    }

    // Receive assigned ID from server
    read(sock, &client_id, sizeof(int));
    printf("Connected to server with client ID: %d\n", client_id);

    // 메시지 수신 스레드 시작
    if (pthread_create(&recv_thread, NULL, receive_messages, &sock) != 0) {
        perror("Could not create thread");
        exit(EXIT_FAILURE);
    }

    /**/
    message_t msg;
    msg.source_id = 1; // 클라이언트의 ID 설정
    msg.ack = 0;       // 일반 메시지

    while (1) {

        char input[MAX_INPUT_SIZE];    // Array to store the input string
        int numbers[MAX_NUMBERS];      // Array to store the numbers
        int num_count = 0;             // Counter for the number of IDs

        printf("Enter destination IDs separated by commas (-1 for broadcast): ");
        if (fgets(input, MAX_INPUT_SIZE, stdin) != NULL) {
            // Remove the newline character if present
            input[strcspn(input, "\n")] = '\0';

            // Parse the input string
            char *token = strtok(input, ",");
            while (token != NULL && num_count < MAX_NUMBERS) {
                numbers[num_count++] = atoi(token);
                token = strtok(NULL, ",");
            }

            // Print the parsed numbers for verification
            printf("Parsed destination IDs:\n");
            for (int i = 0; i < num_count; ++i) {
                printf("%d\n", numbers[i]);
            }
        } else {
            printf("Error reading input.\n");
        }

        msg.source_id = client_id;
        msg.ack = 0;

        if (msg.destination_id[0] == -1) {
            msg.broadcast = 1;
            msg.destination_num = 0;
        } else {
            msg.broadcast = 0;
            msg.destination_num = num_count > MAX_DESTINATIONS ? MAX_DESTINATIONS : num_count;
            memcpy(msg.destination_id, &numbers[0], sizeof(msg.destination_id));
        }

        printf("Enter message: ");
        scanf(" %[^\n]", msg.buffer);

        // 서버로 메시지 전송
        if (write(sock, &msg, sizeof(msg)) < 0) {
            perror("Send failed");
            break;
        }
    }

    // 스레드 종료를 기다림
    pthread_join(recv_thread, NULL);

    close(sock);
    return 0;
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dll_api.h"

#define BUF_SIZE 256

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <SerialPort> <FileToSend>\n", argv[0]);
        return 1;
    }

    const char *serialPort = argv[1];
    const char *filename = argv[2];
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("File open error");
        return 1;
    }

    printf("Attempting to open connection on port: %s\n", serialPort);
    int fd = llopen(serialPort, 1);
    if (fd < 0) {
        printf("Failed to open link\n");
        return 1;
    }
    printf("Connection opened successfully.\n");

    // START packet
    unsigned char startPacket[BUF_SIZE];
    int fn_len = strlen(filename);
    startPacket[0] = 2;
    startPacket[1] = 0; startPacket[2] = sizeof(int);
    int fsize;
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    memcpy(&startPacket[3], &fsize, sizeof(int));
    startPacket[3 + sizeof(int)] = 1;
    startPacket[4 + sizeof(int)] = fn_len;
    memcpy(&startPacket[5 + sizeof(int)], filename, fn_len);
    printf("Sending START packet...\n");
    llwrite(fd, startPacket, 5 + sizeof(int) + fn_len);

    // DATA packets
    unsigned char dataPacket[BUF_SIZE];
    int seq = 0, bytesRead;
    while ((bytesRead = fread(&dataPacket[4], 1, BUF_SIZE - 4, file)) > 0) {
        dataPacket[0] = 1;
        dataPacket[1] = seq++ % 256;
        dataPacket[2] = bytesRead / 256;
        dataPacket[3] = bytesRead % 256;
        printf("Sending DATA packet #%d (%d bytes)\n", seq - 1, bytesRead);
        llwrite(fd, dataPacket, 4 + bytesRead);
    }

    // END packet
    unsigned char endPacket[BUF_SIZE];
    endPacket[0] = 3;
    memcpy(endPacket + 1, startPacket + 1, 4 + sizeof(int) + fn_len);
    printf("Sending END packet...\n");
    llwrite(fd, endPacket, 5 + sizeof(int) + fn_len);

    fclose(file);
    printf("Closing connection...\n");
    llclose(fd);
    return 0;
}

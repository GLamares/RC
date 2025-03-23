
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dll_api.h"

#define BUF_SIZE 256

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <SerialPort>\n", argv[0]);
        return 1;
    }

    const char *serialPort = argv[1];
    printf("Attempting to open connection on port: %s\n", serialPort);
    int fd = llopen(serialPort, 0);
    if (fd < 0) {
        printf("Failed to open link\n");
        return 1;
    }
    printf("Connection opened successfully.\n");

    unsigned char buffer[BUF_SIZE];
    char fileName[256];
    FILE *file = NULL;
    int expectedSeq = 0;

    while (1) {
        int len = llread(fd, buffer);
        printf("Received frame of length: %d\n", len);
        if (len <= 0) continue;

        if (buffer[0] == 2) { // START
            int fileSize;
            memcpy(&fileSize, &buffer[3], sizeof(int));
            int fn_len = buffer[3 + sizeof(int) + 1];
            memcpy(fileName, &buffer[5 + sizeof(int)], fn_len);
            fileName[fn_len] = '\0';
            printf("Opening file for writing: %s (%d bytes expected)\n", fileName, fileSize);
            file = fopen(fileName, "wb");
        }
        else if (buffer[0] == 1) { // DATA
            int seqNum = buffer[1];
            if (seqNum == expectedSeq) {
                int size = buffer[2] * 256 + buffer[3];
                printf("Writing %d bytes to file (DATA seq #%d)\n", size, seqNum);
                fwrite(&buffer[4], 1, size, file);
                expectedSeq = (expectedSeq + 1) % 256;
            } else {
                printf("Unexpected sequence number: got %d, expected %d\n", seqNum, expectedSeq);
            }
        }
        else if (buffer[0] == 3) { // END
            printf("END packet received. File transfer complete.\n");
            break;
        }
    }

    if (file) fclose(file);
    llclose(fd);
    return 0;
}

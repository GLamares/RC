
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dll_api.h"

#define BUF_SIZE 256

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <SerialPort> <OutputFileName>\n", argv[0]);
        return 1;
    }

    const char *serialPort = argv[1];
    const char *outputFile = argv[2];
    printf("Receiver started. Waiting on port %s...\n", serialPort);

    int fd = llopen(serialPort, 0);
    if (fd < 0) {
        printf("Failed to open link\n");
        return 1;
    }

    FILE *out = fopen(outputFile, "wb");
    if (!out) {
        perror("Error creating output file");
        return 1;
    }
    printf("Opened '%s' for writing.\n", outputFile);

    unsigned char buffer[BUF_SIZE];
    int expectedSeq = 0;
    int totalWritten = 0;

    while (1) {
        int len = llread(fd, buffer);
        printf("Received frame of length: %d\n", len);
        if (len <= 0) continue;

        if (buffer[0] == 1) { // DATA
            int seqNum = buffer[1];
            if (seqNum == expectedSeq) {
                int size = buffer[2] * 256 + buffer[3];
                int written = fwrite(&buffer[4], 1, size, out);
                totalWritten += written;
                printf("Writing %d bytes to %s (seq %d)\n", written, outputFile, seqNum);
                expectedSeq = (expectedSeq + 1) % 256;
            } else {
                printf("Unexpected seq: got %d, expected %d\n", seqNum, expectedSeq);
            }
        }
        else if (buffer[0] == 3) { // END
            printf("END packet received. Transfer complete.\n");
            break;
        }
    }

    fclose(out);
    llclose(fd);
    printf("Transfer finished. Wrote %d total bytes to %s\n", totalWritten, outputFile);
    return 0;
}

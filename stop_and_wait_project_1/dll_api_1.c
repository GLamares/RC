
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdbool.h>
#include "dll_api.h"

#define FLAG 0x7E
#define A_SENDER 0x03
#define A_RECEIVER 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define C_RR_0 0x05
#define C_RR_1 0x85
#define C_REJ_0 0x01
#define C_REJ_1 0x81
#define C_I_0 0x00
#define C_I_1 0x40

#define MAX_RETRIES 3
#define TIMEOUT 3
#define BUF_SIZE 256

int alarmCount = 0;
bool alarmEnabled = false;
int currentNs = 0;
int serial_fd;

void alarm_handler(int signo) {
    alarmEnabled = false;
    alarmCount++;
}

unsigned char calculateBCC2(const unsigned char* data, int length) {
    unsigned char bcc2 = 0x00;
    for (int i = 0; i < length; i++) {
        bcc2 ^= data[i];
    }
    return bcc2;
}

void sendSupervisionFrame(int fd, unsigned char A, unsigned char C) {
    unsigned char frame[5] = {FLAG, A, C, A ^ C, FLAG};
    write(fd, frame, 5);
    printf("Sent supervision frame: A=0x%02X, C=0x%02X\n", A, C);
}

int readSupervisionFrame(int fd, unsigned char expectedC) {
    unsigned char buf;
    int state = 0;
    unsigned char A, C, BCC;
    while (1) {
        if (read(fd, &buf, 1) <= 0) continue;
        switch (state) {
            case 0: if (buf == FLAG) state = 1; break;
            case 1: if (buf == A_RECEIVER || buf == A_SENDER) { A = buf; state = 2; } else if (buf != FLAG) state = 0; break;
            case 2: C = buf; state = 3; break;
            case 3: BCC = A ^ C; if (buf == BCC) state = 4; else state = 0; break;
            case 4: if (buf == FLAG && C == expectedC) return 1; else return 0;
        }
    }
}

int llopen(const char* port, int isTransmitter) {
    struct termios newtio;
    serial_fd = open(port, O_RDWR | O_NOCTTY);
    if (serial_fd < 0) return -1;

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 1;

    tcflush(serial_fd, TCIOFLUSH);
    if (tcsetattr(serial_fd, TCSANOW, &newtio) != 0) return -1;

    signal(SIGALRM, alarm_handler);

    if (isTransmitter) {
        int attempts = 0;
        while (attempts < MAX_RETRIES) {
            sendSupervisionFrame(serial_fd, A_SENDER, C_SET);
            alarmEnabled = true;
            alarm(TIMEOUT);
            if (readSupervisionFrame(serial_fd, C_UA)) return serial_fd;
            while (alarmEnabled);
            attempts++;
        }
        return -1;
    } else {
        if (readSupervisionFrame(serial_fd, C_SET)) {
            sendSupervisionFrame(serial_fd, A_RECEIVER, C_UA);
            return serial_fd;
        }
        return -1;
    }
}

int llwrite(int fd, unsigned char* buffer, int length) {
    unsigned char frame[BUF_SIZE + 6];
    int controlNs = currentNs;
    unsigned char control = controlNs ? C_I_1 : C_I_0;
    frame[0] = FLAG;
    frame[1] = A_SENDER;
    frame[2] = control;
    frame[3] = A_SENDER ^ control;
    memcpy(&frame[4], buffer, length);
    unsigned char bcc2 = calculateBCC2(buffer, length);
    frame[4 + length] = bcc2;
    frame[5 + length] = FLAG;

    printf("Sending frame with Ns=%d and length=%d, BCC2=0x%02X\n", controlNs, length, bcc2);

    int attempts = 0;
    while (attempts < MAX_RETRIES) {
        write(fd, frame, length + 6);
        alarmEnabled = true;
        alarm(TIMEOUT);
        unsigned char buf;
        while (alarmEnabled && read(fd, &buf, 1) > 0) {
            if ((controlNs == 0 && buf == C_RR_1) || (controlNs == 1 && buf == C_RR_0)) {
                printf("Received RR for Ns=%d\n", controlNs);
                currentNs = 1 - controlNs;
                alarm(0);
                return length;
            } else if ((controlNs == 0 && buf == C_REJ_0) || (controlNs == 1 && buf == C_REJ_1)) {
                printf("Received REJ for Ns=%d\n", controlNs);
                break;
            }
        }
        alarmEnabled = false;
        attempts++;
    }
    return -1;
}

int llread(int fd, unsigned char* buffer) {
    unsigned char byte;
    int state = 0;
    unsigned char A, C, BCC1;
    int idx = 0;
    unsigned char data[BUF_SIZE];
    unsigned char expectedControl = currentNs ? C_I_1 : C_I_0;

    printf("Waiting for I-frame with Ns=%d\n", currentNs);

    while (1) {
        if (read(fd, &byte, 1) <= 0) continue;
        printf("Read byte: 0x%02X (state %d)\n", byte, state);
        switch (state) {
            case 0:
                if (byte == FLAG) state = 1;
                break;
            case 1:
                if (byte == A_SENDER) { A = byte; state = 2; }
                else if (byte != FLAG) state = 0;
                break;
            case 2:
                if (byte == expectedControl) { C = byte; state = 3; }
                else state = 0;
                break;
            case 3:
                if (byte == (A ^ C)) { BCC1 = byte; idx = 0; state = 4; }
                else state = 0;
                break;
            case 4:
                if (byte == FLAG) {
                    if (idx < 1) {
                        printf("Error: Frame too short\n");
                        return -1;
                    }
                    unsigned char received_bcc2 = data[idx - 1];
                    unsigned char calculated_bcc2 = calculateBCC2(data, idx - 1);
                    printf("BCC2 check: received=0x%02X, calculated=0x%02X\n", received_bcc2, calculated_bcc2);
                    if (calculated_bcc2 == received_bcc2) {
                        memcpy(buffer, data, idx - 1);
                        currentNs = 1 - currentNs;
                        sendIFrame(fd, A_RECEIVER, currentNs ? C_RR_1 : C_RR_0);
                        return idx - 1;
                    } else {
                        printf("BCC2 error: rejecting frame\n");
                        sendIFrame(fd, A_RECEIVER, currentNs ? C_REJ_1 : C_REJ_0);
                        return -1;
                    }
                } else {
                    if (idx < BUF_SIZE) {
                        data[idx++] = byte;
                        printf("Receiving data[%d] = 0x%02X\n", idx - 1, byte);
                    }
                }
                break;
        }
    }

    return -1;
}

int llclose(int fd) {
    int attempts = 0;
    while (attempts < MAX_RETRIES) {
        sendSupervisionFrame(fd, A_SENDER, C_DISC);
        alarmEnabled = true;
        alarm(TIMEOUT);
        if (readSupervisionFrame(fd, C_DISC)) {
            sendSupervisionFrame(fd, A_RECEIVER, C_UA);
            close(fd);
            printf("Connection closed successfully.\n");
            return 1;
        }
        while (alarmEnabled);
        attempts++;
    }
    return -1;
}

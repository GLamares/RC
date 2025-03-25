
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

#define ESC  0x7D//Usado para o Byte Stuffing
#define ESC_FLAG 0x5E
#define ESC_ESC  0x5D

int byteStuff(const unsigned char *input, int length, unsigned char *output) { //evita confusão na delimitação do frame
   
    int j = 0;
    for (int i = 0; i < length; i++) {
        if (input[i] == FLAG) {
            output[j++] = ESC;
            output[j++] = ESC_FLAG;
        } else if (input[i] == ESC) {
            output[j++] = ESC;
            output[j++] = ESC_ESC;
        } else {
            output[j++] = input[i];
        }
    }
    return j;
}

int byteDestuff(const unsigned char *input, int length, unsigned char *output) { //faz o inverso do stuffing, restaura os originais
    int j = 0;
    for (int i = 0; i < length; i++) {
        if (input[i] == ESC) {
            if (input[i + 1] == ESC_FLAG) {
                output[j++] = FLAG;
                i++;
            } else if (input[i + 1] == ESC_ESC) {
                output[j++] = ESC;
                i++;
            } else {
                return -1; // em caso de erro
            }
        } else {
            output[j++] = input[i];
        }
    }
    return j; //retorna o novo comprimento após o stuffing
}

int alarmCount = 0;
bool alarmEnabled = false;
int currentNs = 0;
int serial_fd;

void alarm_handler(int signo) {
    alarmEnabled = false;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

unsigned char calculateBCC2(const unsigned char* data, int length) {

    unsigned char bcc2 = 0x00;
    for (int i = 0; i < length; i++) {
        bcc2 ^= data[i];
    }
    return bcc2;
}

void sendSETFrame(int fd, unsigned char A, unsigned char C) {

    unsigned char frame[5] = {FLAG, A, C, A ^ C, FLAG};
    write(fd, frame, 5);
    printf("Sent SET frame\n");
}

void sendUAFrame(int fd, unsigned char A, unsigned char C) {

    unsigned char frame[5] = {FLAG, A, C, A ^ C, FLAG};
    write(fd, frame, 5);
    printf("Sent UA frame\n");
}

void sendIFrame(int fd, unsigned char A, unsigned char C) {

    unsigned char frame[5] = {FLAG, A, C, A ^ C, FLAG};
    write(fd, frame, 5);
    printf("Sent I frame\n");
}

void sendDiscFrame(int fd, unsigned char A, unsigned char C) {

    unsigned char frame[5] = {FLAG, A, C, A ^ C, FLAG};
    write(fd, frame, 5);
    printf("Sent Disc frame\n");
}

int readSupervisionFrame(int fd, unsigned char expectedC) {

    unsigned char buf;
    int state = 0;
    unsigned char A, C, BCC;
    while (1){

        if (read(fd, &buf, 1) <= 0) 
            continue;

        switch (state){

            case 0: 
                
            if (buf == FLAG)
                    state = 1; 
                
                break;

            case 1: 
                
                if (buf == A_RECEIVER || buf == A_SENDER){
                    A = buf; 
                    state = 2; 
                }
                
                else if (buf != FLAG)
                    state = 0; 
                
                break;
                
            case 2:

                C = buf; 
                state = 3; 
                break;

            case 3:
            
            BCC = A ^ C; 
            if (buf == BCC) 
                state = 4; 
            
            else 
                state = 0; 
            
            break;

            case 4: 

                if (buf == FLAG && C == expectedC) 
                    return 1; 
                
                else 
                    return 0;

                break;
        }
    }
}

int llopen(const char* port, int isTransmitter){

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

    if (isTransmitter){
    
        while (alarmCount < MAX_RETRIES){//ciclo para as retransmissões

            sendSETFrame(serial_fd, A_SENDER, C_SET);
            alarmEnabled = true;
            alarm(TIMEOUT);
            if (readSupervisionFrame(serial_fd, C_UA)){
                
                printf("UA recebido! Cancelando alarme.\n");
                alarm(0);
                alarmEnabled = false;
                return serial_fd;
                break;
            }

            if (!alarmEnabled && alarmCount < MAX_RETRIES){

                printf("Timeout #%d: Reenviando SET...\n", alarmCount);
                sendSETFrame(serial_fd, A_SENDER, C_SET);
                alarmEnabled = true;
                alarm(3);
            }
        }
        return -1;

    } 
    
    else{

        if (readSupervisionFrame(serial_fd, C_SET)){

            sendUAFrame(serial_fd, A_RECEIVER, C_UA);
            return serial_fd;
        }
        return -1;
    }
}

int llwrite(int fd, unsigned char* buffer, int length){

    unsigned char frame[BUF_SIZE + 6];
    unsigned char control;

    if(currentNs)
        control = C_I_1;
    
    else
        control = C_I_0;

    frame[0] = FLAG;
    frame[1] = A_SENDER;
    frame[2] = control;
    frame[3] = A_SENDER ^ control;
    unsigned char stuffed[BUF_SIZE * 2];
    int stuffed_len = byteStuff(buffer, length, stuffed);
    unsigned char bcc2 = calculateBCC2(buffer, length);
    unsigned char bcc2_stuffed[2];
    int bcc2_len = byteStuff(&bcc2, 1, bcc2_stuffed);
    memcpy(&frame[4], stuffed, stuffed_len); //Adiciona o bcc2 ao franme
    memcpy(&frame[4 + stuffed_len], bcc2_stuffed, bcc2_len);
    frame[4 + stuffed_len + bcc2_len] = FLAG;//fecha com a flag

    printf("Sending frame with Ns=%d and length=%d, BCC2=0x%02X\n", currentNs, length, bcc2);

    while (alarmCount < MAX_RETRIES){//retransmissão

        write(fd, frame, 5 + stuffed_len + bcc2_len);
        alarmEnabled = true;
        alarm(TIMEOUT);
        unsigned char buf;
        while (alarmEnabled && read(fd, &buf, 1) > 0){

            if ((currentNs == 0 && buf == C_RR_1) || (currentNs == 1 && buf == C_RR_0)){

                printf("Received RR for Ns=%d\n", currentNs);
                currentNs = 1 - currentNs;
                alarm(0);
                alarmEnabled = false;
                return length;
                break;
            } 
            
            else if ((currentNs == 0 && buf == C_REJ_0) || (currentNs == 1 && buf == C_REJ_1)){

                printf("Received REJ for Ns=%d\n", currentNs);
                break;
            }
        }
    }
    return -1;
}

int llread(int fd, unsigned char* buffer){

    unsigned char byte;
    int state = 0;
    unsigned char A, C, BCC1;
    int idx = 0;
    unsigned char data[BUF_SIZE * 2];
    unsigned char expectedControl = currentNs ? C_I_1 : C_I_0;

    printf("Waiting for I-frame with Ns=%d (expected C=0x%02X)\n", currentNs, expectedControl);

    while (1){

        if (read(fd, &byte, 1) <= 0) 
            continue;

        switch (state){

            case 0: //espera a flag inicial

                if (byte == FLAG) 
                    state = 1;

                break;

            case 1: //espera A

                if (byte == A_SENDER){

                    A = byte; 
                    state = 2; 
                }

                else if (byte == FLAG) 
                    state = 1;

                else 
                    state = 0;

                break;

            case 2: //espera C verificando Ns

                printf("Receiver: expected C=0x%02X, received=0x%02X\n", expectedControl, byte);
                if (byte == expectedControl){

                    C = byte; 
                    state = 3; 
                }
                else 
                    state = 0;

                break;

            case 3: //checka BCC1

                if (byte == (A ^ C)){
                
                    BCC1 = byte; 
                    idx = 0; 
                    state = 4; 
                }

                else 
                    state = 0;

                break;

            case 4:

                if (byte == FLAG){

                    if (idx < 1){ //verifica se havia dados

                        printf("Error: Frame too short\n");
                        return -1;
                    }

                    unsigned char destuffed[BUF_SIZE * 2];
                    int destuffed_len = byteDestuff(data, idx, destuffed); //destuffing
                    if (destuffed_len < 1){

                        printf("Destuffing failed\n");
                        sendIFrame(fd, A_RECEIVER, currentNs ? C_REJ_1 : C_REJ_0);
                        return -1;
                    }//se falhou envia REJ

                    unsigned char received_bcc2 = destuffed[destuffed_len - 1];
                    unsigned char calculated_bcc2 = calculateBCC2(destuffed, destuffed_len - 1);//separação do bcc2 recebido e calculado
                    printf("BCC2 check: received=0x%02X, calculated=0x%02X\n", received_bcc2, calculated_bcc2);

                    if (calculated_bcc2 == received_bcc2){ //com tudo ok, copia para buffer, muda Ns, envia RR e retorna

                        memcpy(buffer, destuffed, destuffed_len - 1);
                        currentNs = 1 - currentNs;
                        sendIFrame(fd, A_RECEIVER, currentNs ? C_RR_1 : C_RR_0);
                        return destuffed_len - 1;
                    } 
                    
                    else{ //envia REJ se tiver mal

                        printf("BCC2 mismatch\n");
                        sendIFrame(fd, A_RECEIVER, currentNs ? C_REJ_1 : C_REJ_0);
                        return -1;
                    }
                }
                else{

                    if (idx < BUF_SIZE * 2) 
                        data[idx++] = byte;
                }
                break;
        }
    }

    return -1;
}

int llclose(int fd){

    while (alarmCount < MAX_RETRIES){

        sendDiscFrame(fd, A_SENDER, C_DISC);
        alarmEnabled = true;
        alarm(TIMEOUT);
        if (readSupervisionFrame(fd, C_DISC)){

            printf("Disc recebido! Cancelando alarme.\n");
            alarm(0);
            alarmEnabled = false;
            
            sendUAFrame(fd, A_RECEIVER, C_UA);
            close(fd);
            printf("Connection closed successfully.\n");
            return 1;
        }
    }
    return -1;
}
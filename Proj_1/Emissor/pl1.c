// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source
#define MAX_RETRIES 3

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

volatile int STOP = FALSE;
unsigned char A = 0x03, C = 0x07, BCC;
int alarmCount = 0;
int alarmEnabled = FALSE;
int retransmissions = 0;

typedef enum{

    Start, // isso é um 0
    FLAGRCV, // isso é um 1
    ARCV,
    CRCV,
    BCCOK,
    PARA

} stateNames;

stateNames currentState = Start;
bool FLAG_RCV;
bool Other_RCV;
bool A_RCV;
bool C_RCV;

unsigned char receivedByte;

void alarmHandler(int signal){

    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

void state_machine(unsigned char byte){
    switch (currentState){

        case Start:

            if (byte == 0x7E)
                currentState = FLAGRCV;
            break;

        case FLAGRCV:

            if (byte == A)
                currentState = ARCV;
            else if (byte != 0x7E)
                currentState = Start;
            break;

        case ARCV:
            if (byte == C)
                currentState = CRCV;
            else if (byte == 0x7E)
                currentState = FLAGRCV;
            else
                currentState = Start;
            break;

        case CRCV:

            if (byte == A^C)
                currentState = BCCOK;
            else if (byte == 0x7E)
                currentState = FLAGRCV;
            else
                currentState = Start;
            break;

        case BCCOK:
            if (byte == 0x7E){
                  // FLAG final recebida
                currentState = PARA;
                STOP = 1; // Finaliza a máquina de estados
            } 
            else
                currentState = Start;
            
            break;

        default:

            currentState = Start;
            break;
    }
}

int main(int argc, char *argv[]){
    
    // Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");
    BCC=A^C;
    // Create string to send
    unsigned char buf[BUF_SIZE] = {0};

    unsigned char SET[] = {0x7E, 0x03, 0x03, 0x00, 0x7E};
    
    /*for (int i = 0; i < BUF_SIZE; i++)
    {
        buf[i] = 'a' + i % 3;
    }*/

    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    buf[5] = '\n';

    int bytes = write(fd, SET, 5);
    printf("%d bytes written\n", bytes);

    // Wait until all bytes have been written to the serial port
    sleep(1);

    printf("Enum: %d\n",currentState);

    alarm(3);

    while (currentState != PARA){
        
        printf("Current state: %d\n", currentState);
        
        int res = read(fd, &receivedByte, 1);

        //printf("O valor de res é %d\n", res);

        if (res > 0){
            
            printf("Recebido: 0x%02X\n", receivedByte);
            state_machine(receivedByte);
        }

        if (alarmCount > retransmissions){

            retransmissions++;

            if (retransmissions < MAX_RETRIES){

                printf("Timeout #%d: Retransmitindo SET...\n", retransmissions);
                write(fd, SET, 5);
                alarm(3);
            } 
            
            else{

                printf("Máximo de retransmissões atingido. Encerrando.\n");
                exit(1);
            }
        }
    }   
    printf("UA recebido! Cancelando alarme\n");
    alarm(0);

    unsigned char UA[5];
    int bytes2 = read(fd, UA, 5);
    buf[bytes2] = '\0';
    
    for(int i = 0; i < sizeof(UA); i++)
        printf("var=0x%X\n", UA[i]);
    
    

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1){
        
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}


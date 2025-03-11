// Read from serial port in non-canonical mode
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

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256
#define MAX_RETRIES 3

int alarmEnabled = FALSE;
int alarmCount = 0;
volatile int STOP = FALSE;
unsigned char A = 0x03, C = 0x03, BCC;
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

void init_SM() {
    currentState = Start;  // Estado inicial
}

// Função da Máquina de Estados
void state_machine(unsigned char byte) {
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

            if (byte == (A^C))
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
                STOP = TRUE; // Finaliza a máquina de estados
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
    // Program usage: Uses either COM1 or COM2
    (void)signal(SIGALRM, alarmHandler);
    BCC = A ^ C;
    const char *serialPortName = argv[1];

    if (argc < 2){
        
        printf("Incorrect program usage\n"
            "Usage: %s <SerialPort>\n"
            "Example: %s /dev/ttyS1\n",
            argv[0],
            argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0){

        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1){

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
    if (tcsetattr(fd, TCSANOW, &newtio) == -1){

        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Loop for input

    init_SM();

    unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    unsigned char UA[]={0x7E, 0x01, 0x07,(A^C), 0x7E};
    unsigned char SET[]={0x7E, 0x03, 0x03, 0x00, 0x7E};
    unsigned char buf2[5];  

    printf("Enum: %d\n",currentState);

    while (currentState != PARA){
        
        printf("Current state: %d\n", currentState);
        
        int res = read(fd, &receivedByte, 1);

        //printf("O valor de res é %d\n", res);

        if (res > 0) {
            printf("Recebido: 0x%X\n", receivedByte);
            state_machine(receivedByte);
        }

        // Se `SET` foi recebido, enviamos `UA`
        if (currentState == PARA && alarmEnabled == FALSE) {
            
            printf("SET recebido! Enviando UA...\n");
            write(fd, UA, 5);
            alarmEnabled = TRUE; // Marca que o alarme foi ativado
            alarm(3); // Ativa o alarme
        }

        // Se o alarme foi acionado, reenviar UA
        if (alarmCount > 0 && alarmEnabled == FALSE) {
            
            printf("Timeout: Reenviando UA...\n");
            write(fd, UA, 5);
            alarm(3);
            alarmEnabled = TRUE;
        }
    }   
    printf("SET recebido! Enviando UA...\n");
    alarm(0);
    
    write(fd, UA, 5);
    printf("UA enviado.\n");
    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1){

        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}


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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

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

void init_SM() {
    currentState = Start;  // Estado inicial
}

unsigned char receivedByte;

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

            if (byte == BCC)
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

typedef enum{

    START_I,
    FLAG_RCV_I,
    A_RCV_I,
    C_RCV_I,
    BCC1_OK_I,
    DATA_RCV,
    BCC2_OK,
    END_I
}stateIFrame;

stateIFrame currentStateI = START_I;

void init_SM_I() {
    currentStateI = START_I;  // Estado inicial
}

void send_ACK(int fd, unsigned char ns, bool is_REJ) {
    
    unsigned char A = 0x03;  
    unsigned char C;
    
    if (is_REJ) {
        
        if (ns == 0)
            C = 0x01;
             // REJ(0)
        else
            C = 0x81; // REJ(1)
    } 
    
    else {

        if (ns == 0)
            C = 0x05; // RR(1)

        else
            C = 0x85; // RR(0)
    }
    unsigned char BCC = A ^ C;
    unsigned char frame[5] = { 0x7E, A, C, BCC, 0x7E };

    if (is_REJ)
        printf("Enviando REJ(%d)...\n", ns);

    else
        printf("Enviando RR(%d)...\n", ns);

    write(fd, frame, 5);
}

void state_machine_I(unsigned char byte, int fd) {

    static unsigned char address, control, bcc1, ns;//A,C,BCC
    static unsigned char dataBuffer[BUF_SIZE];
    static int dataIndex = 0;

    switch (currentStateI) {

        case START_I:

            if (byte == 0x7E)
                currentStateI = FLAG_RCV_I;

            break;

        case FLAG_RCV_I:

            if (byte == 0x03){  // Endereço válido

                address = byte;
                currentStateI = A_RCV_I;
            } 
            
            else if (byte != 0x7E) 
                currentStateI = START_I;
            
            break;

        case A_RCV_I:

            if (byte == 0x00) {  // Ns = 0 ou 1
                
                control = byte;
                ns = 0;
                currentStateI = C_RCV_I;
            } 

            else if(byte == 0x40){

                control = byte;
                ns = 1;
                currentStateI = C_RCV_I;
            }

            else if (byte == 0x7E) 
                currentStateI = FLAG_RCV_I;
            
            else 
                currentStateI = START_I;
            
            break;

        case C_RCV_I:

            bcc1 = address ^ control;

            if (byte == bcc1){

                currentStateI = BCC1_OK_I;
                dataIndex = 0;
            } 

            else if (byte == 0x7E) 
                currentStateI = FLAG_RCV_I;

            else 
                currentStateI = START_I;
            
            break;

        case BCC1_OK_I:

            if (byte == 0x7E)   // Se encontrar FLAG aqui, erro de frame vazio
                currentStateI = START_I;

            else{

                dataBuffer[dataIndex++] = byte;
                currentStateI = DATA_RCV;
            }
            break;

        case DATA_RCV:

        if (byte == 0x7E){  // FLAG final

            unsigned char bcc2_calc = 0;

            for (int i = 0; i < dataIndex - 1; i++) 
                bcc2_calc ^= dataBuffer[i]; 

            if (bcc2_calc == dataBuffer[dataIndex - 1]){

                send_ACK(fd, ns, FALSE);
                currentStateI = END_I;
                
            } 
            else{

                send_ACK(fd, ns, TRUE);
                currentStateI = START_I;
            }
        } 
        else{

            if (dataIndex < BUF_SIZE) 
                dataBuffer[dataIndex++] = byte;

             else 
                currentStateI = START_I;
        }
        break;

        case BCC2_OK:

            send_ACK(fd, ns, FALSE);
            currentStateI = END_I;
            break;

        case END_I:

            currentStateI = START_I;
            break;

        default:

            currentStateI = START_I;
            break;
    }
}

int main(int argc, char *argv[]){
    // Program usage: Uses either COM1 or COM2
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
    init_SM_I();

    unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    unsigned char UA[]={0x7E, 0x01, 0x07, 0x06, 0x7E};
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
}   
    printf("SET recebido! Enviando UA...\n");
    write(fd, UA, 5);
    printf("UA enviado.\n");
    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    while(currentStateI != END_I){
        
        int rec = read(fd, &receivedByte, 1);

        if(rec > 0){

            printf("Recebido: 0x%X\n", receivedByte);
            state_machine_I(receivedByte, fd)
        }
    }

    printf("I frame recebido! Enviando RR");
    send()
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1){

        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}


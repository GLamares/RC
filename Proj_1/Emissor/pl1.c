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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

volatile int STOP = FALSE;

#define BUF_SIZE 256
#define FLAG 0x7E
#define A 0x03
#define C_0 0x00  // Ns = 0
#define C_1 0x40  // Ns = 1
#define RR_0 0x05
#define RR_1 0x85
#define REJ_0 0x01
#define REJ_1 0x81

typedef enum{

    START_TX,
    FLAG_TX,
    A_TX,
    C_TX,
    BCC1_TX,
    DATA_TX,
    BCC2_TX,
    FLAG_END_TX,
    WAIT_ACK,
    RETRANSMIT_TX
    
} stateTxFrame;

stateTxFrame currentStateTx = START_TX;

// Variáveis de transmissão
unsigned char txBuffer[BUF_SIZE];
unsigned char ns = 0;  // Número de sequência (0 ou 1)

// Inicializar máquina de estados do transmissor
void init_SM_Tx(){

    currentStateTx = START_TX;
}

// Função para calcular BCC2 (XOR de todos os bytes de dados)
unsigned char calculate_BCC2(unsigned char *data, int length){

    unsigned char bcc2 = 0;

    for (int i = 0; i < length; i++)
        bcc2 ^= data[i];
    
    return bcc2;
}

void state_machine_Tx(int fd, unsigned char *data, int length){

    unsigned char receivedByte;
    unsigned char expectedRR, expectedREJ;

    if (ns == 0){

        expectedRR = RR_1;
        expectedREJ = REJ_0;
    } 
    else{

        expectedRR = RR_0;
        expectedREJ = REJ_1;
    }

    switch (currentStateTx){

        case START_TX:

            write(fd, data, length);
            currentStateTx = WAIT_ACK;

        break;

        case WAIT_ACK:

            if (read(fd, &receivedByte, 1) > 0){

                if (receivedByte == expectedRR){

                    printf("RR recebido! Próximo frame...\n");

                    if (ns == 0) 
                        ns = 1;

                     else 
                        ns = 0;
                    
                    currentStateTx = START_TX;
                }
                
                else if (receivedByte == expectedREJ){

                    printf("REJ recebido! Retransmitindo frame...\n");
                    currentStateTx = RETRANSMIT_TX;
                }
            }
        break;

        case RETRANSMIT_TX:

            write(fd, data, length);
            currentStateTx = WAIT_ACK;
        break;

        default:

            currentStateTx = START_TX;
            break;
    }
}

int main(int argc, char *argv[])
{
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

    // Create string to send
    unsigned char buf[BUF_SIZE] = {0};
    
    unsigned char SET[] = {0x7E, 0x03, 0x03, 0x00, 0x7E};
    
    buf[5] = '\n';

    int bytes = write(fd, SET, 5);
    printf("%d bytes written\n", bytes);

    // Wait until all bytes have been written to the serial port
    sleep(1);
    
    unsigned char UA[5];
    int bytes2 = read(fd, UA, 5);
    buf[bytes2] = '\0';
    
    unsigned char testData[] = "Hello, Receiver!";
    int dataLength = strlen((char *)testData);

    while (1)
        state_machine_Tx(fd, testData, dataLength);
    
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}

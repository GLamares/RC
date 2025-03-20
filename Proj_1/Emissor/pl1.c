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

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256
#define MAX_RETRIES 3

volatile int STOP = FALSE;
unsigned char A = 0x03, C = 0x07, BCC;
int alarmCount = 0;
int alarmEnabled = FALSE;

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

void send_IFrame(int fd, unsigned char *data, int length){

    unsigned char control;
    
    if (ns == 0) 
        control = C_0;
    
    else 
        control = C_1;

    unsigned char bcc1 = A ^ control;
    unsigned char bcc2 = calculate_BCC2(data, length);

    unsigned char frame[BUF_SIZE + 6];
    frame[0] = FLAG;
    frame[1] = A;
    frame[2] = control;
    frame[3] = bcc1;
    
    // Copia os dados do payload para o frame
    for (int i = 0; i < length; i++)
        frame[4 + i] = data[i];

    frame[4 + length] = bcc2;
    frame[5 + length] = FLAG;

    printf("Enviando IFrame com Ns=%d...\n", ns);
    write(fd, frame, length + 6);
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

            send_IFrame(fd, data, length);
            alarmEnabled = TRUE;
            alarm(3); // Timeout para ACK/NACK
            currentStateTx = WAIT_ACK;
            break;

        case WAIT_ACK:

            if (read(fd, &receivedByte, 1) > 0){

                printf("Recebido: 0x%02X\n", receivedByte);

                if (receivedByte == expectedRR) {

                    printf("RR recebido! Enviando próximo frame...\n");
                    ns = 1 - ns; // Alterna Ns
                    currentStateTx = START_TX;
                }
                else if (receivedByte == expectedREJ){

                    printf("REJ recebido! Retransmitindo frame...\n");
                    currentStateTx = RETRANSMIT_TX;
                }
            }

            if (!alarmEnabled && alarmCount < MAX_RETRIES){

                printf("Timeout #%d: Reenviando IFrame...\n", alarmCount);
                send_IFrame(fd, data, length);
                alarmEnabled = TRUE;
                alarm(3);
            }
            break;

        case RETRANSMIT_TX:

            send_IFrame(fd, data, length);
            alarmEnabled = TRUE;
            alarm(3);
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
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

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

    currentState = Start;
    printf("Enviando SET...\n");
    write(fd, SET, 5);
    alarmEnabled = TRUE;
    alarm(3); // Inicia o timer

    // **Loop de retransmissão até receber UA ou atingir MAX_RETRIES**
    while (alarmCount < MAX_RETRIES){

        int res = read(fd, &receivedByte, 1);//res ou é 1 ou 0, é 1 se ler um byte e 0 se não

        if (res){

            printf("Recebido: 0x%02X\n", receivedByte);
            state_machine(receivedByte);
        }

        if (currentState == PARA){

            printf("UA recebido! Cancelando alarme.\n");
            alarm(0);
            alarmEnabled = FALSE;
            break;
        }

        // **Se o alarme expirou e UA não foi recebido, reenviar SET**
        if (!alarmEnabled && alarmCount < MAX_RETRIES){

            printf("Timeout #%d: Reenviando SET...\n", alarmCount);
            write(fd, SET, 5);
            alarmEnabled = TRUE;
            alarm(3);
        }
    }

    if (alarmCount >= MAX_RETRIES) {
        printf("Máximo de retransmissões atingido. Encerrando emissor.\n");
        exit(1);
    }

    // Wait until all bytes have been written to the serial port
    sleep(1);

    init_SM_Tx();

    unsigned char testData[] = "Hello, Receiver!";
    int dataLength = strlen((char *)testData);
    alarmCount = 0;

    printf("Enviando primeiro I-Frame...\n");
    send_IFrame(fd, testData, dataLength);
    alarmEnabled = TRUE;
    alarm(3);

    while (alarmCount < MAX_RETRIES){

        int rec = read(fd, &receivedByte, 1);
        if (rec > 0){

            printf("Recebido: 0x%02X\n", receivedByte);

            if (receivedByte == RR_1 || receivedByte == RR_0){

                printf("RR recebido! Enviando próximo frame...\n");
                ns = 1 - ns;
                alarm(0);
                alarmEnabled = FALSE;
                send_IFrame(fd, testData, dataLength);
                alarmEnabled = TRUE;
                alarm(3);
            }

            else if (receivedByte == REJ_1 || receivedByte == REJ_0){

                printf("REJ recebido! Reenviando frame...\n");
                send_IFrame(fd, testData, dataLength);
                alarmEnabled = TRUE;
                alarm(3);
            }
        }

        if (!alarmEnabled && alarmCount < MAX_RETRIES) {
            printf("Timeout #%d: Reenviando I-Frame...\n", alarmCount);
            send_IFrame(fd, testData, dataLength);
            alarmEnabled = TRUE;
            alarm(3);
        }
    }

    if (alarmCount >= MAX_RETRIES) {
        printf("Máximo de retransmissões atingido. Encerrando transmissão.\n");
        exit(1);
    }

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}

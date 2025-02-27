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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

volatile int STOP = FALSE;

int main(int argc, char *argv[]){
    // Program usage: Uses either COM1 or COM2
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
    unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    unsigned char UA[]={0x7E, 0x01, 0x07, 0x06, 0x7E};
    unsigned char SET[]={0x7E, 0x03, 0x03, 0x00, 0x7E};
    unsigned char buf2[5];  
    int contador=0;

    while (STOP == FALSE){
        // Returns after 5 chars have been input
        int bytes = read(fd, buf2, 5);
        buf[bytes] = '\0'; // Set end of string to '\0', so we can printf

        for(int i=0; i < sizeof(buf2); i++){

            printf("var=0x%X\n", buf2[i]);

            if (buf[0] == 'z')
                STOP = TRUE;
        }

        for(int i = 0; i < sizeof(buf2); i++){
            
            printf("Received: 0x%X, Expected: 0x%X\n", buf2[i], SET[i]);

            if(buf2[i] == SET[i])
                contador++;
        }
    

    if(contador==5){

        int bytes2 = write(fd, UA, 5);
        printf("%d bytes written\n", bytes2);
    }
    

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
}

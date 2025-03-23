
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define BAUDRATE B38400

int setup_serial_port(const char* serialPort) {
    struct termios newtio;
    int fd = open(serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(serialPort);
        return -1;
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 1;

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &newtio) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

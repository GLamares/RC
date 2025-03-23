
#ifndef DLL_API_H
#define DLL_API_H

int llopen(const char* port, int isTransmitter);
int llwrite(int fd, unsigned char* buffer, int length);
int llread(int fd, unsigned char* buffer);
int llclose(int fd);

#endif

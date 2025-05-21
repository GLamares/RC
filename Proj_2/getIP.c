// getIP.c
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include "getIP.h"

int resolve_hostname(const char* hostname, char* ip_buffer, int buffer_len){

    struct hostent* host;

    if ((host = gethostbyname(hostname)) == NULL){

        herror("gethostbyname");
        return -1;
    }

    if (inet_ntop(AF_INET, host->h_addr_list[0], ip_buffer, buffer_len) == NULL){

        perror("inet_ntop");
        return -1;
    }
    return 0;
}

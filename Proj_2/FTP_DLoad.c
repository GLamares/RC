/**
 * Simple FTP client - clientTCP.c
 * Implements FTP control connection to download a file using PASV mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "getIP.h"

#define FTP_PORT 21
#define MAX_BUF 1024

int connect_to_server(const char* ip, int port);
void send_command(int sockfd, const char* cmd);
int read_response(int sockfd, char* response);
int parse_pasv_response(char* response, char* ip, int* port);
int parse_url(const char* url, char* user, char* pass, char* host, char* path);

int main(int argc, char* argv[]){

    if (argc != 2){

        fprintf(stderr, "Usage: %s ftp://[user:pass@]host/path\n", argv[0]);
        return 1;
    }

    char user[64] = "anonymous", pass[64] = "anonymous@", host[256], path[256];
    if (parse_url(argv[1], user, pass, host, path) < 0){

        fprintf(stderr, "Invalid URL format.\n");
        return 1;
    }

    char server_ip[64];
    if (resolve_hostname(host, server_ip, sizeof(server_ip)) < 0){

        fprintf(stderr, "Could not resolve host: %s\n", host);
        return 1;
    }

    char buffer[MAX_BUF];
    int control_sock = connect_to_server(server_ip, FTP_PORT);
    read_response(control_sock, buffer); // 220

    snprintf(buffer, sizeof(buffer), "USER %s\r\n", user);
    send_command(control_sock, buffer);
    read_response(control_sock, buffer); // 331 ou 230

    snprintf(buffer, sizeof(buffer), "PASS %s\r\n", pass);
    send_command(control_sock, buffer);
    read_response(control_sock, buffer); // 230

    send_command(control_sock, "TYPE I\r\n");
    read_response(control_sock, buffer); // 200

    send_command(control_sock, "PASV\r\n");
    read_response(control_sock, buffer); // 227

    char data_ip[64];
    int data_port;
    if (parse_pasv_response(buffer, data_ip, &data_port) < 0){

        fprintf(stderr, "Failed to parse PASV response\n");
        close(control_sock);
        return 1;
    }

    int data_sock = connect_to_server(data_ip, data_port);

    snprintf(buffer, sizeof(buffer), "RETR %s\r\n", path);
    send_command(control_sock, buffer);
    read_response(control_sock, buffer); // 150

    char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;  // se não houver '/', usar o path inteiro

    FILE* f = fopen(filename, "wb");
    //FILE* f = fopen(path, "wb");
    if (!f){

        perror("fopen");
        close(data_sock);
        close(control_sock);
        return 1;
    }

    int bytes;
    while ((bytes = read(data_sock, buffer, MAX_BUF)) > 0){

        if (fwrite(buffer, 1, bytes, f) != bytes){

            perror("fwrite");
            fclose(f);
            close(data_sock);
            close(control_sock);
            return 1;
        }
    }
    fclose(f);
    close(data_sock);

    read_response(control_sock, buffer); // 226
    send_command(control_sock, "QUIT\r\n");
    read_response(control_sock, buffer); // 221

    close(control_sock);
    printf("File '%s' downloaded successfully.\n", path);
    return 0;
}

int connect_to_server(const char* ip, int port){

    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){

        perror("socket()");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_aton(ip, &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){

        perror("connect()");
        exit(1);
    }

    return sockfd;
}

void send_command(int sockfd, const char* cmd){

    printf(">> %s", cmd);
    if (write(sockfd, cmd, strlen(cmd)) < 0){

        perror("write()");
        exit(1);
    }
}

int read_response(int sockfd, char* response) {
    bzero(response, MAX_BUF);
    int len = 0;
    char line[512];
    while (1) {
        bzero(line, sizeof(line));
        int i = 0;
        char ch;
        while (i < sizeof(line) - 1 && read(sockfd, &ch, 1) == 1) {
            line[i++] = ch;
            if (ch == '\n') break;
        }
        line[i] = '\0';
        printf("<< %s", line);
        strcat(response, line);
        len += i;

        // Se linha começa com "xyz " (com espaço), é o fim
        if (isdigit(line[0]) && isdigit(line[1]) && isdigit(line[2]) && line[3] == ' ')
            break;

        if (len >= MAX_BUF - 1) break;
    }
    return atoi(response);
}


int parse_pasv_response(char* response, char* ip, int* port){

    int h1, h2, h3, h4, p1, p2;
    char* p1_start = strchr(response, '(');
    if (!p1_start) 
        return -1;

    if (sscanf(p1_start, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) < 6)
        return -1;

    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = (p1 << 8) + p2;
    return 0;
}

int parse_url(const char* url, char* user, char* pass, char* host, char* path){

    if (strncmp(url, "ftp://", 6) != 0) 
        return -1;

    const char* p = url + 6;
    const char* at = strchr(p, '@');
    if (at) 
        sscanf(p, "%[^:]:%[^@]@%[^/]/%s", user, pass, host, path);
    
     else 
        sscanf(p, "%[^/]/%s", host, path);
    

    return 0;
}

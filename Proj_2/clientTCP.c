/**      (C)2000-2021 FEUP
 *       tidy up some includes and parameters
 * */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#define SERVER_PORT 6000
#define SERVER_ADDR "192.168.28.96"
#define MAX_FIELD 256

typedef struct {
    char user[MAX_FIELD];
    char pass[MAX_FIELD];
    char host[MAX_FIELD];
    char path[MAX_FIELD];
} ftp_url;

int parse_url(const char* url, ftp_url *info) {
    if (strncmp(url, "ftp://", 6) != 0){
        
        fprintf(stderr, "❌ URL inválido. Tem de começar com 'ftp://'\n");
        return -1;
    }

    const char* ptr = url + 6;

    // Verifica se há user:pass@
    const char* at_ptr = strchr(ptr, '@');

    if (at_ptr) {
        // Tem user e password
        const char* colon_ptr = strchr(ptr, ':');
        if (!colon_ptr || colon_ptr > at_ptr) {
            fprintf(stderr, "❌ Erro no formato do user:pass\n");
            return -1;
        }

        size_t user_len = colon_ptr - ptr;
        size_t pass_len = at_ptr - colon_ptr - 1;

        if (user_len >= MAX_FIELD || pass_len >= MAX_FIELD) {
            fprintf(stderr, "❌ User ou password demasiado longos\n");
            return -1;
        }

        strncpy(info->user, ptr, user_len);
        info->user[user_len] = '\0';

        strncpy(info->pass, colon_ptr + 1, pass_len);
        info->pass[pass_len] = '\0';

        ptr = at_ptr + 1;
    } else {
        // Anonymous por defeito
        strcpy(info->user, "anonymous");
        strcpy(info->pass, "anonymous@");
    }

    // Agora espera-se host/path
    const char* slash_ptr = strchr(ptr, '/');
    if (!slash_ptr) {
        fprintf(stderr, "❌ URL inválido — falta caminho (/path)\n");
        return -1;
    }

    size_t host_len = slash_ptr - ptr;
    if (host_len >= MAX_FIELD) {
        fprintf(stderr, "❌ Host demasiado longo\n");
        return -1;
    }

    strncpy(info->host, ptr, host_len);
    info->host[host_len] = '\0';

    strncpy(info->path, slash_ptr, MAX_FIELD);
    info->path[MAX_FIELD - 1] = '\0';

    return 0;
}

int main(int argc, char **argv) {

    if (argc > 1)
        printf("**** No arguments needed. They will be ignored. Carrying ON.\n");
    int sockfd;
    struct sockaddr_in server_addr;
    char buf[] = "Mensagem de teste na travessia da pilha TCP/IP\n";
    size_t bytes;

    /*server address handling*/
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);    /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(SERVER_PORT);        /*server TCP port must be network byte ordered */

    /*open a TCP socket*/
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }
    /*connect to the server*/
    if (connect(sockfd,
                (struct sockaddr *) &server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }
    /*send a string to the server*/
    bytes = write(sockfd, buf, strlen(buf));
    if (bytes > 0)
        printf("Bytes escritos %ld\n", bytes);
    else {
        perror("write()");
        exit(-1);
    }

    if (close(sockfd)<0) {
        perror("close()");
        exit(-1);
    }
    return 0;
}



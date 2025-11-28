// client_leilao.c
// Cliente de leilao com TCP + thread para multicast UDP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "comum.h"

typedef struct {
    int udp_fd;
} ThreadMulticastArgs;

void *thread_multicast(void *arg) {
    ThreadMulticastArgs *t = (ThreadMulticastArgs *)arg;
    int udp_fd = t->udp_fd;
    free(t);

    char buf[256];
    struct sockaddr_in remetente;
    socklen_t tam = sizeof(remetente);

    printf("[MULTICAST] Thread de escuta iniciada.\n");

    while (1) {
        ssize_t lidos = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                                 (struct sockaddr *)&remetente, &tam);
        if (lidos < 0) {
            perror("recvfrom");
            break;
        }
        buf[lidos] = '\0';
        printf("[MULTICAST] %s\n", buf);
    }

    printf("[MULTICAST] Thread finalizada.\n");
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_servidor> <id_cliente> [porta_tcp]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip_servidor = argv[1];
    int id_cliente = atoi(argv[2]);
    int porta_tcp = (argc > 3) ? atoi(argv[3]) : PORTA_TCP_PADRAO;

    // 1. Configurar socket TCP
    int sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_tcp < 0) {
        perror("socket TCP");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr_servidor;
    memset(&addr_servidor, 0, sizeof(addr_servidor));
    addr_servidor.sin_family = AF_INET;
    addr_servidor.sin_port = htons(porta_tcp);

    if (inet_pton(AF_INET, ip_servidor, &addr_servidor.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd_tcp, (struct sockaddr *)&addr_servidor, sizeof(addr_servidor)) < 0) {
        perror("connect");
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }

    printf("Conectado ao servidor %s:%d como cliente %d.\n",
           ip_servidor, porta_tcp, id_cliente);

    // 2. Configurar socket UDP para multicast
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror("socket UDP");
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }

    struct sockaddr_in addr_local;
    memset(&addr_local, 0, sizeof(addr_local));
    addr_local.sin_family = AF_INET;
    addr_local.sin_port = htons(PORTA_UDP_MULTICAST);
    addr_local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_fd, (struct sockaddr *)&addr_local, sizeof(addr_local)) < 0) {
        perror("bind UDP");
        close(udp_fd);
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(GRUPO_MULTICAST);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(udp_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("IP_ADD_MEMBERSHIP");
        close(udp_fd);
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }

    // 3. Criar thread para escutar multicast
    pthread_t tid;
    ThreadMulticastArgs *targs = malloc(sizeof(ThreadMulticastArgs));
    targs->udp_fd = udp_fd;
    if (pthread_create(&tid, NULL, thread_multicast, targs) != 0) {
        perror("pthread_create");
        close(udp_fd);
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }
    pthread_detach(tid);

    // 4. Loop de envio de lances
    while (1) {
        int valor_lance;
        printf("Digite o valor do lance (ou -1 para sair): ");
        if (scanf("%d", &valor_lance) != 1) {
            fprintf(stderr, "Entrada invalida.\n");
            break;
        }
        if (valor_lance < 0) {
            printf("Encerrando cliente.\n");
            break;
        }

        MensagemLance lance;
        lance.id_cliente = id_cliente;
        lance.valor_lance = valor_lance;

        uint8_t buf[8];
        empacotar_lance(buf, lance);

        if (send(sockfd_tcp, buf, sizeof(buf), 0) < 0) {
            perror("send TCP");
            break;
        }

        char resposta[256];
        ssize_t lidos = recv(sockfd_tcp, resposta, sizeof(resposta) - 1, 0);
        if (lidos <= 0) {
            printf("Servidor encerrou a conexao.\n");
            break;
        }
        resposta[lidos] = '\0';
        printf("[TCP] Resposta do servidor: %s", resposta);
    }

    close(udp_fd);
    close(sockfd_tcp);
    return 0;
}

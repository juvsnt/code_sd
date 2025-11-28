// server_leilao.c
// Servidor de leilao com TCP + threads + multicast UDP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "comum.h"

#define BACKLOG 10

typedef struct {
    int cliente_fd;
    int udp_fd;
    struct sockaddr_in addr_multicast;
} ThreadArgs;

// Estado global do leilao
int lance_atual = 0;
int cliente_vencedor = -1;
pthread_mutex_t mutex_lance = PTHREAD_MUTEX_INITIALIZER;

// Envia notificacao de novo lance via multicast
void enviar_multicast_novo_lance(int udp_fd, struct sockaddr_in *addr, MensagemLance m) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "NOVO_LANCE %d %d", m.id_cliente, m.valor_lance);

    if (sendto(udp_fd, msg, strlen(msg), 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        perror("sendto multicast");
    } else {
        printf("[MULTICAST] Enviada notificacao: %s\n", msg);
    }
}

// Função executada por thread para cada cliente
void *thread_cliente(void *arg) {
    ThreadArgs *targs = (ThreadArgs *)arg;
    int cliente_fd = targs->cliente_fd;
    int udp_fd = targs->udp_fd;
    struct sockaddr_in addr_m = targs->addr_multicast;
    free(targs);

    uint8_t buf[8];
    ssize_t lidos;

    printf("Thread iniciada para cliente fd=%d\n", cliente_fd);

    while (1) {
        lidos = recv(cliente_fd, buf, sizeof(buf), 0);
        if (lidos == 0) {
            printf("Cliente fd=%d encerrou a conexao.\n", cliente_fd);
            break;
        } else if (lidos < 0) {
            perror("recv");
            break;
        } else if (lidos != 8) {
            printf("Mensagem recebida com tamanho inesperado: %zd bytes\n", lidos);
            continue;
        }

        MensagemLance lance = desempacotar_lance(buf);
        printf("Recebido lance do cliente %d: %d\n", lance.id_cliente, lance.valor_lance);

        int lance_aprovado = 0;

        pthread_mutex_lock(&mutex_lance);
        if (lance.valor_lance > lance_atual) {
            lance_atual = lance.valor_lance;
            cliente_vencedor = lance.id_cliente;
            lance_aprovado = 1;
            printf("[ATUALIZACAO] Novo lance atual = %d (cliente %d)\n",
                   lance_atual, cliente_vencedor);
        }
        pthread_mutex_unlock(&mutex_lance);

        char resposta[128];
        if (lance_aprovado) {
            snprintf(resposta, sizeof(resposta),
                     "Lance aceito. Novo lance atual = %d (cliente %d)\n",
                     lance_atual, cliente_vencedor);

            // Enviar notificacao multicast
            enviar_multicast_novo_lance(udp_fd, &addr_m, lance);
        } else {
            snprintf(resposta, sizeof(resposta),
                     "Lance rejeitado. Lance atual ainda = %d (cliente %d)\n",
                     lance_atual, cliente_vencedor);
        }

        if (send(cliente_fd, resposta, strlen(resposta), 0) < 0) {
            perror("send resposta");
            break;
        }
    }

    close(cliente_fd);
    printf("Thread finalizada.\n");
    return NULL;
}

int main(int argc, char *argv[]) {
    int porta_tcp = (argc > 1) ? atoi(argv[1]) : PORTA_TCP_PADRAO;

    int servidor_fd;
    struct sockaddr_in addr_servidor, addr_cliente;
    socklen_t tam_cliente;

    // Socket TCP
    servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd < 0) {
        perror("socket TCP");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr_servidor, 0, sizeof(addr_servidor));
    addr_servidor.sin_family = AF_INET;
    addr_servidor.sin_addr.s_addr = INADDR_ANY;
    addr_servidor.sin_port = htons(porta_tcp);

    if (bind(servidor_fd, (struct sockaddr *)&addr_servidor, sizeof(addr_servidor)) < 0) {
        perror("bind");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(servidor_fd, BACKLOG) < 0) {
        perror("listen");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    // Socket UDP para multicast
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror("socket UDP");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr_multicast;
    memset(&addr_multicast, 0, sizeof(addr_multicast));
    addr_multicast.sin_family = AF_INET;
    addr_multicast.sin_port = htons(PORTA_UDP_MULTICAST);
    addr_multicast.sin_addr.s_addr = inet_addr(GRUPO_MULTICAST);

    unsigned char ttl = 1;
    if (setsockopt(udp_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt TTL");
    }

    printf("Servidor de leilao iniciado na porta TCP %d\n", porta_tcp);
    printf("Multicast UDP em grupo %s:%d\n\n", GRUPO_MULTICAST, PORTA_UDP_MULTICAST);

    while (1) {
        tam_cliente = sizeof(addr_cliente);
        int cliente_fd = accept(servidor_fd, (struct sockaddr *)&addr_cliente, &tam_cliente);
        if (cliente_fd < 0) {
            perror("accept");
            continue;
        }

        printf("Novo cliente conectado. fd=%d\n", cliente_fd);

        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->cliente_fd = cliente_fd;
        args->udp_fd = udp_fd;
        args->addr_multicast = addr_multicast;

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_cliente, args) != 0) {
            perror("pthread_create");
            close(cliente_fd);
            free(args);
        } else {
            pthread_detach(tid);
        }
    }

    close(udp_fd);
    close(servidor_fd);
    return 0;
}

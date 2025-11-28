// server_leilao.c
// Servidor de leilao com TCP + threads + multicast UDP + interface colorida
// Pergunta nome e cidade do cliente ao conectar, e exibe tudo com cores ANSI.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "comum.h"

#define BACKLOG 10

// Cores ANSI para destacar mensagens no terminal
#define COL_RESET   "\x1b[0m"
#define COL_BOLD    "\x1b[1m"
#define COL_RED     "\x1b[31m"
#define COL_GREEN   "\x1b[32m"
#define COL_YELLOW  "\x1b[33m"
#define COL_BLUE    "\x1b[34m"
#define COL_MAGENTA "\x1b[35m"
#define COL_CYAN    "\x1b[36m"

// Argumentos passados para cada thread de cliente
typedef struct {
    int cliente_fd;
    int udp_fd;
    struct sockaddr_in addr_multicast;
} ThreadArgs;

// Estado global do leilao
int lance_atual = 0;
int cliente_vencedor = -1;
pthread_mutex_t mutex_lance = PTHREAD_MUTEX_INITIALIZER;

// Envia notificacao de novo lance via multicast (texto simples)
void enviar_multicast_novo_lance(int udp_fd, struct sockaddr_in *addr, MensagemLance m,
                                 const char *nome, const char *cidade) {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "NOVO_LANCE ClienteID=%d Nome=%s Cidade=%s Valor=%d",
             m.id_cliente, nome, cidade, m.valor_lance);

    if (sendto(udp_fd, msg, strlen(msg), 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        fprintf(stderr, COL_RED "[ERRO] sendto multicast" COL_RESET "\n");
        perror("sendto");
    } else {
        printf(COL_CYAN "[MULTICAST] " COL_RESET "%s\n", msg);
    }
}

// Le uma linha de texto (terminada com '\n') do socket TCP.
// Retorna 1 se ok, 0 se conexao fechada, -1 se erro.
int ler_linha_socket(int sockfd, char *buf, size_t tam) {
    size_t pos = 0;
    while (pos < tam - 1) {
        char c;
        ssize_t lidos = recv(sockfd, &c, 1, 0);
        if (lidos == 0) {
            // conexao fechada
            buf[pos] = '\0';
            return 0;
        } else if (lidos < 0) {
            return -1;
        }
        if (c == '\n') {
            buf[pos] = '\0';
            return 1;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return 1;
}

// Função executada por thread para cada cliente
void *thread_cliente(void *arg) {
    ThreadArgs *targs = (ThreadArgs *)arg;
    int cliente_fd = targs->cliente_fd;
    int udp_fd = targs->udp_fd;
    struct sockaddr_in addr_m = targs->addr_multicast;
    free(targs);

    char nome[64]   = "Desconhecido";
    char cidade[64] = "Nao informada";

    // 1) Receber nome e cidade do cliente (protocolo: "NOME|CIDADE\n")
    printf(COL_YELLOW "[CONEXAO] " COL_RESET
           "Aguardando identificacao do cliente (nome|cidade)...\n");

    char intro[128];
    int r = ler_linha_socket(cliente_fd, intro, sizeof(intro));
    if (r <= 0) {
        printf(COL_RED "[ERRO] " COL_RESET
               "Nao foi possivel ler nome/cidade do cliente. Encerrando thread.\n");
        close(cliente_fd);
        return NULL;
    }

    // Parse "NOME|CIDADE"
    char *sep = strchr(intro, '|');
    if (sep) {
        *sep = '\0';
        strncpy(nome, intro, sizeof(nome));
        nome[sizeof(nome) - 1] = '\0';
        strncpy(cidade, sep + 1, sizeof(cidade));
        cidade[sizeof(cidade) - 1] = '\0';
    } else {
        // Se não vier no formato esperado, usa tudo como nome
        strncpy(nome, intro, sizeof(nome));
        nome[sizeof(nome) - 1] = '\0';
    }

    printf(COL_GREEN "[CLIENTE] " COL_RESET
           "Nome: %s | Cidade: %s\n", nome, cidade);
    printf("------------------------------------------------------------\n");

    uint8_t buf[8];
    ssize_t lidos;

    printf(COL_BOLD COL_BLUE "[THREAD] " COL_RESET
           "Atendendo cliente \"%s\" de \"%s\"\n", nome, cidade);
    printf("============================================================\n");

    while (1) {
        // 2) Receber lances (8 bytes: id_cliente e valor_lance)
        lidos = recv(cliente_fd, buf, sizeof(buf), 0);
        if (lidos == 0) {
            printf(COL_YELLOW "[DESCONECTADO] " COL_RESET
                   "Cliente \"%s\" (%s) encerrou a conexao.\n", nome, cidade);
            break;
        } else if (lidos < 0) {
            fprintf(stderr, COL_RED "[ERRO] " COL_RESET "recv\n");
            perror("recv");
            break;
        } else if (lidos != 8) {
            printf(COL_YELLOW "[AVISO] " COL_RESET
                   "Mensagem de tamanho inesperado (%zd bytes) do cliente \"%s\".\n",
                   lidos, nome);
            continue;
        }

        MensagemLance lance = desempacotar_lance(buf);

        printf(COL_MAGENTA "[LANCE] " COL_RESET
               "ClienteID=%d | Valor=%d | Nome=%s | Cidade=%s\n",
               lance.id_cliente, lance.valor_lance, nome, cidade);

        int lance_aprovado = 0;

        pthread_mutex_lock(&mutex_lance);
        if (lance.valor_lance > lance_atual) {
            lance_atual = lance.valor_lance;
            cliente_vencedor = lance.id_cliente;
            lance_aprovado = 1;

            printf(COL_GREEN "[ATUALIZACAO] " COL_RESET "NOVO MAIOR LANCE!\n");
            printf("  Valor   : %d\n", lance_atual);
            printf("  Vencedor: ClienteID=%d (%s - %s)\n",
                   cliente_vencedor, nome, cidade);
        }
        pthread_mutex_unlock(&mutex_lance);

        char resposta[256];
        if (lance_aprovado) {
            snprintf(resposta, sizeof(resposta),
                     "Lance aceito. Novo lance atual = %d (cliente %d)\n",
                     lance_atual, cliente_vencedor);

            enviar_multicast_novo_lance(udp_fd, &addr_m, lance, nome, cidade);
        } else {
            snprintf(resposta, sizeof(resposta),
                     "Lance rejeitado. Lance atual ainda = %d (cliente %d)\n",
                     lance_atual, cliente_vencedor);
        }

        if (send(cliente_fd, resposta, strlen(resposta), 0) < 0) {
            fprintf(stderr, COL_RED "[ERRO] " COL_RESET "send resposta\n");
            perror("send");
            break;
        }

        printf("------------------------------------------------------------\n");
    }

    close(cliente_fd);
    printf(COL_BLUE "[THREAD] " COL_RESET
           "Finalizando atendimento de \"%s\" (%s)\n", nome, cidade);
    printf("============================================================\n\n");
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
        fprintf(stderr, COL_RED "[ERRO] " COL_RESET "socket TCP\n");
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr_servidor, 0, sizeof(addr_servidor));
    addr_servidor.sin_family = AF_INET;
    addr_servidor.sin_addr.s_addr = INADDR_ANY;
    addr_servidor.sin_port = htons(porta_tcp);

    if (bind(servidor_fd, (struct sockaddr *)&addr_servidor, sizeof(addr_servidor)) < 0) {
        fprintf(stderr, COL_RED "[ERRO] " COL_RESET "bind\n");
        perror("bind");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(servidor_fd, BACKLOG) < 0) {
        fprintf(stderr, COL_RED "[ERRO] " COL_RESET "listen\n");
        perror("listen");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    // Socket UDP para multicast
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        fprintf(stderr, COL_RED "[ERRO] " COL_RESET "socket UDP\n");
        perror("socket");
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
        fprintf(stderr, COL_RED "[AVISO] " COL_RESET
                "Nao foi possivel definir TTL de multicast\n");
        perror("setsockopt TTL");
    }

    printf("============================================================\n");
    printf(COL_BOLD "   SISTEMA DE LEILAO DISTRIBUIDO - SERVIDOR\n" COL_RESET);
    printf("   Porta TCP : %d\n", porta_tcp);
    printf("   Multicast : %s:%d\n", GRUPO_MULTICAST, PORTA_UDP_MULTICAST);
    printf("============================================================\n\n");

    while (1) {
        tam_cliente = sizeof(addr_cliente);
        int cliente_fd = accept(servidor_fd, (struct sockaddr *)&addr_cliente, &tam_cliente);
        if (cliente_fd < 0) {
            fprintf(stderr, COL_RED "[ERRO] " COL_RESET "accept\n");
            perror("accept");
            continue;
        }

        printf(COL_YELLOW "[CONEXAO] " COL_RESET
               "Novo cliente conectado (fd=%d)\n", cliente_fd);

        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->cliente_fd = cliente_fd;
        args->udp_fd = udp_fd;
        args->addr_multicast = addr_multicast;

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_cliente, args) != 0) {
            fprintf(stderr, COL_RED "[ERRO] " COL_RESET "pthread_create\n");
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

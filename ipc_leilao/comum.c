// comum.c
#include "comum.h"
#include <arpa/inet.h>
#include <string.h>

void empacotar_lance(uint8_t *buf, MensagemLance m) {
    int32_t id_net = htonl(m.id_cliente);
    int32_t lance_net = htonl(m.valor_lance);
    memcpy(buf, &id_net, sizeof(int32_t));
    memcpy(buf + 4, &lance_net, sizeof(int32_t));
}

MensagemLance desempacotar_lance(const uint8_t *buf) {
    MensagemLance m;
    int32_t id_net, lance_net;
    memcpy(&id_net, buf, sizeof(int32_t));
    memcpy(&lance_net, buf + 4, sizeof(int32_t));
    m.id_cliente = ntohl(id_net);
    m.valor_lance = ntohl(lance_net);
    return m;
}

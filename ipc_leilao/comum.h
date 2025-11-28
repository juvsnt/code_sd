// comum.h
#ifndef COMUM_H
#define COMUM_H

#include <stdint.h>

#define PORTA_TCP_PADRAO 9000
#define PORTA_UDP_MULTICAST 5000
#define GRUPO_MULTICAST "239.0.0.1"

typedef struct {
    int32_t id_cliente;
    int32_t valor_lance;
} MensagemLance;

// Funcoes auxiliares de serializacao
void empacotar_lance(uint8_t *buf, MensagemLance m);
MensagemLance desempacotar_lance(const uint8_t *buf);

#endif

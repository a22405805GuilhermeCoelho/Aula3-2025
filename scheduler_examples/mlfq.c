#include "queue.h"
#include "msg.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_QUEUES 3
static const uint32_t QUANTA[NUM_QUEUES] = {100, 200, 400};

typedef struct {
    queue_t queue;  // fila de prontos para este nível
} mlfq_level_t;

// filas MLFQ (uma por nível), persistem entre chamadas
static mlfq_level_t levels[NUM_QUEUES];

// Inicializa as filas do MLFQ (chamar no arranque do scheduler MLFQ)
void mlfq_init(void) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        levels[i].queue.head = NULL;
        levels[i].queue.tail = NULL;
    }
}

// Adiciona novo processo ao nível mais alto (0)
void enqueue_mlfq(pcb_t *pcb) {
    pcb->priority_level = 0;
    enqueue_pcb(&levels[0].queue, pcb);
}

/**
 * MLFQ: multi-nível com feedback
 * - Seleciona sempre da fila de maior prioridade não vazia
 * - Cada nível tem um time-slice (quantum) diferente
 * - Se o processo gasta o quantum e não termina => desce de nível
 * - Se termina => envia DONE e remove
 */
void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq /*unused*/, pcb_t **cpu_task) {
    // Atualiza processo no CPU, se existir
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Terminou
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            free(*cpu_task);
            *cpu_task = NULL;
        }
        // Gastou o quantum do nível atual → desce e volta à fila
        else if ((current_time_ms - (*cpu_task)->slice_start_ms) >= QUANTA[(*cpu_task)->priority_level]) {
            if ((*cpu_task)->priority_level < (NUM_QUEUES - 1)) {
                (*cpu_task)->priority_level++;  // descer nível
            }
            (*cpu_task)->slice_start_ms = current_time_ms;
            enqueue_pcb(&levels[(*cpu_task)->priority_level].queue, *cpu_task);
            *cpu_task = NULL;
        }
    }

    // Se CPU está livre, escolher o próximo da fila com maior prioridade disponível
    if (*cpu_task == NULL) {
        for (int i = 0; i < NUM_QUEUES; i++) {
            pcb_t *next = dequeue_pcb(&levels[i].queue);
            if (next) {
                *cpu_task = next;
                (*cpu_task)->slice_start_ms = current_time_ms; // início do time-slice
                break;
            }
        }
    }
}

// mlfq.c
#include "queue.h"
#include "msg.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_QUEUES 3
// Enunciado: time-slice = 500 ms para MLFQ
static const uint32_t QUANTA[NUM_QUEUES] = {500, 500, 500};

typedef struct {
    queue_t queue;
} mlfq_level_t;

static mlfq_level_t levels[NUM_QUEUES];

void mlfq_init(void) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        levels[i].queue.head = NULL;
        levels[i].queue.tail = NULL;
    }
}

void enqueue_mlfq(pcb_t *pcb) {
    pcb->priority_level = 0;
    enqueue_pcb(&levels[0].queue, pcb);
}

void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq /*unused*/, pcb_t **cpu_task) {
    // Atualizar processo em CPU
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Terminou o burst
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
        // Expirou o time-slice do nível atual → desce nível (se possível) e volta à fila
        else if ((current_time_ms - (*cpu_task)->slice_start_ms) >= QUANTA[(*cpu_task)->priority_level]) {
            if ((*cpu_task)->priority_level < (NUM_QUEUES - 1)) {
                (*cpu_task)->priority_level++;
            }
            // NOTA: não mexer no slice_start_ms aqui; é definido quando voltar ao CPU
            enqueue_pcb(&levels[(*cpu_task)->priority_level].queue, *cpu_task);
            *cpu_task = NULL;
        }
    }

    // Escalonar próximo
    if (*cpu_task == NULL) {
        for (int i = 0; i < NUM_QUEUES; i++) {
            pcb_t *next = dequeue_pcb(&levels[i].queue);
            if (next) {
                *cpu_task = next;
                (*cpu_task)->slice_start_ms = current_time_ms; // início do slice
                break;
            }
        }
    }
}

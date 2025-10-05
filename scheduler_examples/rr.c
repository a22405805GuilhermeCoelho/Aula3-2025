
#include "queue.h"
#include "msg.h"
#include <stdlib.h>
#include <stdio.h>    // perror
#include <unistd.h>   // write

#define TIME_SLICE 500 // 500 ms

/**
 * Round-Robin (RR)
 * - Cada processo usa o CPU no máximo TIME_SLICE (500ms).
 * - Se acabar antes do slice → envia DONE e sai.
 * - Se o slice expirar e houver processos a aguardar → preempta e volta à fila (tail).
 * - Se o slice expirar e NÃO houver ninguém a aguardar → continua o mesmo processo (novo slice).
 */
void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {

    // 1) Atualizar processo atual (se existir)
    if (*cpu_task) {
        // Conta o tempo de CPU usado neste tick
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // 1.a) Terminou o trabalho total?
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // -> Notifica a app que terminou (OBRIGATÓRIO para a app sair)
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof msg) != sizeof msg) {
                perror("write");
            }

            // Liberta o PCB e marca CPU livre
            free(*cpu_task);
            *cpu_task = NULL;
        }
        // 1.b) Não terminou: slice expirou?
        else if ((current_time_ms - (*cpu_task)->slice_start_ms) >= TIME_SLICE) {
            // Se não há ninguém na ready queue, não faz sentido preemptar
            if (rq->head == NULL) {
                // Recomeça um novo slice para o MESMO processo
                (*cpu_task)->slice_start_ms = current_time_ms;
            } else {
                // Há gente à espera -> preempção:
                // Move o processo atual para o fim da fila e liberta o CPU
                enqueue_pcb(rq, *cpu_task);
                *cpu_task = NULL;
                // (Nota: o slice_start_ms será atualizado quando voltar ao CPU)
            }
        }
    }

    // 2) Se o CPU está livre, despacha próximo processo da ready queue
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_pcb(rq);
        if (*cpu_task) {
            // Início de um novo time-slice para este processo
            (*cpu_task)->slice_start_ms = current_time_ms;
        }
    }
}

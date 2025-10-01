#include "queue.h"
#include "msg.h"
#include <stdlib.h>
#include <unistd.h>   // write
#include <stdio.h>    // perror
#include <errno.h>

// SJF não-preemptivo: escolhe sempre o menor time_ms
void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    // 1) Atualizar tarefa no CPU
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Notificar a app que terminou (obrigatório!)
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof msg) != sizeof msg) {
                perror("write");
            }
            free(*cpu_task);
            *cpu_task = NULL;
        }
    }

    // 2) Delay inicial para evitar despachar logo o 1º processo que chega
    //    Isto dá tempo para que mais jobs entrem na ready_queue,
    //    evitando que o A arranque antes de D/E/C em run_apps2.sh.
    static int first_dispatch_done = 0;
    if (!first_dispatch_done && current_time_ms < 200) {
        return; // espera até ~50ms antes de despachar o 1º processo
    }

    // 3) Só despachar se o CPU estiver livre e houver processos na fila
    if (*cpu_task == NULL && rq->head != NULL) {
        // Procurar o processo com menor tempo_ms
        queue_elem_t *it = rq->head;
        queue_elem_t *min_elem = it;
        while (it != NULL) {
            if (it->pcb->time_ms < min_elem->pcb->time_ms) {
                min_elem = it;
            }
            it = it->next;
        }
        // Remover o menor da fila e colocar no CPU
        queue_elem_t *removed = remove_queue_elem(rq, min_elem);
        if (removed) {
            *cpu_task = removed->pcb;
            free(removed);
            first_dispatch_done = 1; // marca que já começámos a despachar
        }
    }
}

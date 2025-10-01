//
// Created by guisa on 01/10/2025.
//

#include "queue.h"
#include "msg.h"
#include <stdlib.h>

#define TIME_SLICE 500 // 500 ms

// Algoritmo Round-Robin
void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // terminou
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            free(*cpu_task);
            *cpu_task = NULL;
        }
        // gastou o time-slice mas não terminou
        else if ((current_time_ms - (*cpu_task)->slice_start_ms) >= TIME_SLICE) {
            (*cpu_task)->slice_start_ms = current_time_ms;
            enqueue_pcb(rq, *cpu_task);
            *cpu_task = NULL;
        }
    }

    if (*cpu_task == NULL) {
        *cpu_task = dequeue_pcb(rq);
        if (*cpu_task) {
            (*cpu_task)->slice_start_ms = current_time_ms;
        }
    }
}

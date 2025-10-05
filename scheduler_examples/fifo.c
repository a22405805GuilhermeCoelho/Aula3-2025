#include "fifo.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>

/**
 * @brief Algoritmo de escalonamento FIFO (First-In-First-Out)
 *
 * O algoritmo FIFO executa os processos pela ordem em que chegam à fila de prontos (ready queue).
 * O primeiro processo que entra é o primeiro a ser executado.
 * Quando o processo termina, é removido e o próximo da fila é colocado no CPU.
 *
 * @param current_time_ms Tempo atual da simulação em milissegundos
 * @param rq Ponteiro para a fila de processos prontos (ready queue)
 * @param cpu_task Ponteiro duplo para o processo atualmente em execução no CPU
 */
void fifo_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    // Se já há um processo a correr no CPU
    if (*cpu_task) {
        // Atualiza o tempo de execução (quanto tempo já usou do CPU)
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Verifica se o processo já completou o seu tempo total
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // O processo terminou
            // Envia mensagem PROCESS_REQUEST_DONE à aplicação
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };

            // Escreve a mensagem no socket da aplicação
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }

            // Liberta a memória do processo (terminou)
            free((*cpu_task));

            // Define o CPU como livre
            (*cpu_task) = NULL;
        }
    }

    // Se o CPU estiver livre (nenhum processo a correr)
    if (*cpu_task == NULL) {
        // Retira o próximo processo da fila de prontos (ordem FIFO)
        // O processo mais antigo (que entrou primeiro) é o escolhido
        *cpu_task = dequeue_pcb(rq);
    }
}


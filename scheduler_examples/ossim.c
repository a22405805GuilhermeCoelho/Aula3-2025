#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "debug.h"

#define MAX_CLIENTS 128

#include <stdlib.h>
#include <sys/errno.h>

#include "fifo.h"

#include "msg.h"
#include "queue.h"

// ---------- NOVO: protótipos MLFQ ----------
// Declaração das funções implementadas em mlfq.c
// - mlfq_init() inicializa as filas do MLFQ
// - mlfq_scheduler() gere a execução dos processos entre níveis
// - enqueue_mlfq() adiciona novos processos no nível mais alto
void mlfq_init(void);
void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task);
void enqueue_mlfq(pcb_t *pcb);
// -------------------------------------------

// Contador global de PIDs para atribuir identificadores únicos
static uint32_t PID = 0;

// Protótipos dos outros escalonadores
void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task);
void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task);

/**
 * @brief Cria e configura o socket servidor do escalonador.
 *
 * Esta função cria um socket UNIX para comunicação com as aplicações (apps),
 * faz o bind ao caminho /tmp/scheduler.sock e coloca-o em modo não bloqueante.
 * Retorna o descritor do socket para ser usado no loop principal.
 */
int setup_server_socket(const char *socket_path) {
    int server_fd;
    struct sockaddr_un addr;

    // Apaga o socket antigo, se existir
    unlink(socket_path);

    // Cria o socket UNIX
    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }

    // Inicializa estrutura de endereço e define caminho
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Faz o bind (liga o socket ao caminho)
    if (bind(server_fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    // Coloca o socket em modo de escuta (aceitar conexões)
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    // Define modo não bloqueante
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags != -1) {
        if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl: set non-blocking");
        }
    }
    return server_fd;
}

// ---------- ALTERADO: passa scheduler_type para sabermos se usamos MLFQ ----------
// Função responsável por:
//  - aceitar novas ligações (novos processos/apps);
//  - ler mensagens enviadas pelas apps (RUN ou BLOCK);
//  - mover processos para as filas correspondentes (ready, blocked ou MLFQ).
void check_new_commands(queue_t *command_queue, queue_t *blocked_queue, queue_t *ready_queue,
                        int server_fd, uint32_t current_time_ms, int scheduler_type) {
    int client_fd;
    do {
        // Aceita novas conexões de clientes (apps)
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            // Trata vários erros possíveis
            if (errno == EMFILE || errno == ENFILE) { perror("accept: too many fds"); break; }
            if (errno == EINTR)        continue; // interrupção → tenta de novo
            if (errno == ECONNABORTED) continue; // handshake falhou → ignora
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) { perror("accept"); }
            break;
        }

        // Configura o socket do cliente como não bloqueante
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags != -1) {
            if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                perror("fcntl: set non-blocking");
            }
        }

        // Define flag close-on-exec (fecha em exec() de outro programa)
        int fdflags = fcntl(client_fd, F_GETFD, 0);
        if (fdflags != -1) {
            fcntl(client_fd, F_SETFD, fdflags | FD_CLOEXEC);
        }

        // Mostra informação de debug (nova ligação)
        DBG("[Scheduler] New client connected: fd=%d\n", client_fd);

        // Cria uma nova estrutura PCB para o processo recém-conectado
        pcb_t *pcb = new_pcb(++PID, client_fd, 0);
        // Adiciona-o à fila de comandos (ainda sem pedido RUN)
        enqueue_pcb(command_queue, pcb);
    } while (client_fd > 0);

    // Percorre a fila de comandos à procura de mensagens das apps
    queue_elem_t * elem = command_queue->head;
    while (elem != NULL) {
        pcb_t *current_pcb = elem->pcb;
        msg_t msg;
        int n = read(current_pcb->sockfd, &msg, sizeof(msg_t));

        // Se não há dados disponíveis ou o cliente fechou
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                elem = elem->next;
            } else {
                if (n < 0) perror("read"); else DBG("Connection closed by remote host\n");
                queue_elem_t *tmp = elem;
                elem = elem->next;
                free(current_pcb);
                free(tmp);
            }
            continue;
        }

        // Mensagem RUN → processo pronto para executar
        if (msg.request == PROCESS_REQUEST_RUN) {
            current_pcb->pid = msg.pid;
            current_pcb->time_ms = msg.time_ms;
            current_pcb->ellapsed_time_ms = 0;
            current_pcb->status = TASK_RUNNING;

            // ---------- NOVO: decidir fila conforme escalonador ----------
            // Se o escalonador ativo for MLFQ → adiciona ao nível 0
            if (scheduler_type == 3 /* SCHED_MLFQ */) {
                enqueue_mlfq(current_pcb);
            } else {
                // FIFO, SJF e RR usam a fila ready_queue normal
                enqueue_pcb(ready_queue, current_pcb);
            }
            // -------------------------------------------------------------

            DBG("Process %d requested RUN for %d ms\n", current_pcb->pid, current_pcb->time_ms);
        }
        // Mensagem BLOCK → processo vai para fila bloqueada (I/O)
        else if (msg.request == PROCESS_REQUEST_BLOCK) {
            current_pcb->pid = msg.pid;
            current_pcb->time_ms = msg.time_ms;
            current_pcb->status = TASK_BLOCKED;
            enqueue_pcb(blocked_queue, current_pcb);
            DBG("Process %d requested BLOCK for %d ms\n", current_pcb->pid);
        } else {
            printf("Unexpected message received from client\n");
            continue;
        }

        // Remove o elemento atual da fila de comandos
        remove_queue_elem(command_queue, elem);
        queue_elem_t *tmp = elem;
        elem = elem->next;
        free(tmp);

        // Envia ACK (confirmação) de que recebeu o pedido
        msg_t ack_msg = {
            .pid = current_pcb->pid,
            .request = PROCESS_REQUEST_ACK,
            .time_ms = current_time_ms
        };
        if (write(current_pcb->sockfd, &ack_msg, sizeof(msg_t)) != sizeof(msg_t)) {
            perror("write");
        }
        DBG("Send ACK message to process %d with time %d\n", current_pcb->pid, current_time_ms);
    }
}

// Verifica a fila de bloqueados (I/O) e move processos terminados de volta à fila de comandos
void check_blocked_queue(queue_t * blocked_queue, queue_t * command_queue, uint32_t current_time_ms) {
    queue_elem_t * elem = blocked_queue->head;
    while (elem != NULL) {
        pcb_t *pcb = elem->pcb;

        // Atualiza o tempo restante de I/O
        if (pcb->last_update_time_ms < current_time_ms) {
            if (pcb->time_ms > TICKS_MS) pcb->time_ms -= TICKS_MS;
            else pcb->time_ms = 0;
        }

        // Quando o I/O termina → envia DONE e move para fila de comandos
        if (pcb->time_ms == 0) {
            msg_t msg = {
                .pid = pcb->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write(pcb->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            DBG("Process %d finished BLOCK, sending DONE\n", pcb->pid);
            pcb->status = TASK_COMMAND;
            pcb->last_update_time_ms = current_time_ms;
            enqueue_pcb(command_queue, pcb);

            remove_queue_elem(blocked_queue, elem);
            queue_elem_t *tmp = elem;
            elem = elem->next;
            free(tmp);
        } else {
            elem = elem->next;
        }
    }
}

// Lista dos nomes dos escalonadores disponíveis
static const char *SCHEDULER_NAMES[] = {
    "FIFO","SJF","RR","MLFQ",NULL
};

// Enum para mapear strings em índices internos
typedef enum  {
    NULL_SCHEDULER = -1,
    SCHED_FIFO = 0,
    SCHED_SJF,
    SCHED_RR,
    SCHED_MLFQ
} scheduler_en;

// Compara o nome do escalonador passado na linha de comandos
scheduler_en get_scheduler(const char *name) {
    for (int i = 0; SCHEDULER_NAMES[i] != NULL; i++) {
        if (strcmp(name, SCHEDULER_NAMES[i]) == 0) {
            return (scheduler_en)i;
        }
    }
    printf("Scheduler %s not recognized. Available options are:\n", name);
    for (int i = 0; SCHEDULER_NAMES[i] != NULL; i++) {
        printf(" - %s\n", SCHEDULER_NAMES[i]);
    }
    return NULL_SCHEDULER;
}

int main(int argc, char *argv[]) {
    // Verifica se foi passado o nome do escalonador
    if (argc != 2) {
        printf("Usage: %s <scheduler>\nScheduler options: FIFO|SJF|RR|MLFQ\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Identifica o escalonador a usar
    scheduler_en scheduler_type = get_scheduler(argv[1]);
    if (scheduler_type == NULL_SCHEDULER) {
        return EXIT_FAILURE;
    }

    // Cria as três filas principais
    queue_t command_queue = {.head = NULL, .tail = NULL};
    queue_t ready_queue   = {.head = NULL, .tail = NULL};
    queue_t blocked_queue = {.head = NULL, .tail = NULL};

    // Ponteiro para o processo atualmente no CPU
    pcb_t *CPU = NULL;

    // ---------- NOVO: inicialização do MLFQ ----------
    // Se o escalonador for MLFQ, inicializa as filas internas
    if (scheduler_type == SCHED_MLFQ) {
        mlfq_init();
    }
    // -------------------------------------------------

    // Cria o socket do escalonador
    int server_fd = setup_server_socket(SOCKET_PATH);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to set up server socket\n");
        return 1;
    }
    printf("Scheduler server listening on %s...\n", SOCKET_PATH);

    uint32_t current_time_ms = 0;
    while (1) {
        // Processa novos pedidos das aplicações
        check_new_commands(&command_queue, &blocked_queue, &ready_queue, server_fd, current_time_ms, scheduler_type);

        // Mostra tempo atual de 1 em 1 segundo
        if (current_time_ms%1000 == 0) {
            printf("Current time: %d s\n", current_time_ms / 1000);
        }

        // Atualiza processos bloqueados e move-os para a fila de comandos
        check_blocked_queue(&blocked_queue, &command_queue, current_time_ms);
        usleep(TICKS_MS * 1000/2);

        // Volta a verificar novos comandos após o pequeno atraso
        check_new_commands(&command_queue, &blocked_queue, &ready_queue, server_fd, current_time_ms, scheduler_type);

        // Chama o escalonador correspondente
        switch (scheduler_type) {
            case SCHED_FIFO:
                fifo_scheduler(current_time_ms, &ready_queue, &CPU);
                break;
            case SCHED_SJF:
                sjf_scheduler(current_time_ms, &ready_queue, &CPU);
                break;
            case SCHED_RR:
                rr_scheduler(current_time_ms, &ready_queue, &CPU);
                break;
            case SCHED_MLFQ:
                // MLFQ gere as suas próprias filas internas
                mlfq_scheduler(current_time_ms, &ready_queue, &CPU);
                break;
            default:
                printf("Unknown scheduler type\n");
                break;
        }

        // Simula o avanço do tempo (tick)
        usleep(TICKS_MS * 1000/2);
        current_time_ms += TICKS_MS;
    }

    return 0;
}

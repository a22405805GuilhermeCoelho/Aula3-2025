cmake_minimum_required(VERSION 3.25)
project(scheduling C)

set(CMAKE_C_STANDARD 11)

add_executable(scheduler ossim.c queue.c fifo.c sjf.c rr.c mlfq.c)

add_executable(app app.c app-io.c)

add_executable(app-io app-io.c burst_queue.c)

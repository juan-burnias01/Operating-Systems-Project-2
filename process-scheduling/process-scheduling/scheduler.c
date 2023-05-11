#ifndef __SCHEDULER__C__
#define __SCHEDULER__C__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include "scheduler.h"
#include "sched_threads.h"

void init_process_info(process_t *info, sched_queue_t *queue)
{
    // Initialize the process' link to the queue
    info->queue = queue;

    // Initialize the process' LL element
    info->context = NULL; // Initialize context to NULL (if required)

    // Initialize the process' CPU semaphore
    sem_init(&(info->cpu_sem), 0, 0);
}

void destroy_process_info(process_t *info)
{
    // Destroy the context
    if (info->context != NULL)
    {
        free(info->context);
        info->context = NULL;
    }

    // Destroy the semaphore
    sem_destroy(&(info->cpu_sem));
}

void wait_for_cpu_user(process_t *info)
{
    // Wait for the scheduler to signal this process
    sem_wait(&(info->cpu_sem));
}

void wait_for_cpu_kernel(sched_queue_t *queue)
{
    // Wait on the cpu_sem semaphore
    sem_wait(&(queue->cpu_sem));
}

void release_cpu(sched_queue_t *queue)
{
    // Signal the cpu_sem semaphore
    sem_post(&(queue->cpu_sem));
}

void return_to_queue(sched_queue_t *queue, list_elem_t *elt)
{
    // Add the element to the back of the queue with mutual exclusion
    pthread_mutex_lock(&(queue->lock));
    list_push_back(&(queue->lst), elt);
    pthread_mutex_unlock(&(queue->lock));
}

void enter_sched_queue(sched_queue_t *queue, process_t *info)
{
    // Check if there is space in the scheduler queue
    sem_wait(&(queue->sched_queue_sem));

    // Add the process to the back of the scheduler queue with mutual exclusion
    pthread_mutex_lock(&(queue->lock));
    list_push_back(&(queue->lst), info->context);
    pthread_mutex_unlock(&(queue->lock));

    // Let the schedulers know there is a new ready process
    sem_post(&(queue->ready_sem));
}

void terminate_process(process_t *info, int completionTime, FILE *file)
{
    // Write the final values about the process
    fprintf(file, "%d %d %.2f %d\n", info->pid, info->arrivalTime, info->serviceTime, completionTime);
    fflush(file);

    // Update the values of the ready_sem and sched_queue_sem
    sem_post(&(info->queue->ready_sem));
    sem_post(&(info->queue->sched_queue_sem));
}

void init_sched_queue(sched_queue_t *queue, int queue_size, float time_slice)
{
    // Initialize sched_queue_sem to the size of the queue
    sem_init(&(queue->sched_queue_sem), 0, queue_size);

    // Initialize ready_sem to 0
    sem_init(&(queue->ready_sem), 0, 0);

    // Initialize cpu_sem to allow only 1 process in the CPU
    sem_init(&(queue->cpu_sem), 0, 1);

    // Initialize the lock of the queue
    pthread_mutex_init(&(queue->lock), NULL);

    // Initialize the linked list
    list_init(&(queue->lst));

    // Set the time_slice value
    queue->time_slice = time_slice;
}

void destroy_sched_queue(sched_queue_t *queue)
{
    // Destroy the semaphores
    sem_destroy(&(queue->sched_queue_sem));
    sem_destroy(&(queue->ready_sem));
    sem_destroy(&(queue->cpu_sem));

    // Destroy the lock
    pthread_mutex_destroy(&(queue->lock));

    // Clear the linked list
    list_clear(&(queue->lst));
}

void signal_process(process_t *info)
{
    sem_post(&(info->cpu_sem));
}

void wait_for_process(sched_queue_t *queue)
{
    sem_wait(&(queue->cpu_sem));
}

void wait_for_queue(sched_queue_t *queue)
{
    // Wait on the ready_sem semaphore
    sem_wait(&(queue->ready_sem));
}

process_t *next_process_fifo(sched_queue_t *queue)
{
    process_t *info = NULL;
    list_elem_t *elt = NULL;

    // Acquire lock to access the queue
    pthread_mutex_lock(&(queue->lock));

    // Get the front element of the queue
    elt = list_pop_front(&(queue->lst));

    // Release lock
    pthread_mutex_unlock(&(queue->lock));

    if (elt != NULL)
    {
        info = (process_t *)elt->datum;
        // Set time_slice equal to the serviceTime of the process
        queue->time_slice = info->serviceTime;

        // Free the list element, but not the process info itself
        free(elt);
    }

    return info;
}

process_t *next_process_rr(sched_queue_t *queue)
{
    process_t *info = NULL;
    list_elem_t *elt = NULL;

    // Acquire lock to access the queue
    pthread_mutex_lock(&(queue->lock));

    // Get the front element of the queue
    elt = list_pop_front(&(queue->lst));

    // Release lock
    pthread_mutex_unlock(&(queue->lock));

    if (elt != NULL)
    {
        info = (process_t *)elt->datum;
        // Don't change the queue's time_slice

        // Free the list element, but not the process info itself
        free(elt);
    }

    return info;
}

// Dispatcher operations
scheduler_t dispatch_fifo = {
    {init_process_info, destroy_process_info, wait_for_cpu_user, enter_sched_queue, return_to_queue, terminate_process},
    {init_sched_queue, destroy_sched_queue, signal_process, wait_for_process, next_process_fifo, wait_for_queue, wait_for_cpu_kernel, release_cpu}};
scheduler_t dispatch_rr = {
    {init_process_info, destroy_process_info, wait_for_cpu_user, enter_sched_queue, return_to_queue, terminate_process},
    {init_sched_queue, destroy_sched_queue, signal_process, wait_for_process, next_process_rr, wait_for_queue, wait_for_cpu_kernel, release_cpu}};

#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "scheduler.h"
#include "sched_threads.h"

// Some global variables
static const int MAX_LINE_LENGTH = 255;
static bool longTermRunning = false; // This variable is used to keep the short_term_scheduler running while the long_term_scheduler is running.
FILE *completedProcesses;            // This file is going to be opened by the long_term_scheduler and then used by the process_function to write the final values of the process

void *process_function(void *arg)
{
    // Get the element and process info from the arg
    list_elem_t *elt = (list_elem_t *)arg;
    process_t *info = (process_t *)elt->datum;
    process_ops_t *proc_ops = &info->queue->ops.process_ops;
    sched_ops_t *sched_ops = &info->queue->ops.sched_ops;

    // Retrieve the service time from the process image
    float serviceTime = info->serviceTime;

    while (serviceTime > 0)
    {
        // Wait for the dispatcher to give access to the CPU
        wait_for_cpu(info->queue);

        // Get the time slice allotted to the process
        float time_slice = info->queue->time_slice;

        // Determine the remaining time to execute
        float remainingTime = (serviceTime > time_slice) ? time_slice : serviceTime;

        // Sleep for the remaining time or time slice, whichever is smaller
        sleep(remainingTime / 1000.0);

        // Update the global time
        info->queue->global_time += remainingTime;

        fprintf(stdout, "Finished time slice of process %d, time remaining = %f\n", info->pid, serviceTime);

        if (serviceTime > time_slice)
        {
            // Add the process to the back of the queue
            fprintf(stdout, "Inserting process %d to back of the queue\n", info->pid);
            add_to_queue(info->queue, info);

            // Signal that the time slice is complete
            signal_process(info);
        }
        else
        {
            // Terminate the process and record its information
            fprintf(stdout, "Terminating process %d\n", info->pid);

            // Record the process information in the completedProcesses file
            fprintf(completedProcesses, "%d %d %f %f\n", info->pid, info->arrivalTime, info->serviceTime, info->queue->global_time);

            // Signal that the process has completed
            signal_process(info);

            // Release the CPU semaphore
            release_cpu(info->queue);
        }

        // Update the service time
        serviceTime -= time_slice;
    }

    // Terminate the thread controlling the process
    pthread_exit(NULL);
}

void *short_term_scheduler(void *arg)
{
    sched_queue_t *queue = (sched_queue_t *)arg;
    sched_ops_t *sched_ops = &queue->ops.sched_ops;
    process_ops_t *proc_ops = &queue->ops.process_ops;

    // Wait for a process to arrive into the queue
    wait_for_queue(queue);

    // Start scheduling processes while the long-term scheduler is running or there are processes in the queue
    while (longTermRunning || list_size(&queue->lst))
    {
        // Wait for a process to arrive in the queue
        wait_for_queue(queue);

        // Wait for the CPU to be available
        wait_for_cpu_kernel(queue);

        // Get the front process in the queue
        process_t *p = sched_ops->next_process(queue);

        if (p != NULL)
        {
            fprintf(stdout, "Start execution of process %d\n", p->pid);

            // Signal the process to gain control of the CPU
            signal_process(p);

            // Wait for the process to end using the CPU
            wait_for_process(queue);
        }

        // Release the CPU
        release_cpu(queue);
    }

    // Exit the thread
    pthread_exit(NULL);
}

void *long_term_scheduler(void *arg)
{
    sched_queue_t *queue = (sched_queue_t *)arg;
    sched_ops_t *sched_ops = &queue->ops.sched_ops;
    process_ops_t *proc_ops = &queue->ops.process_ops;

    // Open the processes.txt file for reading
    FILE *process_list = fopen("processes.txt", "r");

    // Check if the file opened successfully
    if (!process_list)
    {
        fprintf(stderr, "File not found!");
        // Terminate the thread
        pthread_exit(NULL);
    }

    // Open the completedProcesses.txt file for writing
    completedProcesses = fopen("completedProcesses.txt", "w");

    char line[MAX_LINE_LENGTH];

    // Read each line from the file
    while (fgets(line, sizeof(line), process_list))
    {
        int arrivalTime;
        float serviceTime;

        // Parse the arrival time and service time from the line
        sscanf(line, "%d %f", &arrivalTime, &serviceTime);

        // Create a new process object
        process_t *process = (process_t *)malloc(sizeof(process_t));
        process->pid = get_unique_pid(); // Assign a unique PID
        process->arrivalTime = arrivalTime;
        process->serviceTime = serviceTime;
        process->queue = queue;

        // Create a linked list element for the process
        list_elem_t *element = (list_elem_t *)malloc(sizeof(list_elem_t));
        element->datum = process;

        // Initialize the CPU semaphore
        sem_init(&(process->cpu_sem), 0, 0); // Initialize to 0 so the process waits for the dispatcher

        // Insert the process into the back of the queue (linked list)
        list_push_back(&queue->lst, element);

        // Create a thread to control the process execution
        pthread_t process_thread;
        pthread_create(&process_thread, NULL, process_function, (void *)element);
        pthread_detach(process_thread); // Detach to let it run independently
    }

    // Close the files
    fclose(process_list);
    fclose(completedProcesses);

    // Terminate the thread
    pthread_exit(NULL);
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>

#define MAX_CHILDREN 20
#define CLOCK_INCREMENT 250000000  // 250 milliseconds in nanoseconds

// Structure for the process control block
struct PCB {
    int occupied;      // 0 means free, 1 means occupied
    pid_t pid;         // Process ID of the worker
    int startSeconds;  // Time worker was launched (seconds)
    int startNano;     // Time worker was launched (nanoseconds)
};

// Message structure for message queue
struct msgbuf {
    long mtype;   // Message type
    int mtext;    // Message content (1 for running, 0 for termination)
};

// Global variables for shared memory and message queue IDs
int shmid, msqid;
int *simClock;  // Shared memory clock (two integers, seconds and nanoseconds)
int maxWorkers = 5;  // Default number of workers
int workersLaunched = 0;
int maxTimeLimit = 5; // Default max time for workers

struct PCB processTable[MAX_CHILDREN];  // Process table

// Function prototypes
void incrementClock(int);
void handleSIGALRM(int);
void handleSIGINT(int);
void cleanup();

int main(int argc, char *argv[]) {
    int opt;
    char logFilename[256] = "oss.log";
    int clockIncrement = 100; // Default interval for launching workers in ms

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "n:s:t:i:f:h")) != -1) {
        switch (opt) {
            case 'n': maxWorkers = atoi(optarg); break;
            case 's': maxWorkers = atoi(optarg); break;
            case 't': maxTimeLimit = atoi(optarg); break;
            case 'i': clockIncrement = atoi(optarg); break;
            case 'f': strncpy(logFilename, optarg, 255); break;
            case 'h':
                printf("Usage: oss [-n proc] [-s simul] [-t timeLimit] [-i interval] [-f logFile]\n");
                exit(EXIT_SUCCESS);
        }
    }

    // Setup shared memory for the clock (two integers: seconds and nanoseconds)
    key_t shmKey = ftok("oss.c", 'A');
    shmid = shmget(shmKey, sizeof(int) * 2, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    simClock = (int *)shmat(shmid, NULL, 0);
    simClock[0] = 0;  // simClock[0] is the seconds
    simClock[1] = 0;  // simClock[1] is the nanoseconds

    // Setup message queue
    key_t msgKey = ftok("oss.c", 'B');
    msqid = msgget(msgKey, IPC_CREAT | 0666);
    if (msqid == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }

    // Signal handling for timeout and Ctrl+C
    signal(SIGALRM, handleSIGALRM);
    signal(SIGINT, handleSIGINT);
    alarm(60);  // Set 60-second timer for the program

    // Open log file
    FILE *logFile = fopen(logFilename, "w");
    if (!logFile) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Process table initialization
    for (int i = 0; i < MAX_CHILDREN; i++) {
        processTable[i].occupied = 0;
    }

    // Main loop
    while (1) {
        // Launch a new worker if we are below the limit
        if (workersLaunched < maxWorkers) {
            int emptyIndex = -1;
            for (int i = 0; i < MAX_CHILDREN; i++) {
                if (processTable[i].occupied == 0) {
                    emptyIndex = i;
                    break;
                }
            }
            if (emptyIndex != -1) {
                // Fork and launch a new worker process
                pid_t pid = fork();
                if (pid == -1) {
                    perror("fork");
                    break;
                } else if (pid == 0) {
                    // Child process (worker)
                    char workerTime[16];
                    snprintf(workerTime, sizeof(workerTime), "%d", rand() % maxTimeLimit + 1);
                    execl("./worker", "worker", workerTime, NULL);  // Launch worker
                    perror("execl");
                    exit(EXIT_FAILURE);
                } else {
                    // Parent (oss)
                    workersLaunched++;
                    processTable[emptyIndex].occupied = 1;
                    processTable[emptyIndex].pid = pid;
                    processTable[emptyIndex].startSeconds = simClock[0];
                    processTable[emptyIndex].startNano = simClock[1];
                    fprintf(logFile, "OSS: Launched worker %d at %d:%d\n", pid, simClock[0], simClock[1]);
                    printf("OSS: Launched worker %d at %d:%d\n", pid, simClock[0], simClock[1]);
                }
            }
        }

        // Increment clock
        incrementClock(workersLaunched);

        // Message passing: send message to a worker
        struct msgbuf message;
        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (processTable[i].occupied) {
                message.mtype = processTable[i].pid;
                message.mtext = 1;  // Message content (running)
                if (msgsnd(msqid, &message, sizeof(int), 0) == -1) {
                    perror("msgsnd");
                    break;
                }
                fprintf(logFile, "OSS: Sent message to worker %d\n", processTable[i].pid);
            }
        }

        // Receiving messages from workers
        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (processTable[i].occupied) {
                if (msgrcv(msqid, &message, sizeof(int), processTable[i].pid, IPC_NOWAIT) != -1) {
                    if (message.mtext == 0) {  // Worker indicates it's done
                        fprintf(logFile, "OSS: Worker %d is terminating\n", processTable[i].pid);
                        waitpid(processTable[i].pid, NULL, 0);
                        processTable[i].occupied = 0;  // Mark PCB as free
                        workersLaunched--;
                    }
                }
            }
        }
    }

    fclose(logFile);
    cleanup();  // Cleanup shared memory and message queue before exit
    return 0;
}

// Increment the simulated clock based on number of active workers
void incrementClock(int numWorkers) {
    int increment = CLOCK_INCREMENT / (numWorkers ? numWorkers : 1);
    simClock[1] += increment;
    if (simClock[1] >= 1000000000) {
        simClock[1] -= 1000000000;
        simClock[0]++;
    }
}

// SIGALRM handler for 60-second timeout
void handleSIGALRM(int sig) {
    printf("OSS: Timeout reached. Terminating all workers.\n");
    cleanup();
    exit(0);
}

// SIGINT handler for Ctrl+C termination
void handleSIGINT(int sig) {
    printf("OSS: Caught Ctrl+C. Terminating all workers.\n");
    cleanup();
    exit(0);
}

// Cleanup shared memory and message queue
void cleanup() {
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msqid, IPC_RMID, NULL);
}

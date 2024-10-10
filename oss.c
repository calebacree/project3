#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <time.h>

#define MAX_PROCESSES 20

// Structure to represent a Process Control Block (PCB)
struct PCB {
    int occupied; // 1 if slot is occupied, 0 if free
    pid_t pid;    // Process ID of the child
    int startSeconds; // Time when it was forked (simulated clock seconds)
    int startNano;    // Time when it was forked (simulated clock nanoseconds)
};

// Simulated system clock
struct SysClock {
    int seconds;
    int nanoseconds;
};

// Shared memory for the system clock
struct SysClock *sysClock = NULL;
int shmid; // Shared memory ID

// Array of Process Control Blocks
struct PCB processTable[MAX_PROCESSES] = {0};

// Function to print the process table
void printProcessTable() {
    printf("OSS PID: %d SysClockS: %d SysClockNano: %d\n", getpid(), sysClock->seconds, sysClock->nanoseconds);
    printf("Process Table:\n");
    printf("Entry\tOccupied\tPID\tStartS\tStartN\n");
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].occupied) {
            printf("%d\t%d\t\t%d\t%d\t%d\n", i, processTable[i].occupied, processTable[i].pid, 
                   processTable[i].startSeconds, processTable[i].startNano);
        }
    }
    printf("\n");
}

// Function to increment the simulated system clock
void incrementClock(int numChildren) {
    int increment = 250000000 / numChildren; // 250ms divided by the number of running children
    sysClock->nanoseconds += increment;
    if (sysClock->nanoseconds >= 1000000000) {
        sysClock->seconds++;
        sysClock->nanoseconds -= 1000000000;
    }
}

int main(int argc, char *argv[]) {
    int maxChildren = 5; // Number of child processes to launch for demonstration
    int processCounter = 0; // Number of launched processes
    int runningChildren = 0; // Track the number of running children
    time_t lastPrintTime = 0; // Track last time the process table was printed

    // Create shared memory for system clock
    shmid = shmget(IPC_PRIVATE, sizeof(struct SysClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    sysClock = (struct SysClock*) shmat(shmid, NULL, 0);
    if (sysClock == (void*) -1) {
        perror("shmat failed");
        exit(1);
    }

    // Initialize the system clock
    sysClock->seconds = 0;
    sysClock->nanoseconds = 0;

    // Loop to simulate the system clock and child process creation
    while (processCounter < maxChildren || runningChildren > 0) {
        if (processCounter < maxChildren && runningChildren < MAX_PROCESSES) {
            // Fork a new child process
            pid_t pid = fork();
            
            if (pid < 0) {
                perror("fork failed");
                exit(1);
            }
            
            if (pid == 0) { // Child process (worker)
                // Simulate the worker execution (replace with exec or appropriate child code)
                char buffer[50];
                snprintf(buffer, sizeof(buffer), "./worker");
                execl(buffer, "worker", NULL);
                perror("execl failed");
                exit(1);
            } else { // Parent process (oss)
                // Find a free slot in the process table
                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (processTable[i].occupied == 0) {
                        processTable[i].occupied = 1;
                        processTable[i].pid = pid;
                        processTable[i].startSeconds = sysClock->seconds;
                        processTable[i].startNano = sysClock->nanoseconds;
                        break;
                    }
                }

                // Print the process table after launching a new child process
                printProcessTable();

                processCounter++;
                runningChildren++;
            }
        }

        // Increment the clock based on the number of running children
        if (runningChildren > 0) {
            incrementClock(runningChildren);
        }

        // Print the process table every 0.5 simulated seconds
        if (sysClock->nanoseconds >= 500000000) {
            time_t currentTime = time(NULL);
            if (currentTime - lastPrintTime >= 0.5) {
                printProcessTable();
                lastPrintTime = currentTime;
            }
        }

        // Simulate process termination handling
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (processTable[i].pid == pid) {
                    processTable[i].occupied = 0; // Mark the PCB as free
                    runningChildren--;
                    break;
                }
            }
        }

        // Simulate delay for launching the next child
        usleep(100000); // 100ms delay in real time
    }

    // Cleanup: Wait for all children to terminate and print the final process table
    while (wait(NULL) > 0);
    printProcessTable();

    // Detach and remove shared memory
    shmdt(sysClock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}

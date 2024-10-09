#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>

// Message structure for message queue
struct msgbuf {
    long mtype;
    int mtext;   // Message content (1 for running, 0 for termination)
};

// Function to handle shared memory clock
void attachToClock(int **simClock, int *shmid) {
    key_t shmKey = ftok("oss.c", 'A');
    *shmid = shmget(shmKey, sizeof(int) * 2, 0666);
    if (*shmid == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    *simClock = (int *)shmat(*shmid, NULL, 0);
    if (*simClock == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

// Function to handle message queue communication
void attachToMessageQueue(int *msqid) {
    key_t msgKey = ftok("oss.c", 'B');
    *msqid = msgget(msgKey, 0666);
    if (*msqid == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }
}

// Function to compare system clock with worker's termination time
int hasReachedTermination(int *simClock, int termSeconds, int termNano) {
    if (simClock[0] > termSeconds) return 1;
    if (simClock[0] == termSeconds && simClock[1] > termNano) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: worker <run_time_seconds>\n");
        exit(EXIT_FAILURE);
    }

    int runTimeSec = atoi(argv[1]);  // Time worker should run in seconds
    int runTimeNano = rand() % 1000000000;  // Random nanoseconds for variability

    int shmid, msqid;
    int *simClock;  // Shared memory clock (seconds and nanoseconds)
    attachToClock(&simClock, &shmid);
    attachToMessageQueue(&msqid);

    // Get the starting system clock time
    int startSec = simClock[0];
    int startNano = simClock[1];
    
    // Calculate when this worker should terminate
    int termSec = startSec + runTimeSec;
    int termNano = startNano + runTimeNano;
    if (termNano >= 1000000000) {  // Carry over to seconds if nanoseconds exceed 1 billion
        termSec++;
        termNano -= 1000000000;
    }

    // Print initial information about the worker
    printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n--Just Starting\n",
           getpid(), getppid(), startSec, startNano, termSec, termNano);

    int iteration = 0;
    struct msgbuf message;

    while (1) {
        // Wait to receive message from OSS
        if (msgrcv(msqid, &message, sizeof(int), getpid(), 0) == -1) {
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }

        // Increment iteration count and check system clock
        iteration++;
        printf("WORKER PID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n--%d iterations have passed\n",
               getpid(), simClock[0], simClock[1], termSec, termNano, iteration);

        // Check if it's time to terminate
        if (hasReachedTermination(simClock, termSec, termNano)) {
            printf("WORKER PID:%d SysClockS:%d SysClockNano:%d --Terminating after %d iterations.\n",
                   getpid(), simClock[0], simClock[1], iteration);

            // Send termination message to OSS
            message.mtype = getppid();
            message.mtext = 0;  // Indicating the worker is terminating
            if (msgsnd(msqid, &message, sizeof(int), 0) == -1) {
                perror("msgsnd");
                exit(EXIT_FAILURE);
            }
            break;  // Exit the loop and terminate the worker
        }

        // Send message back to OSS indicating worker is still running
        message.mtype = getppid();
        message.mtext = 1;  // Indicating the worker is still running
        if (msgsnd(msqid, &message, sizeof(int), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }
    }

    // Detach shared memory before exiting
    shmdt(simClock);

    return 0;
}

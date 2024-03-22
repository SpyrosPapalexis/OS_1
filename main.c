#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

#define BUFFERSIZE 256

typedef struct MyMemory{
    float in[2];
    char out[BUFFERSIZE];
}Mymemory;

union semun {
    int val;                  /* value for SETVAL */
    struct semid_ds *buf;     /* buffer for IPC_STAT, IPC_SET */
    unsigned short *array;    /* array for GETALL, SETALL */
};

void SemInit(int semID) {

    union semun arg_0;
    union semun arg_1;

    arg_0.val = 0;
    arg_1.val = 1;

    //semaphore initialization
    if (semctl(semID, 0, SETVAL, arg_1) == -1) {    //used to control shared memory
        printf("sem0 failed!\n");
        exit(1);
    }

    if (semctl(semID, 1, SETVAL, arg_0) == -1) {    //used to signal parent that data has been written in memory
        printf("sem1 failed!\n");
        exit(1);
    }

    if (semctl(semID, 2, SETVAL, arg_0) == -1) {    //used to signal child that line has been written in memory
        printf("sem2 failed!\n");
        exit(1);
    }

    if (semctl(semID, 3, SETVAL, arg_0) == -1) {    //used to signal child that parent is reading for return values
        printf("sem3 failed!\n");
        exit(1);
    }

    if (semctl(semID, 4, SETVAL, arg_0) == -1) {    //used to signal parent that return data has been written in memory
        printf("sem4 failed!\n");
        exit(1);
    }
}

void SemDown(int semID, int id) {

    struct sembuf semOper;

    semOper.sem_num = id;
    semOper.sem_op = -1;
    semOper.sem_flg = 0;

    semop(semID, &semOper, 1);
}

void SemUp(int semID, int id) {

    struct sembuf semOper;

    semOper.sem_num = id;
    semOper.sem_op = 1;
    semOper.sem_flg = 0;

    semop(semID, &semOper, 1);
}

int main(int argc, char* argv[]){

    if (argc != 4){
        printf("incorrect amount of arguments!\n");
        return -1;
    }
    char *X = argv[1];
    int K = atoi(argv[2]);
    int N = atoi(argv[3]);

    int shm_id;
    int sem_id;
    pid_t pid;

    struct MyMemory *memory;

    shm_id = shmget(IPC_PRIVATE, sizeof(struct MyMemory), IPC_CREAT | 0666);
    if (shm_id == -1) {
        printf("Memory Failed\n");
        return -1;
    }

    memory = (struct MyMemory*)shmat(shm_id, 0, 0);

    if ((sem_id = semget(IPC_PRIVATE, 5, IPC_CREAT | 0666)) == -1) {
        printf("Semaphores Failed\n");
        return -1;
    }

    SemInit(sem_id);

    //find line count

    FILE *ftemp = fopen(X, "r");
    int lcount = 0;
    for (char c = getc(ftemp); c != EOF; c = getc(ftemp)){
        if (c == '\n') lcount++;
    }
    fclose(ftemp);

    for (int i = 0; i < K; i++){
        pid = fork();
        if (pid == 0){
            break;
        }
    }

    if (pid == 0){
        srand(getpid());

        float waitingtime[N];
        //random line request
        int rl;
        int mypid = getpid();
        for (int j = 0; j < N; j++){

            //child writes random line number in in shared memory
            rl = rand()%lcount;
            SemDown(sem_id,0);

            struct timespec tstart={0,0}, tend={0,0};
            clock_gettime(CLOCK_MONOTONIC, &tstart);

            memory->in[0] = mypid;
            memory->in[1] = rl;
            SemUp(sem_id,1);

            SemDown(sem_id,2);
            //child gets message from memory and prints it
            char message[BUFFERSIZE];
            strcpy(message,memory->out);
            printf("CLIENT: child with ID=%d recieved from parent: %s\n",mypid,message);

            clock_gettime(CLOCK_MONOTONIC, &tend);

            waitingtime[j] = ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec);

            SemUp(sem_id,0);
        }
        float totaltime=0;
        for (int i=0; i < N; i++){
            totaltime =+ waitingtime[i];
        }
        SemDown(sem_id,3);
        memory->in[0] = mypid;
        memory->in[1] = totaltime/N;
        SemUp(sem_id,4);


    }else if (pid > 0){
        for (int i=0;i < K*N; i++){
            SemDown(sem_id,1);
            //parent gets line and id from memory
            int childpid = memory->in[0];
            int rl = memory->in[1];
            printf("SERVER: request from client with ID=%d for line %d\n",childpid,rl);

            //parent finds random line
            char buffer[BUFFERSIZE];
            char line[BUFFERSIZE];
            int j = 0;
            FILE *f = fopen(X, "r");
            while (fgets(buffer, sizeof(buffer), f)){
                if (rl == j){
                    strcpy(line, buffer);
                    break;
                }
                j++;
            }
            fclose(f);

            //parent gets random line in memory
            strcpy(memory->out,line);
            SemUp(sem_id,2);
        }

        //signals that reading process has finished and expects return values (pid and average time) 
        SemUp(sem_id,3);
        SemDown(sem_id,0);
        for (int i=0; i<K; i++){

            SemDown(sem_id,4);
            //parent gets time and id from memory
            int childpid = memory->in[0];
            float timer = memory->in[1];
            printf("SERVER: Client with ID=%d has finished requests with time = %f seconds\n",childpid,timer);

            //parent signals next kid to finish
            SemUp(sem_id,3);
        }

    }else{
        printf("Unexpected error!\n");
        return -1;
    }
    return 0;
}
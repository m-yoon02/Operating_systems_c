// CPSC 457 (Fall 2025) — Assignment 1, Part 2
// Michelle Yoon(30189382)
// Github: https://csgit.ucalgary.ca/michelle.yoon/cpsc_457_a1

// - Limit processes: clamp N to the available work so each child gets >= 1 value
// - Divide work: balanced, contiguous, non-overlapping subranges with remainder distribution
// - Safe shared memory: no locks needed because each child writes to a disjoint block;
//   parent uses a header counts[] + a flat blocks[] region split into blocks per child.
// - blockSize = ceil(total/nprocs) ≥ possible primes in that child's subrange (primes ≤ numbers)
// - Parent detaches and IPC_RMID's the segment to avoid leaks.

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <errno.h>
#include <math.h>

// primality test for assignment constraints
static int is_prime(int num) {
    if (num < 2) return 0;
    if (num % 2 == 0) return num == 2;
    int limit = (int)floor(sqrt((double)num));
    for (int d = 3; d <= limit; d += 2) {
        if (num % d == 0) return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <LOWER> <UPPER> <NPROCS>\n", argv[0]);
        return 1;
    }

    long lower  = atol(argv[1]);
    long upper  = atol(argv[2]);
    int  nprocs = atoi(argv[3]);

    if (upper < lower || nprocs <= 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    long total = upper - lower + 1;

    // Limit the number of processes ----
    // Clamp process count so each child gets at least one value
    if (nprocs > total) nprocs = (int)total;

    //  Divide work among children ----
    // Balanced contiguous, non-overlapping subranges, with remainder distribution
    long base = total / nprocs;    // minimum chunk size
    long rem  = total % nprocs;    // first 'rem' chunks get +1

    //  Shared memory layout (safe without locks) ----
    // [ counts[nprocs] ][ blocks[nprocs * blockSize] ]
    // blockSize = ceil(total/nprocs) ensures enough capacity per child
    long blockSize = (total + nprocs - 1) / nprocs;   // ceil(total / nprocs)

    size_t counts_bytes = (size_t)nprocs * sizeof(int);
    size_t blocks_bytes = (size_t)nprocs * (size_t)blockSize * sizeof(int);
    size_t shm_bytes    = counts_bytes + blocks_bytes;

    int shmid = shmget(IPC_PRIVATE, shm_bytes, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        return 1;
    }

    void *shm = shmat(shmid, NULL, 0);
    if (shm == (void*)-1) {
        perror("shmat");
        (void)shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    int *counts = (int*)shm;                                   // header: per-child prime counts
    int *blocks = (int*)((char*)shm + counts_bytes);           // flat primes buffer split into blocks

    // Initialize counts
    for (int i = 0; i < nprocs; i++) counts[i] = 0;

    pid_t *pids = (pid_t*)malloc((size_t)nprocs * sizeof(pid_t));
    if (!pids) {
        fprintf(stderr, "malloc failed\n");
        (void)shmdt(shm);
        (void)shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    // ---- Fork exactly nprocs children in a bounded loop ----
    for (int i = 0; i < nprocs; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            // On fork failure, stop spawning, reap those started, and clean up
            perror("fork");
            for (int k = 0; k < i; k++) { int st; (void)waitpid(pids[k], &st, 0); }
            free(pids);
            (void)shmdt(shm);
            (void)shmctl(shmid, IPC_RMID, NULL);
            return 1;
        }
        if (pid == 0) {
            // ---- CHILD i: compute its subrange ----
            long len   = base + (i < rem ? 1 : 0);
            long off   = i * base + (i < rem ? i : rem);
            long start = lower + off;
            long end   = start + len - 1;

            printf("Child PID %d checking range [%ld, %ld]\n", getpid(), start, end);

            // ---- Safe shared-memory writes: disjoint block per child ----
            int  count   = 0;
            int *myblock = blocks + (size_t)i * (size_t)blockSize;

            for (long x = start; x <= end; x++) {
                if (is_prime((int)x)) {
                    // Guard is redundant given blockSize choice, but keep it for safety
                    if (count < blockSize) {
                        myblock[count++] = (int)x;
                    }
                }
            }
            counts[i] = count;   // publish number of primes written by child i

            _exit(0);
        } else {
            // ---- PARENT ----
            pids[i] = pid;
        }
    }

    // ---- PARENT: wait for all children (no zombies) ----
    for (int i = 0; i < nprocs; i++) {
        int st;
        if (waitpid(pids[i], &st, 0) < 0) perror("waitpid");
    }
    free(pids);

    // ---- Read  results in ascending order ----
    // each child's subrange is ascending and child indices increase with start offset,
    // concatenating blocks in i = 0..nprocs-1 yields sorted primes.
    printf("Parent: All children finished. Primes found:\n");
    for (int i = 0; i < nprocs; i++) {
        int cnt = counts[i];
        int *myblock = blocks + (size_t)i * (size_t)blockSize;
        for (int j = 0; j < cnt; j++) {
            printf("%d ", myblock[j]);
        }
    }
    printf("\n");

    // Clean up shared memory 
    if (shmdt(shm) < 0) perror("shmdt");
    if (shmctl(shmid, IPC_RMID, NULL) < 0) perror("shmctl IPC_RMID");

    return 0;
}



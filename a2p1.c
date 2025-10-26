// CPSC 457 (Fall 2025) — Assignment 2, Part I 
// Michelle Yoon (30189382)
// Github:https://csgit.ucalgary.ca/michelle.yoon/cpsc_457_a1

//gcc -O2 a2p1.c -o a2p1
//./a2p1 < inputfile1.csv


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>

#define MAX_TASKS 2000  // input says 1000 records, leave headroom
typedef long long i64;

typedef struct {
    int rec_id;                // 0..n-1 unique per line
    int pid;                   // process id (1..50)
    int arrival;               // arrival time
    int first_resp_hint;       // read but not used
    int burst;                 // CPU burst length
} Task;

typedef struct {
    i64 start_time;            // first start
    i64 finish_time;           // completion time
    i64 waiting;               // accumulated waiting time (FCFS -> start - arrival)
    i64 turnaround;            // finish - arrival
    i64 response;              // start - arrival
} Stat;

static int cmp_arrival_pid(const void *a, const void *b) {
    const Task *x = (const Task*)a, *y = (const Task*)b;
    if (x->arrival != y->arrival) return (x->arrival < y->arrival) ? -1 : 1;
    if (x->pid != y->pid) return (x->pid < y->pid) ? -1 : 1; // tie-breaker: lower PID first
    // stable by rec_id to preserve file order otherwise
    return (x->rec_id < y->rec_id) ? -1 : (x->rec_id > y->rec_id);
}

static bool parse_line(const char *s, int *pid, int *arr, int *hint, int *burst) {
    // Accept header or comment lines by skipping anything that doesn't start with digit or space
    // Expected row format: process,arrival,first_response_hint,burst
    // Robust CSV parse for simple integers
    const char *p = s; while (isspace((unsigned char)*p)) p++;
    if (!*p) return false; // empty line
    if (!isdigit((unsigned char)*p)) return false; // header or comment -> skip
    // Try to parse 4 ints separated by commas or spaces
    int a,b,c,d; char ch;
    if (sscanf(s, " %d %c %d %c %d %c %d", &a, &ch, &b, &ch, &c, &ch, &d) == 7) {
        *pid=a; *arr=b; *hint=c; *burst=d; return true;
    }
    if (sscanf(s, " %d , %d , %d , %d", &a, &b, &c, &d) == 4) { *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    if (sscanf(s, " %d %d %d %d", &a, &b, &c, &d) == 4) { *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    return false;
}

int main(void) {
    // Read all tasks from stdin
    static Task base[MAX_TASKS];
    int n = 0;
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        int pid, arr, hint, burst;
        if (!parse_line(line, &pid, &arr, &hint, &burst)) continue; // skip headers/comments
        if (n >= MAX_TASKS) { fprintf(stderr, "Too many tasks, increase MAX_TASKS\n"); return 1; }
        base[n].rec_id = n; base[n].pid=pid; base[n].arrival=arr; base[n].first_resp_hint=hint; base[n].burst=burst; n++;
    }
    if (n == 0) { fprintf(stderr, "No tasks parsed. Ensure the CSV has numeric rows after the header.\n"); return 1; }

    // Sort once by arrival then PID to lock FCFS order
    qsort(base, n, sizeof(Task), cmp_arrival_pid);

    // Open CSV outputs
    FILE *agg = fopen("fcfs_results.csv", "w");
    FILE *det = fopen("fcfs_results_details.csv", "w");
    if (!agg || !det) { fprintf(stderr, "Failed to open output CSV files.\n"); return 1; }

    fprintf(agg, "latency,throughput,avg_wait,avg_turnaround,avg_response,makespan,dispatches\n");
    fprintf(det, "latency,job_id,pid,arrival,burst,first_start,finish,waiting,turnaround,response\n");

    const int LMIN=1, LMAX=200;
    for (int latency = LMIN; latency <= LMAX; ++latency) {
        i64 time = 0;
        i64 first_arrival = base[0].arrival;
        i64 dispatches = 0;
        i64 sum_wait=0, sum_turn=0, sum_resp=0;

        for (int i = 0; i < n; ++i) {
            const Task *t = &base[i];
            // If CPU is idle before this arrival, jump to arrival
            if (time < (i64)t->arrival) time = t->arrival;
            // Charge scheduler/dispatcher latency before running
            time += latency; dispatches++;
            i64 start = time;
            i64 finish = start + t->burst;
            i64 wait = start - t->arrival;
            i64 turn = finish - t->arrival;
            i64 resp = start - t->arrival; // FCFS: first response == first start

            sum_wait += wait; sum_turn += turn; sum_resp += resp;
            time = finish; // non-preemptive

            fprintf(det, "%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld\n",
                    latency, t->rec_id, t->pid, t->arrival, t->burst,
                    (long long)start, (long long)finish,
                    (long long)wait, (long long)turn, (long long)resp);
        }
        i64 makespan = time - first_arrival;
        double throughput = (makespan > 0) ? ((double)n / (double)makespan) : 0.0;
        double avg_wait = (double)sum_wait / (double)n;
        double avg_turn = (double)sum_turn / (double)n;
        double avg_resp = (double)sum_resp / (double)n;

        fprintf(agg, "%d,%.8f,%.8f,%.8f,%.8f,%lld,%lld\n",
                latency, throughput, avg_wait, avg_turn, avg_resp,
                (long long)makespan, (long long)dispatches);
    }

    fclose(agg); fclose(det);

    // Minimal terminal summary
    printf("FCFS done. Wrote fcfs_results.csv and fcfs_results_details.csv (latency 1..200).\n");
    return 0;
}
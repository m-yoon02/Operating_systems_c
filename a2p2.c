// CPSC 457 (Fall 2025) — Assignment 2, Part 2
// Michelle Yoon (30189382)
// Github:https://csgit.ucalgary.ca/michelle.yoon/cpsc_457_a1

//gcc -O2 a2p2.c -o a2p2
// ./a2p2 < inputfile1.csv


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>

#define MAX_TASKS 2000
#define QCAP 600000  // ring buffer capacity (enough for q=1 worst-case)


typedef long long i64;

typedef struct {
    int rec_id, pid, arrival, first_resp_hint, burst;
} Task;

typedef struct {
    int remaining;
    i64 first_start;     // -1 if not started
    i64 finish;
    i64 last_enq;        // when it most recently became ready
    i64 wait_accum;      // sum of (dispatch_time - last_enq)
} Run;

static int cmp_arrival_pid(const void *a, const void *b) {
    const Task *x = (const Task*)a, *y = (const Task*)b;
    if (x->arrival != y->arrival) return (x->arrival < y->arrival) ? -1 : 1;
    if (x->pid != y->pid) return (x->pid < y->pid) ? -1 : 1;
    return (x->rec_id < y->rec_id) ? -1 : (x->rec_id > y->rec_id);
}

static bool parse_line(const char *s, int *pid, int *arr, int *hint, int *burst) {
    const char *p = s; while (isspace((unsigned char)*p)) p++;
    if (!*p) return false;
    if (!isdigit((unsigned char)*p)) return false;
    int a,b,c,d; char ch;
    if (sscanf(s, " %d %c %d %c %d %c %d", &a, &ch, &b, &ch, &c, &ch, &d) == 7) { *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    if (sscanf(s, " %d , %d , %d , %d", &a, &b, &c, &d) == 4) { *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    if (sscanf(s, " %d %d %d %d", &a, &b, &c, &d) == 4) { *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    return false;
}

// simple ring queue of task indices
static int q[QCAP]; static int qh, qt; // [qh, qt)
static inline void q_reset(void){ qh=qt=0; }
static inline int q_empty(void){ return qh==qt; }
static inline int q_size(void){ return qt-qh; }
static inline void q_push(int v){ if (qt-qh >= QCAP) { fprintf(stderr, "Queue overflow\n"); exit(1);} q[qt++]=v; }
static inline int q_pop(void){ if (q_empty()) return -1; return q[qh++]; }

int main(void){
    static Task base[MAX_TASKS]; int n=0; char line[4096];
    while (fgets(line, sizeof(line), stdin)){
        int pid, arr, hint, burst; if (!parse_line(line,&pid,&arr,&hint,&burst)) continue;
        if (n>=MAX_TASKS){ fprintf(stderr,"Too many tasks\n"); return 1; }
        base[n].rec_id=n; base[n].pid=pid; base[n].arrival=arr; base[n].first_resp_hint=hint; base[n].burst=burst; n++;
    }
    if (n==0){ fprintf(stderr, "No tasks parsed.\n"); return 1; }
    qsort(base, n, sizeof(Task), cmp_arrival_pid);

    FILE *agg=fopen("rr_results.csv","w");
    FILE *det=fopen("rr_results_details.csv","w");
    if (!agg||!det){ fprintf(stderr,"Failed to open output files\n"); return 1; }
    fprintf(agg, "quantum,throughput,avg_wait,avg_turnaround,avg_response,makespan,dispatches,preemptions\n");
    fprintf(det, "quantum,job_id,pid,arrival,burst,first_start,finish,waiting,turnaround,response\n");

    const int LATENCY=20;
    const int QMIN=1, QMAX=200;

    for (int Q=QMIN; Q<=QMAX; ++Q){
        // clone runtime state
        static Run run[MAX_TASKS];
        for (int i=0;i<n;++i){ run[i].remaining=base[i].burst; run[i].first_start=-1; run[i].finish=-1; run[i].last_enq=base[i].arrival; run[i].wait_accum=0; }

        q_reset();
        i64 time=0; int next=0; // next index in base[] to arrive
        i64 first_arrival = base[0].arrival;
        i64 dispatches=0, preempts=0; i64 sum_wait=0, sum_turn=0, sum_resp=0; int finished=0;

        // seed time to first arrival and enqueue all with arrival==first arrival
        if (time < base[0].arrival) time = base[0].arrival;
        while (next<n && base[next].arrival<=time){ q_push(next); next++; }

        while (finished < n){
            if (q_empty()){
                // jump to next arrival
                if (next<n){ time = base[next].arrival; while (next<n && base[next].arrival<=time){ q_push(next); next++; } }
                else break; // no more jobs (should not happen)
            }
            if (q_empty()) continue;

            time += LATENCY; dispatches++;
            int idx = q_pop();
            Task *t = &base[idx]; Run *r = &run[idx];
            if (r->first_start<0){ r->first_start = time; }
            // waiting += time - last_enq
            if (time > r->last_enq) r->wait_accum += (time - r->last_enq);

            // run one slice (no preemption by arrivals within slice for RR)
            int slice = (r->remaining < Q) ? r->remaining : Q;
            i64 end = time + slice;
            // enqueue any arrivals up to and including 'end' BEFORE re-queueing current (classic behavior)
            while (next<n && base[next].arrival <= end){ q_push(next); next++; }

            time = end; r->remaining -= slice;
            if (r->remaining == 0){
                r->finish = time; finished++;
                i64 turn = r->finish - t->arrival; i64 resp = r->first_start - t->arrival; i64 wait = r->wait_accum;
                sum_turn += turn; sum_resp += resp; sum_wait += wait;
                fprintf(det, "%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld\n",
                        Q, t->rec_id, t->pid, t->arrival, t->burst,
                        (long long)r->first_start, (long long)r->finish,
                        (long long)wait, (long long)turn, (long long)resp);
            } else {
                // time slice expired -> preempt and requeue to tail
                preempts++;
                r->last_enq = time;
                q_push(idx);
            }
        }

        i64 makespan = time - first_arrival;
        double throughput = (makespan>0)? ((double)n/(double)makespan):0.0;
        double avg_wait = (double)sum_wait/(double)n;
        double avg_turn = (double)sum_turn/(double)n;
        double avg_resp = (double)sum_resp/(double)n;
        fprintf(agg, "%d,%.8f,%.8f,%.8f,%.8f,%lld,%lld,%lld\n",
                Q, throughput, avg_wait, avg_turn, avg_resp,
                (long long)makespan, (long long)dispatches, (long long)preempts);
    }

    fclose(agg); fclose(det);
    printf("RR done. Wrote rr_results.csv and rr_results_details.csv (quantum 1..200, latency=20).\n");
    return 0;
}
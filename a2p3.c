// CPSC 457 (Fall 2025) — Assignment 2, Part 3
// Michelle Yoon (30189382)
// Github:https://csgit.ucalgary.ca/michelle.yoon/cpsc_457_a1

// gcc -O2 a2p3.c -o a2p3
// ./a2p3 < inputfile1.csv

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_TASKS 2000
#define QCAP 600000

typedef long long i64;

typedef struct { int rec_id, pid, arrival, first_resp_hint, burst; } Task;

typedef struct {
    int remaining;
    int level;            // 1,2,3
    int slice_left;       // remaining time in current quantum for levels 1/2
    i64 first_start;      // -1 if not started
    i64 finish;
    i64 last_enq;
    i64 wait_accum;
} Run;

static int cmp_arrival_pid(const void *a, const void *b){
    const Task *x=(const Task*)a, *y=(const Task*)b;
    if (x->arrival!=y->arrival) return (x->arrival<y->arrival)?-1:1;
    if (x->pid!=y->pid) return (x->pid<y->pid)?-1:1;
    return (x->rec_id<y->rec_id)?-1:(x->rec_id>y->rec_id);
}

static bool parse_line(const char *s, int *pid, int *arr, int *hint, int *burst){
    const char *p=s; while(*p && isspace((unsigned char)*p)) p++;
    if (!*p) return false; if (!isdigit((unsigned char)*p)) return false;
    int a,b,c,d; char ch;
    if (sscanf(s, " %d %c %d %c %d %c %d", &a,&ch,&b,&ch,&c,&ch,&d)==7){ *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    if (sscanf(s, " %d , %d , %d , %d", &a,&b,&c,&d)==4){ *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    if (sscanf(s, " %d %d %d %d", &a,&b,&c,&d)==4){ *pid=a; *arr=b; *hint=c; *burst=d; return true; }
    return false;
}

// Simple deque with push_front/push_back and pop_front
static int q1[QCAP], q2[QCAP], q3[QCAP];
static int h1,t1,h2,t2,h3,t3; // [h,t)
static inline void q_reset(){ h1=t1=h2=t2=h3=t3=0; }
static inline int q1_empty(){ return h1==t1; }
static inline int q2_empty(){ return h2==t2; }
static inline int q3_empty(){ return h3==t3; }
static inline void push_back(int *q, int *t, int v){ q[(*t)++] = v; }
static inline void push_front(int *q, int *h, int v){ if (*h==0){ fprintf(stderr,"push_front overflow, increase QCAP or refactor\n"); exit(1);} q[--(*h)] = v; }
static inline int pop_front(int *q, int *h, int *t){ if (*h==*t) return -1; return q[(*h)++]; }

int main(void){
    static Task base[MAX_TASKS]; int n=0; char line[4096];
    while (fgets(line,sizeof(line),stdin)){
        int pid,arr,hint,burst; if (!parse_line(line,&pid,&arr,&hint,&burst)) continue;
        if (n>=MAX_TASKS){ fprintf(stderr,"Too many tasks\n"); return 1; }
        base[n].rec_id=n; base[n].pid=pid; base[n].arrival=arr; base[n].first_resp_hint=hint; base[n].burst=burst; n++;
    }
    if (n==0){ fprintf(stderr,"No tasks parsed\n"); return 1; }
    qsort(base,n,sizeof(Task),cmp_arrival_pid);

    FILE *agg=fopen("mlfq_results.csv","w");
    FILE *det=fopen("mlfq_results_details.csv","w");
    if(!agg||!det){ fprintf(stderr,"Failed to open output files\n"); return 1; }
    fprintf(agg,"throughput,avg_wait,avg_turnaround,avg_response,makespan,dispatches,preemptions\n");
    fprintf(det,"job_id,pid,arrival,burst,first_start,finish,waiting,turnaround,response,final_level\n");

    const int LAT=20, QL1=40, QL2=80;

    static Run run[MAX_TASKS];
    for (int i=0;i<n;++i){ run[i].remaining=base[i].burst; run[i].level=1; run[i].slice_left=QL1; run[i].first_start=-1; run[i].finish=-1; run[i].last_enq=base[i].arrival; run[i].wait_accum=0; }

    q_reset(); int next=0; i64 time=0; i64 first_arrival=base[0].arrival;
    if (time<first_arrival) time=first_arrival;
    // enqueue all arrivals at time 0..first arrival
    while (next<n && base[next].arrival<=time){ push_back(q1,&t1,next); next++; }

    i64 dispatches=0, preempts=0; int finished=0; i64 sum_wait=0,sum_turn=0,sum_resp=0;

    while (finished<n){
        // If nothing ready, jump to next arrival
        if (q1_empty() && q2_empty() && q3_empty()){
            if (next<n){ time = base[next].arrival; while (next<n && base[next].arrival<=time){ push_back(q1,&t1,next); next++; } }
            else break;
        }
        if (q1_empty() && q2_empty() && q3_empty()) continue;

        time += LAT; dispatches++;
        int idx=-1, lvl=0;
        if (!q1_empty()){ idx=pop_front(q1,&h1,&t1); lvl=1; }
        else if (!q2_empty()){ idx=pop_front(q2,&h2,&t2); lvl=2; }
        else { idx=pop_front(q3,&h3,&t3); lvl=3; }

        Task *t=&base[idx]; Run *r=&run[idx];
        if (r->first_start<0){ r->first_start = time; }
        if (time > r->last_enq) r->wait_accum += (time - r->last_enq);
        if (r->level != lvl){ r->level = lvl; r->slice_left = (lvl==1?QL1:(lvl==2?QL2:0)); }
        if (lvl==1 && r->slice_left<=0) r->slice_left=QL1;
        if (lvl==2 && r->slice_left<=0) r->slice_left=QL2;

        // Plan run length, considering preemption by future arrivals (only arrivals to Q1 matter)
        int budget = (lvl==3) ? r->remaining : (r->slice_left < r->remaining ? r->slice_left : r->remaining);
        i64 run_end = time + budget;
        // if a new arrival occurs earlier and lvl>1 or lvl==3, preempt at that arrival (higher priority Q1)
        if (lvl>=2 && next<n && base[next].arrival < run_end){
            run_end = base[next].arrival;
        }
        int used = (int)(run_end - time);
        if (used < 0) used = 0;
        r->remaining -= used;
        if (lvl!=3) r->slice_left -= used;
        time = run_end;

        // Enqueue all arrivals up to 'time'
        while (next<n && base[next].arrival<=time){ push_back(q1,&t1,next); next++; }

        if (r->remaining == 0){
            r->finish = time; finished++;
            i64 turn=r->finish - t->arrival, resp=r->first_start - t->arrival, wait=r->wait_accum;
            sum_turn+=turn; sum_resp+=resp; sum_wait+=wait;
            fprintf(det,"%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%d\n",
                    t->rec_id,t->pid,t->arrival,t->burst,
                    (long long)r->first_start,(long long)r->finish,
                    (long long)wait,(long long)turn,(long long)resp,r->level);
        } else {
            // Not finished. Decide requeue behavior.
            if ((lvl==1 || lvl==2) && r->slice_left==0){
                // consumed full quantum -> demote
                if (lvl==1){ r->level=2; r->slice_left=QL2; r->last_enq=time; push_back(q2,&t2,idx); }
                else { r->level=3; r->slice_left=0; r->last_enq=time; push_back(q3,&t3,idx); }
                preempts++;
            } else if (lvl>=2 && next>0 && base[next-1].arrival==time){
                // Preempted by higher-level arrival -> keep same level, resume later with remaining slice
                r->last_enq=time; // resume later
                if (lvl==2) push_front(q2,&h2,idx); else push_front(q3,&h3,idx); // keep position (front) for fairness
                preempts++;
            } else {
                // RR slice ended early only because budget==remaining for lvl==1/2 (rare); or lvl==3 ran until next arrival but no preempt (shouldn't happen)
                r->last_enq=time;
                if (lvl==1) push_back(q1,&t1,idx); else if (lvl==2) push_back(q2,&t2,idx); else push_back(q3,&t3,idx);
                preempts++;
            }
        }
    }

    i64 makespan = (n>0)? (run[0].finish>=0? run[0].finish:0) : 0;
    // Compute true makespan as max finish - min arrival
    i64 max_finish=0; for (int i=0;i<n;++i) if (run[i].finish>max_finish) max_finish=run[i].finish;
    i64 min_arr=base[0].arrival;
    makespan = max_finish - min_arr;
    double throughput = (makespan>0)? ((double)n/(double)makespan):0.0;
    double avg_wait = (double)sum_wait/(double)n;
    double avg_turn = (double)sum_turn/(double)n;
    double avg_resp = (double)sum_resp/(double)n;

    fprintf(agg,"%.8f,%.8f,%.8f,%.8f,%lld,%lld,%lld\n",
            throughput,avg_wait,avg_turn,avg_resp,(long long)makespan,(long long)dispatches,(long long)preempts);
    fclose(agg); fclose(det);

    printf("MLFQ done. Wrote mlfq_results.csv and mlfq_results_details.csv (Q1=40, Q2=80, Q3=FCFS, latency=20).\n");
    return 0;
}
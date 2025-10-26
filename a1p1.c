// CPSC 457 (Fall 2025) — Assignment 1, Part I 
// Michelle Yoon (30189382)
// Github:https://csgit.ucalgary.ca/michelle.yoon/cpsc_457_a1

// Input handling & error checking:
// - Reads exactly 100*1000 integers from stdin (FILE *fp = stdin).
// - Each value must be 0 or 1; otherwise: error -> exit(1).
// - After reading the matrix, verifies that there is no trailing non-whitespace data.
// - Counts number of '1's (treasures):
//      * 0 treasures: warn and proceed (parent will print "No treasure found.")
//      * >1 treasures: error and exit(1) (invalid input per spec assumption).


#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>   // for isspace

#define NROWS 100
#define NCOLS 1000

int main(void) {
    static int matrix[NROWS][NCOLS];   // static to avoid large stack allocation
    FILE *fp = stdin;                 

    // ---- Strict parse: read exactly NROWS*NCOLS integers, each 0 or 1 ----
    long ones_count = 0;
    for (int r = 0; r < NROWS; r++) {
        for (int c = 0; c < NCOLS; c++) {
            int v;
            if (fscanf(fp, " %d", &v) != 1) {
                fprintf(stderr, "Input error: expected integer at row %d col %d\n", r, c);
                return 1;
            }
            if (v != 0 && v != 1) {
                fprintf(stderr, "Input error: value at row %d col %d must be 0 or 1 (got %d)\n", r, c, v);
                return 1;
            }
            matrix[r][c] = v;
            if (v == 1) ones_count++;
        }
    }

    // ---- Check for trailing non-whitespace junk after the 100,000th integer ----
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (!isspace(ch)) {
            fprintf(stderr, "Input error: extra non-whitespace data after %d integers\n", NROWS * NCOLS);
            return 1;
        }
    }

    // ---- Edge cases based on treasure count ----
    if (ones_count == 0) {
        fprintf(stderr, "Warning: no treasure in matrix (no '1' found). Program will continue.\n");
        // program still run the search; parent will report "No treasure found."
    } else if (ones_count > 1) {
        fprintf(stderr, "Error: multiple treasures found (%ld). Expected exactly one.\n", ones_count);
        return 1;  // treat as invalid input per spec assumption
    }

    // ---- fork 100 children, one per row ----
    pid_t pids[NROWS];
    for (int r = 0; r < NROWS; r++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            // Cleanly reap already-started children before exiting
            for (int i = 0; i < r; i++) {
                int st; (void)waitpid(pids[i], &st, 0);
            }
            return 1;
        }
        if (pid == 0) {
            // ---- CHILD: search row r ----
            printf("Child %d (PID %d): Searching row %d\n", r, getpid(), r);
            int found = 0;
            for (int c = 0; c < NCOLS; c++) {
                if (matrix[r][c] == 1) { found = 1; break; }
            }
            _exit(found ? 1 : 0);  // 1 => found, 0 => not found
        } else {
            pids[r] = pid;         // Parent keeps PID→row mapping via index
        }
    }

    // ---- PARENT: wait for ALL children; if a child reports found, compute column ----
    int treasure_row = -1, treasure_col = -1;
    int reaped = 0;

    while (reaped < NROWS) {
        int status = 0;
        pid_t pid = wait(&status);
        if (pid < 0) {
            if (errno == EINTR) continue;
            perror("wait");
            break;
        }
        reaped++;

        // Map PID back to row
        int row = -1;
        for (int r = 0; r < NROWS; r++) {
            if (pids[r] == pid) { row = r; break; }
        }
        if (row == -1) continue;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
            // Child says this row contains treasure; parent computes column
            treasure_row = row;
            for (int c = 0; c < NCOLS; c++) {
                if (matrix[row][c] == 1) { treasure_col = c; break; }
            }
            // Keep reaping to avoid zombies; no early-return
        }
    }

    if (treasure_row >= 0 && treasure_col >= 0) {
        pid_t winner = pids[treasure_row];
        printf("Parent: The treasure was found by child with PID %d at row %d and column %d\n",
               (int)winner, treasure_row, treasure_col);
    } else {
        printf("Parent: No treasure found.\n");
    }

    return 0;
}

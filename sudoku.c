/* This is a parallelized sudoku solver written by 
Mithil Pechimuthu, Roll No.: 21110129, CSE, IIT Gandhinagar.
Ideated and developed on 18/8/2023

The back tracking sections of this program were inspored from 
https://codereview.stackexchange.com/questions/37430/sudoku-solver-in-c

RUNNING THE PROGRAM: 
STEP 1: gcc sodoku.c -o sudoku
STEP 2: ./sudoku <path to input file> <path to output filr>

When we look at the backtracking algorithm and the recursion tree that is associated with it, we can see that we can assign atmost 9 process to handle the subtree
associated with every node. The node in the tree are the cells with '0' (empty cell). I initially developed a program that tried to parallelize the solver upto a hieght
'h' in the recursion tree. For the nodes below 'h', the forked processes, handle the casses as though they are performing the traditional backtracking. However, after 
experimenting with different values of 'h', I observed that h = 1, was the best option. 
For h = 0, it is the traditional single process backtracking. 
For h > 1, the program didn't run properly on all systems, often leading to fork failures. 

Moreover the speedups for h > 1 weren't a lot as compared to the speedup from h = 1 for 9x9 sudoku puzzle. 
*/

#include <stdio.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>

/* This struct is what will be shared among the parent process and it's 9 children. */
struct Shared {
    short int scratchpad[9][9][9]; /*These are 9 grids for the 9 children processes to fill. One or many of them will have the right solution at the end, if one exists.*/
    int solved[9]; /*This ith entry in this array stores if the ith child process was able to solve the sudoku with the configuration assigned to it.*/
    sem_t done; /*This is shared semaphore between parent and children. The parent waits for atleast one of the children to post this semaphore.*/
};

int Check(short int grid[][9], short int row, short int col, short int num){ 
    /*This functions validates if we can put the following element into the cell (row, col) 
    The overhead of checking if done using semaphores is way higher when compared to sequential execution.
    Moreover, using separate processes to do this will not lead to any speed up.*/

    int block_row = (row/3) * 3;
    int block_col = (col/3) * 3;
    int i, j;

    for(i=0; i<9; ++i){
        if (grid[row][i] == num) return 0;
        if (grid[i][col] == num) return 0;
        if (grid[block_row + (i%3)][block_col + (i/3)] == num) return 0;
    }
    return 1;
}

int Backtrack(short int grid[][9], short int row, short int col){
    /* The traditional backtracking function to solve a 9x9 sudoku.*/
    if(row<9 && col<9){
        if(grid[row][col] != 0){
            if((col+1)<9) return Backtrack(grid, row, col+1);
            else if((row+1)<9) return Backtrack(grid, row+1, 0);
            else return 1;
        }
        else{
            for(int i=0; i<9; ++i){
                if(Check(grid, row, col, i+1)){
                    grid[row][col] = i+1;
                    if((col+1)<9){
                        if(Backtrack(grid, row, col +1)){
                            return 1;
                        }
                        else{
                            grid[row][col] = 0;
                        }
                    }
                    else if((row+1)<9){
                        if(Backtrack(grid, row+1, 0)){
                            return 1;
                        }
                        else{
                            grid[row][col] = 0;
                        }
                    }
                    else return 1;
                }
            }
        }
        return 0;
    }
    else return 1;
}

int main(int argc, char* argv[]) {
    /* argv[1] = path to input file.
       argv[2] = path to putput file.*/

    if (argc < 3) { 
        printf("Please provide the input file and output file.\n");
        return 1;
    }
    FILE* infile = fopen(argv[1], "r");

    if (infile == NULL) {
        printf("Failed to open the file for reading.\n");
        return 1;
    }
    /* Initializing the grid */
    short int grid[9][9];
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 9; j++) {
            char c;
            if (fscanf(infile, " %c", &c) != 1) {
                printf("Error reading data from file.\n");
                fclose(infile);
                return 1;
            }
            grid[i][j] = c - '0';
        }
    }
    fclose(infile);

    /* Initializing the shared memory construct to handle IPC. */
    int shmid;
    struct Shared* data;
    shmid = shmget(IPC_PRIVATE, sizeof(struct Shared), IPC_CREAT | 0666);
        if (shmid == -1) {
        perror("shmget");
        return 1;
    }

    /* Attach the shared memory segment. */
    data = (struct Shared *)shmat(shmid, NULL, 0);
    if (data == (struct Shared *)-1) {
        perror("shmat");
        return 1;
    }
    for (int i = 0; i < 9; i++){
        for (int j = 0; j < 9; j++){
            for (int k = 0; k < 9; k++){
                data->scratchpad[i][j][k] = grid[j][k];
            }
        }
    }
    int r,c; // r,c will store the value of row and column respectively of the first encountered empty cell in the sudoku.
    r = -1;
    c = -1;
    for (int i = 0; i < 9; i++){
        for (int j = 0; j < 9; j ++){
            if (grid[i][j] == 0){
                r = i;
                c = j;
                break;
            }
        }
        if (r >= 0){
            break;
        }
    }
    if (r < 0){
        printf("The Sudoku is already solved.\n");
    }
 
    sem_init(&data->done, 1, 0);

    pid_t procs[9]; //array that stores the pids of the children.
    for (int i = 0; i < 9; i++){
        procs[i] = fork();
        if(procs[i] == 0){
            if(Check(data->scratchpad[i], r, c, i+1))
            {
                data->scratchpad[i][r][c] = i+1; //Basically the i'th child process tries to fill the sudoku after filing the first empty cell with the value i+1.
                if((c+1)<9)
                {
                    data->solved[i] = Backtrack(data->scratchpad[i],r,c+1);
                }
                else if((r+1)<9)
                {
                    data->solved[i] = Backtrack(data->scratchpad[i],r+1,0);
                }
                else data->solved[i] = 1;
            }
            if (data->solved[i] == 1){
                sem_post(&data->done); //Assuming that the sudoku has atleast one solution, atleast one process will exit after letting go of done semaphore.
                exit(0);
            }
            else{
                exit(0);
            }
        }
        else if (procs[i] < 0){
            printf("Fork failed!\n");
            exit(1);
        }
    }
    sem_wait(&data->done);
    for (int i = 0; i < 9; i++){
        if (data->solved[i] == 1){
            FILE* outfile = fopen(argv[2],"a");
            for (int j = 0; j < 9; j++){
                for (int k = 0; k < 9; k++){
                    fprintf(outfile,"%d",data->scratchpad[i][j][k]);
                }
                fprintf(outfile,"\n");
            }
            fclose(outfile);
        }
    }
    //Detach the shared memory segment in the parent process when done with it.
    shmdt(data);
    // Remove the shared memory segment in the parent process when done with it.
    shmctl(shmid, IPC_RMID, NULL);
    //Destroy the semaphore.
    sem_destroy(&data->done);
    return 0;
}
/*
 * Swarthmore College, CS 31
 * Copyright (c) 2022 Swarthmore College Computer Science Department,
 * Swarthmore PA
 */

//Takes in command line arguments. Opens and reads a file. Simulates
//the game of life based on whether a cell is live or not. 
//Plays gol in different ways based on user input. 

#include <pthreadGridVisi.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "colors.h"

/****************** Definitions **********************/
/* Three possible modes in which the GOL simulation can run */
#define OUTPUT_NONE   (0)   // with no animation
#define OUTPUT_ASCII  (1)   // with ascii animation
#define OUTPUT_VISI   (2)   // with ParaVis animation

/* Used to slow down animation run modes: usleep(SLEEP_USECS);
 * Change this value to make the animation run faster or slower
 */
//#define SLEEP_USECS  (1000000)
#define SLEEP_USECS    (100000)

/* A global variable to keep track of the number of live cells in the
 * world (this is the ONLY global variable you may use in your program)
 */
static int total_live = 0;

/* Mutex */
static pthread_mutex_t my_mutex;

/* Barrier */
static pthread_barrier_t my_barrier;

/* This struct represents all the data you need to keep track of your GOL
 * simulation.  Rather than passing individual arguments into each function,
 * we'll pass in everything in just one of these structs.
 * this is passed to play_gol, the main gol playing loop
 *
 * NOTE: You will need to use the provided fields here, but you'll also
 *       need to add additional fields. (note the nice field comments!)
 * NOTE: DO NOT CHANGE THE NAME OF THIS STRUCT!!!!
 */
struct gol_data {

    // NOTE: DO NOT CHANGE the names of these 4 fields (but USE them)
    int rows;  // the row dimension
    int cols;  // the column dimension
    int iters; // number of iterations to run the gol simulation
    int output_mode; // set to:  OUTPUT_NONE, OUTPUT_ASCII, or OUTPUT_VISI
    int num_threads; // number of p_threads to use
    int row_start; // the start row of the thread
    int row_end; // the end row of the thread
    int col_start; // the start column of the thread
    int col_end; // the end column of the thread
    int id; // the thread id 
    
    int partition; // if 0 = partition board by rows: if 1 = partition board by columns
    int print_part; // if 1 = print specific thread information

    int *array;
    int *new_array;
    
    /* fields used by ParaVis library (when run in OUTPUT_VISI mode). */
    // NOTE: DO NOT CHANGE their definitions BUT USE these fields
    visi_handle handle;
    color3 *image_buff;
};


/****************** Function Prototypes **********************/
// TODO: A few starting point function prototypes.
//       You will need to implement these functions.

/* the main gol game playing loop (prototype must match this) */
void *play_gol(void *args);

/* init gol data from the input file and run mode cmdline args */
int init_game_data_from_args(struct gol_data *data, char **argv);

// A mostly implemented function, but a bit more for you to add.
/* print board to the terminal (for OUTPUT_ASCII mode) */
void print_board(struct gol_data *data, int round);

// Checks if the (k,l) neighbors are live or not.
// returns the number of live neighbors
int check_neighbors(struct gol_data *data, int k, int l);

// Sets (k,l) to live or dead based on number of live neighbors (live_neighbors)
int set_alive(struct gol_data *data, int live_neighbors, int k, int l);

// updates the color for visi animation
// pasted from weeklylab code - modified to work here
void update_colors(struct gol_data *data);

// Partitions the board based on user input. 
// Will divide the board evenly based on the number of threads
void partition(struct gol_data *data);

/************ Definitions for using ParVisi library ***********/
/* initialization for the ParaVisi library (DO NOT MODIFY) */
int setup_animation(struct gol_data* data);
/* register animation with ParaVisi library (DO NOT MODIFY) */
int connect_animation(void (*applfunc)(struct gol_data *data),
        struct gol_data* data);
/* name for visi (you may change the string value if you'd like) */
static char visi_name[] = "GOL!";


int main(int argc, char **argv) {

    int ret;
    struct gol_data data;
    struct gol_data *tid_data;
    double secs;
    struct timeval start_time, stop_time;
    pthread_t *tids;

    /* check number of command line /~kwebb/cs31/f22/Labs/lab09/arguments */
    if (argc < 6) {
        printf("Error: Invalid number of command line arguments.\n");
        printf("usage: %s <infile.txt> <output_mode>[0|1|2] ", argv[0]);
        printf("<num_threads> <partition>[0|1] <print_partition>[0|1]\n");
        exit(1);
    }

    /* Initialize game state (all fields in data) from information
     * read from input file */
    ret = init_game_data_from_args(&data, argv);
    if (ret != 0) {
        printf("Initialization error: file %s, mode %s\n", argv[1], argv[2]);
        exit(1);
    }

    tid_data = malloc(sizeof(struct gol_data) * data.num_threads);
    if (!tid_data) {
        perror("malloc: tid_data array");
        exit(1);
    }

    tids = malloc(sizeof(pthread_t) * data.num_threads);
    if (!tids) {
        perror("malloc: pthread_t array");
        exit(1);
    }

    /* Initialize the mutex. */
    if (pthread_mutex_init(&my_mutex, NULL)) {
        printf("pthread_mutex_init error\n");
        exit(1);
    }

    /* Initialize the barrier with the number of threads that will
     * synchronize on it: */
    if (pthread_barrier_init(&my_barrier, NULL, data.num_threads)) {
        printf("pthread_barrier_init error\n");
        exit(1);
    }

    /* initialize ParaVisi animation (if applicable) */
    if (data.output_mode == OUTPUT_VISI) {
        
        data.handle = init_pthread_animation(data.num_threads, data.rows, data.cols, visi_name);
        if (data.handle == NULL) {
            printf("ERROR init_pthread_animation\n");
            exit(1);
        }
        
        data.image_buff = get_animation_buffer(data.handle);
        if(data.image_buff == NULL) {
            printf("ERROR get_animation_buffer returned NULL\n");
            exit(1);
        }
    }

    /* ASCII output: clear screen & print the initial board */
    if (data.output_mode == OUTPUT_ASCII) {
        if (system("clear")) { perror("clear"); exit(1); }
        print_board(&data, 0);
    }

    /* Invoke play_gol in different ways based on the run mode */
    if (data.output_mode == OUTPUT_NONE) {  // run with no animation
        ret = gettimeofday(&start_time, NULL);
    
        for (int i = 0; i < data.num_threads; i++) {
            data.id = i;
            partition(&data);            
            tid_data[i] = data;
            ret = pthread_create(&tids[i], NULL, play_gol, &tid_data[i]);
            if (ret) {
                perror("Error pthread_create\n");
                exit(1);
            }   
        }
        for (int i = 0; i < data.num_threads; i++) {
            pthread_join(tids[i], 0);
        }
        ret = gettimeofday(&stop_time, NULL);
    }
    else if (data.output_mode == OUTPUT_ASCII) { // run with ascii animation
        ret = gettimeofday(&start_time, NULL);
        for (int i = 0; i < data.num_threads; i++) {
            data.id = i;
            partition(&data);            
            tid_data[i] = data;
            ret = pthread_create(&tids[i], NULL, play_gol, &tid_data[i]);
            if (ret) {
                perror("Error pthread_create\n");
                exit(1);
            }       
        }
        for (int i = 0; i < data.num_threads; i++) {
            pthread_join(tids[i], 0);
        }
        ret = gettimeofday(&stop_time, NULL);

        // clear the previous print_board output from the terminal:
        // (NOTE: you can comment out this line while debugging)
        if (system("clear")) { perror("clear"); exit(1); }

        // NOTE: DO NOT modify this call to print_board at the end
        //       (it's to help us with grading your output)
        print_board(&data, data.iters);
    }
    else {  // OUTPUT_VISI: run with ParaVisi animation

        for (int i = 0; i < data.num_threads; i++) {
            data.id = i;
            partition(&data);
            tid_data[i] = data;
            ret = pthread_create(&tids[i], NULL, play_gol, &tid_data[i]);
            if (ret) {
                perror("Error pthread_create\n");
                exit(1);
            }
        }
        
        run_animation(data.handle, data.iters);

        for (int i = 0; i < data.num_threads; i++) {
            pthread_join(tids[i], 0);
        }
    }

    if (pthread_mutex_destroy(&my_mutex)) {
        printf("pthread_mutex_destroy error\n");
        exit(1);
    }
    if (pthread_barrier_destroy(&my_barrier)) {
        printf("pthread_barrier_destroy error\n");
        exit(1);
    }

    if (data.output_mode != OUTPUT_VISI) {
        
        double seconds = stop_time.tv_sec - start_time.tv_sec;
        double micros = (stop_time.tv_usec - start_time.tv_usec)/1000000.0;
        secs = seconds + micros;

        fprintf(stdout, "Total time: %0.3f seconds\n", secs);
        fprintf(stdout, "Number of live cells after %d rounds: %d\n\n",
                data.iters, total_live);
    }

    free(tids);
    free(tid_data);
    free(data.array);
    free(data.new_array);

    return 0;
}

//Checks the amount of live neighbors that the coordinate (k,l) has
//returns the number of live neighbors
int check_neighbors(struct gol_data *data, int k, int l) {

    int num_neighbors, neighbor_row, neighbor_col, i, j;
    num_neighbors = 0;
    for (i = -1; i < 2; i++) {
        neighbor_row = (k + i + data->rows) % (data->rows);
        for (j = -1; j < 2; j++) {
            if (i == 0 && j == 0) {
                continue;
            }
            neighbor_col = (l + j + data->cols) % (data->cols);
            if (data->array[neighbor_row * data->cols + neighbor_col] == 1) {
                num_neighbors += 1;
            }
        }
    }

    return num_neighbors;
}

//sets the coordinate (i,j) to live if it meets the game requirements
//returns the local number of live cells to modify.  ->modifies the array as is. 
int set_alive(struct gol_data *data, int live_neighbors, int i, int j) {
    
    int alive = 0;

    if (data->array[i*(data->cols)+j] == 1) {
        if (live_neighbors < 2 ) {
            alive--;
            data->new_array[i*(data->cols)+j] = 0;
        }
        else if (live_neighbors > 3) {
            alive--;
            data->new_array[i*(data->cols)+j] = 0;
        }
        else {
            data->new_array[i*(data->cols)+j] = 1;
        }
    }
    else {
        if (live_neighbors == 3) {
            data->new_array[i*(data->cols)+j] = 1;
            alive++;
        }
        else {
            data->new_array[i*(data->cols)+j] = 0;
        }
    }
    return alive;
}

//updates the colors based on whether the coordinate is live or not
//copied and edited from visi_example in week notes
void update_colors(struct gol_data *data) {

    int i, j, r, c, index, buff_i;
    color3 *buff;

    buff = data->image_buff;  // just for readability
    r = data->row_end;
    c = data->col_end;

    for (i = data->row_start; i < r; i++) {
        for (j = data->col_start; j < c; j++) {
            index = i*(data->cols) + j;
            // translate row index to y-coordinate value
            // in the image buffer, r,c=0,0 is assumed to be the _lower_ left
            // in the grid, r,c=0,0 is _upper_ left.
            buff_i = ((data->rows) - (i+1))*(data->cols) + j;

            // update animation buffer
            if (data->array[index] == 1) {
                buff[buff_i] = c3_black;
            } else if (data->array[index] == 0) {
                buff[buff_i] = colors[((data->id)%8)];
            } 
        }
    }
}

//partitions the board based on the id field from data, which
//is unique to each individual thread. sets the start and 
//end row and col fields in data. 
void partition(struct gol_data *data) {

    int div, mod, i, smaller, acc = 0;
    int id = data->id;
    int chunk;

    if (data->partition == 0) {
        div = data->rows / data->num_threads;
        mod = data->rows % data->num_threads;
    }
    else if (data->partition == 1) {
        div = data->cols / data->num_threads;
        mod = data->cols % data->num_threads;
    }

    if (id < mod) {
        chunk = div + 1;
        
    }
    else {
        chunk = div;
    }

    if (id < mod) {
        smaller = id;
    }
    else {
        smaller = mod;
    }

    for (i = 0; i < smaller; i++) {
        acc++;
    }
    acc = acc * (div+1);
    for (i = mod; i < id; i++) {
        acc = acc + div;
    }

    if (data->partition == 0) {
        data->row_start = acc;
        data->row_end = data->row_start + chunk; 
        data->col_start = 0;
        data->col_end = data->cols;
    }
    else {
        data->col_start = acc;
        data->col_end = data->col_start + chunk; 
        data->row_start = 0;
        data->row_end = data->rows;
    }
    

}

/* initialize the gol game state from command line arguments
 *       argv[1]: name of file to read game config state from
 *       argv[2]: run mode value
 * data: pointer to gol_data struct to initialize
 * argv: command line args
 *       argv[1]: name of file to read game config state from
 *       argv[2]: run mode
 * returns: 0 on success, 1 on error
 */
int init_game_data_from_args(struct gol_data *data, char **argv) {

    FILE *infile;
    int ret;

    infile = fopen(argv[1], "r");
    if (infile == NULL){
        printf("ERROR: failed to open file: %s\n", argv[1]);
        exit(1);
    }
    ret = fscanf(infile,"%d", &data->rows);
    if (ret ==0){
        printf("Error: failed to read rows");
        exit(1);
    }
    ret = fscanf(infile,"%d", &data->cols);
    if (ret == 0){
        printf("Error: failed to read cols");
        exit(1);
    }

    //creating dynamic array
    //then initializing elements to 0
    data->array = malloc(sizeof(int)*data->rows*data->cols);
    if (data->array == NULL) {
        printf("ERROR: array initialization malloc error");
        exit(1);
    }
    int i, j;
    for (i=0; i < data->rows; i++){
        for (j=0; j < data->cols; j++){
            data->array[i*data->cols+j] = 0;
        }
    }

    data->new_array = malloc(sizeof(int)*data->rows*data->cols);
    if (data->new_array == NULL) {
        printf("ERROR: array initialization malloc error");
        exit(1);
    }
    
    for (i=0; i < data->rows; i++){
        for (j=0; j < data->cols; j++){
            data->new_array[i*data->cols+j] = 0;
        }
    }

    //reading the number of iterations to perform
    ret = fscanf(infile,"%d", &data->iters);
    if (ret ==0){
        printf("Error");
        exit(1);
    }
    //checking and setting output mode
    if (atoi(argv[2]) == 0){
        data->output_mode = OUTPUT_NONE;
    }
    else if (atoi(argv[2]) == 1){
        data->output_mode = OUTPUT_ASCII;
    }
    else if (atoi(argv[2]) == 2){
        data->output_mode = OUTPUT_VISI;
    }
    else{
        printf("Error: invalid print mode choice\n");
        exit(1);
    }

    //checking and setting partition
    if (atoi(argv[4]) == 0) {
        data->partition = 0;
    }
    else if (atoi(argv[4]) == 1) {
        data->partition = 1;
    }
    else {
        printf("Error: invalid partition option.");
        printf(" Valid options are '0' or '1'.\n");
        exit(1);
    }
    
    //checking and setting num_threads
    if (atoi(argv[3]) < 1) {
        printf("Error: invalid number of threads.");
        printf(" Number of threads must be at least 1.\n");
        exit(1);
    } 
    else if (data->partition == 1 && atoi(argv[3]) > data->cols) {
        printf("The number of threads was bigger than the number of columns, ");
        printf("so the number of columns will be used as the number of threads.\n");
        data->num_threads = data->cols;
    }
    else if (data->partition == 0 && atoi(argv[3]) > data->rows) {
        printf("The number of threads was bigger than the number of rows, ");
        printf("so the number of rows will be used as the number of threads.\n");
        data->num_threads = data->rows;
    }
    else {
        data->num_threads = atoi(argv[3]);
    }

    if (atoi(argv[5]) == 0) {
        data->print_part = 0;
    }
    else if (atoi(argv[5]) == 1) {
        data->print_part = 1;
    }
    else {
        printf("Error: invalid print_partition option.");
        printf(" Valid options are '0' or '1'.\n");
        exit(1);
    }

    //reading number of live cells
    ret = fscanf(infile, "%d", &total_live);
    if (ret == 0){
        printf("Error: failed to read number of live cells");
        exit(1);
    }
    
    //reading the live cells
    int num1, num2;
    for (i = 0; i < total_live; i++) {
        ret = fscanf(infile, "%d%d", &num1, &num2);
        if (ret == 0){
        printf("Error: failed to read live cell #%d", (i+1));
        exit(1);
        }
        data->array[num1*data->cols+num2] = 1;
        data->new_array[num1*data->cols+num2] = 1;
    }

    //closing the file
    ret = fclose(infile);
    if (ret != 0) {
        printf("Error: failed to close file: %s\n", argv[1]);
        exit(1);
    }
    return 0;
}

/* the gol application main loop function:
 *  runs rounds of GOL,
 *    * updates program state for next round (world and total_live)
 *    * performs any animation step based on the output/run mode
 *
 *   data: pointer to a struct gol_data  initialized with
 *         all GOL game playing state
 */
void *play_gol(void *args) {

    struct gol_data *data;
    data = (struct gol_data *)args;
    
    int live_neighbors, i, j, k, round_alive = 0;
    int *temp;

    if (data->print_part == 1) {
      printf("tid %2d: rows: %3d: %3d (%d) cols: %3d: %3d (%d)\n", \
        data->id, data->row_start, data->row_end-1, data->row_end - data->row_start, \
        data->col_start, data->col_end-1, data->col_end - data->col_start);  
    }
    
    for (k = 1; k <= data->iters; k++) {
        round_alive = 0;
        for (i = data->row_start; i < data->row_end; i++) {
            for (j = data->col_start; j < data->col_end; j++) {
                live_neighbors = check_neighbors(data, i, j);
                round_alive += set_alive(data, live_neighbors, i, j);
            }
        }

        pthread_mutex_lock(&my_mutex);
        total_live += round_alive;
        pthread_mutex_unlock(&my_mutex);

        temp = data->array;
        data->array = data->new_array;
        data->new_array = temp;
        
        if (data->id == 0) {
            if (data->output_mode == OUTPUT_ASCII) {
                system("clear");
                print_board(data, k);
                usleep(SLEEP_USECS);
            }
        }
        if (data->output_mode == OUTPUT_VISI) {
            update_colors(data);
            draw_ready(data->handle);
            usleep(SLEEP_USECS);
        }
        
        pthread_barrier_wait(&my_barrier);    
    }
    
       
    return NULL;
}

/* Print the board to the terminal.
 *   data: gol game specific data
 *   round: the current round number
 */
void print_board(struct gol_data *data, int round) {

    int i, j;

    /* Print the round number. */
    fprintf(stderr, "Round: %d\n", round);

    for (i = 0; i < data->rows; ++i) {
        for (j = 0; j < data->cols; ++j) {
            if (data->array[i*data->cols+j] == 1) {
                fprintf(stderr, " @");
            }
            else {
                fprintf(stderr, " .");
            }
        }
        fprintf(stderr, "\n");
    }

    /* Print the total number of live cells. */
    fprintf(stderr, "Live cells: %d\n\n", total_live);
}


/**********************************************************/
/***** START: DO NOT MODIFY THIS CODE *****/
/* initialize ParaVisi animation */
int setup_animation(struct gol_data* data) {
    /* connect handle to the animation */
    int num_threads = 1;
    data->handle = init_pthread_animation(num_threads, data->rows,
            data->cols, visi_name);
    if (data->handle == NULL) {
        printf("ERROR init_pthread_animation\n");
        exit(1);
    }
    // get the animation buffer
    data->image_buff = get_animation_buffer(data->handle);
    if(data->image_buff == NULL) {
        printf("ERROR get_animation_buffer returned NULL\n");
        exit(1);
    }
    return 0;
}

/* sequential wrapper functions around ParaVis library functions */
void (*mainloop)(struct gol_data *data);

void* seq_do_something(void * args){
    mainloop((struct gol_data *)args);
    return 0;
}

int connect_animation(void (*applfunc)(struct gol_data *data),
        struct gol_data* data)
{
    pthread_t pid;

    mainloop = applfunc;
    if( pthread_create(&pid, NULL, seq_do_something, (void *)data) ) {
        printf("pthread_created failed\n");
        return 1;
    }
    return 0;
}
/***** END: DO NOT MODIFY THIS CODE *****/
/******************************************************/

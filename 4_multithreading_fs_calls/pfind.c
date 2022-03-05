#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <ftw.h>
#include <stdatomic.h>
#include <stdbool.h>

// ---------------------- linked list structs ---------------------- //

typedef struct node{
    char* str;
    struct node* next;
}node;

typedef struct queue{
    node* head;
    node* tail;
    int size;
}queue;

// ---------------------- global variables ---------------------- //

queue* q;
atomic_int counter = 0;        // counter for files that contains the searching term
bool running = false;
int num_of_threads;
int living_threads = 0;
bool all_threads_created;
int active_threads = 0;
bool waiting_threads_on_empty_queue = false;

// lock for all operations that read/write data to/from the queue
pthread_mutex_t lock_queue;

// lock and cv for the starting signal
pthread_mutex_t lock_running;
pthread_cond_t cv_running;

// lock for all operations that read/write data to/from active_threads variable
pthread_mutex_t lock_active_threads;

pthread_cond_t cv_all_threads_created;
pthread_cond_t cv_waiting_threads_on_empty_queue;

// cv for threads that tried to deque a non empty queue
// when there are threads that were waiting on an empty queue
pthread_cond_t cv_stealing_threads;

// ---------------------- auxiliary functions ---------------------- //

int validate_cli_args(int argc, char** argv){
    if(argc != 4){
        fprintf(stderr, "Error: wrong number of CLI args.\n");
        return 1;
    }

    DIR* dir = opendir(argv[1]);
    if(dir){
        if(closedir(dir))
            return 4;
        return 0;
    } else if (errno == ENOENT){
        fprintf(stderr, "Error: directory doesn't exist.\n");
        return 2;
    } else{
        fprintf(stderr, "Error: opendir() failed.\n");
        return 3;
    }
}

char* get_file_name(char* dir_name, char* suffix){
    char* file_name;
    if(dir_name[strlen(dir_name) - 1] == '/'){
        file_name = malloc(strlen(dir_name) + strlen(suffix) + 1);
        strcpy(file_name, dir_name);
        strcat(file_name, suffix);
    }
    else{
        file_name = malloc(strlen(dir_name) + strlen(suffix) + 2);
        strcpy(file_name, dir_name);
        strcat(file_name, "/");
        strcat(file_name, suffix);
    }
    return file_name;
}

void update_active_threads(char sign){
    pthread_mutex_lock(&lock_active_threads);
    if(sign == '-')
        active_threads--;
    else
        active_threads++;
    pthread_mutex_unlock(&lock_active_threads);
}

int open_dir_failure(char* dir_name){
    if (errno == ENOENT){
        fprintf(stderr, "Error: directory %s doesn't exist.\n", dir_name);
        update_active_threads('-');
        return 1;
    }
    else{
        fprintf(stderr, "Error: opendir() failed.\n");
        update_active_threads('-');
        return 2;
    }
}

// ---------------------- linked list functions ---------------------- //

int init_queue(char* search_dir){
    if(search_dir == NULL){
        fprintf(stderr, "Error: search_dir = NULL\n");
        exit(1);
    }

    q = malloc(sizeof (queue));
    if(q == NULL){
        fprintf(stderr, "Error: malloc() failed.\n");
        exit(1);
    }

    node* n = malloc(sizeof (node));
    if(n == NULL){
        fprintf(stderr, "Error: malloc() failed.\n");
        exit(1);
    }

    if(search_dir[strlen(search_dir) - 1] == '/'){
        n -> str = malloc(strlen(search_dir) + 1);
        strcpy(n -> str, search_dir);
    }
    else{
        n -> str = malloc(strlen(search_dir) + 2);
        strcpy(n -> str, search_dir);
        strcat(n -> str, "/");
    }

    n -> next = NULL;

    q -> head = n;
    q -> tail = n;
    q -> size = 1;

    return 0;
}

node* create_node(char* file_name){
    node* new_node = malloc(sizeof (node));
    if(file_name[strlen(file_name) - 1] == '/'){
        new_node -> str = malloc(strlen(file_name) + 1);
        strcpy(new_node -> str, file_name);
        new_node -> str[strlen(file_name)] = '\0';
    }
    else{
        new_node -> str = malloc(strlen(file_name) + 2);
        strcpy(new_node -> str, file_name);
        strcat(new_node -> str, "/");
        new_node -> str[strlen(file_name) + 1] = '\0';
    }
    new_node -> next = NULL;
    return new_node;
}

void enqueue(char* dir_name){
    node* new_dir = create_node(dir_name);

    pthread_mutex_lock(&lock_queue);

    if(q -> size == 0)
        q -> head = new_dir;
    else
        q -> tail -> next = new_dir;

    q -> tail = new_dir;
    q -> size++;

    pthread_mutex_unlock(&lock_queue);

    // broadcast waiting threads on empty queue
    pthread_cond_broadcast(&cv_waiting_threads_on_empty_queue);
}

char* dequeue(){
    pthread_mutex_lock(&lock_queue);

    // A new thread that want to dequeue a non empty queue when there are
    // threads that were waiting on an empty queue -> has to wait until one of the
    // waiting threads will dequeue
    if (waiting_threads_on_empty_queue && q -> size != 0)
        pthread_cond_wait(&cv_stealing_threads, &lock_queue);

    int is_waiting_thread_on_empty_queue = 0;

    while(q -> size == 0){

        pthread_mutex_lock(&lock_active_threads);
        if(active_threads == 0){
            pthread_mutex_unlock(&lock_queue);
            pthread_mutex_unlock(&lock_active_threads);

            // broadcast waiting threads on empty queue
            pthread_cond_broadcast(&cv_waiting_threads_on_empty_queue);

            // broadcast threads that tried to deque a non empty queue
            // when there are threads that were waiting on an empty queue
            pthread_cond_broadcast(&cv_stealing_threads);

            pthread_exit((void *)EXIT_SUCCESS);
        }
        pthread_mutex_unlock(&lock_active_threads);

        waiting_threads_on_empty_queue = true;
        is_waiting_thread_on_empty_queue = 1;
        pthread_cond_wait(&cv_waiting_threads_on_empty_queue, &lock_queue);
    }

    // Changing the flag when one of the waiting threads on empty queue managed to dequeue
    if(waiting_threads_on_empty_queue && is_waiting_thread_on_empty_queue){
        waiting_threads_on_empty_queue = false;

        // broadcast threads that tried to deque a non empty queue
        // when there are threads that were waiting on an empty queue
        // so that these threads won't be labeled as "stealing"
        pthread_cond_broadcast(&cv_stealing_threads);
    }

    update_active_threads('+');

    node* curr_node = q -> head;

    char* dir_name = malloc(strlen(curr_node -> str) + 1);
    strcpy(dir_name, curr_node -> str);
    dir_name[strlen(curr_node -> str)] = '\0';

    q -> size--;
    q -> head = q -> head -> next;

    if(curr_node != NULL){
        if(curr_node -> str != NULL)
            free(curr_node -> str);
        free(curr_node);
    }

    pthread_mutex_unlock(&lock_queue);

    return dir_name;
}

// ---------------------- searching thread functions ---------------------- //

void* searching_thread(void* symbol){

    pthread_mutex_lock(&lock_running);

    living_threads++;
    if(num_of_threads == living_threads){
        all_threads_created = true;
        pthread_cond_signal(&cv_all_threads_created);
    }

    while(!running){
        pthread_cond_wait(&cv_running, &lock_running);
    }

    pthread_mutex_unlock(&lock_running);

    // -------- after signal -------- //

    // running
    while(1){

        char* dir_name = dequeue();
        DIR* dir = opendir(dir_name);

        if(dir){

            errno = 0;
            struct dirent* d;

            while( NULL != (d = readdir(dir)) ){

                char* file_name = get_file_name(dir_name, d->d_name);
                struct stat st;
                int s = lstat(file_name, &st);

                if(s){
                    fprintf(stderr, "Error: lstat() failed: %s | filename = %s\n", strerror(errno), file_name);
                    update_active_threads('-');
                    free(file_name);
                    free(dir_name);
                    pthread_exit((void *)EXIT_FAILURE);
                }

                // . or ..
                if( strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0 ){
                    free(file_name);
                    continue;
                }

                // file or symbolic link
                if(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
                    if(strstr(d->d_name, symbol)) {
                        printf("%s\n", file_name);
                        counter++;
                    }
                }

                // directory
                else{

                    // check permissions
                    mode_t dir_mode = st.st_mode;
                    if(!(dir_mode & S_IRUSR)){
                        printf("Directory %s: Permission denied.\n", file_name);
                        free(file_name);
                        continue;
                    }

                    enqueue(file_name);
                }
                free(file_name);
            }

            if(errno != 0){
                fprintf(stderr, "Error: readdir() failed: %s\n", strerror(errno));
                update_active_threads('-');
                free(dir_name);
                pthread_exit((void *)EXIT_FAILURE);
            }

            update_active_threads('-');
            free(dir_name);
        }
        else if (errno == EACCES){
            printf("Directory %s: Permission denied.\n", dir_name);
            free(dir_name);
            continue;
        }
        else if(open_dir_failure(dir_name)){
            free(dir_name);
            pthread_exit((void *)EXIT_FAILURE);
        }

        if(closedir(dir)){
            update_active_threads('-');
            pthread_exit((void *)EXIT_FAILURE);
        }
    }
}

// ---------------------- auxiliary functions - threads ---------------------- //

int init_mutexes_and_cvs(){

    int rc = pthread_mutex_init(&lock_queue, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_mutex_init(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_mutex_init(&lock_running, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_mutex_init(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_init(&cv_running, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_init(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_mutex_init(&lock_active_threads, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_mutex_init(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_init(&cv_all_threads_created, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_init(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_init(&cv_waiting_threads_on_empty_queue, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_init(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_init(&cv_stealing_threads, NULL);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_init(): %s\n", strerror(rc));
        exit(1);
    }

    return 0;
}

int destroy_mutexes_and_cvs(){
    int rc = pthread_mutex_destroy(&lock_queue);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_mutex_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_mutex_destroy(&lock_running);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_mutex_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_destroy(&cv_running);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_mutex_destroy(&lock_active_threads);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_mutex_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_destroy(&cv_all_threads_created);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_destroy(&cv_waiting_threads_on_empty_queue);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_destroy(&cv_stealing_threads);

    if (rc) {
        fprintf(stderr, "ERROR in pthread_cond_destroy(): %s\n", strerror(rc));
        exit(1);
    }

    return 0;
}

int create_threads(pthread_t* threads, char* search_term){
    for (int i = 0; i < num_of_threads; i++) {
        //printf("***Main: creating thread %d\n", i);

        int rc = pthread_create(&threads[i], NULL, &searching_thread, search_term);

        if (rc) {
            fprintf(stderr, "Error: pthread_create() failed: %s\n", strerror(rc));
            exit(1);
        }
    }

    return 0;
}

int join_threads(pthread_t* threads, bool* no_thread_with_error){
    void *status;
    for (int i = 0; i < num_of_threads; i++) {

        int rc = pthread_join(threads[i], &status);

        if (rc) {
            fprintf(stderr, "Error: pthread_join() failed: %s\n", strerror(rc));
            exit(1);
        }

        if ((long)status){
            *no_thread_with_error = false;
        }
    }

    return 0;
}

/**
 * argv[1] = search root directory
 * argv[2] = search term
 * argv[3] = number of searching threads
 */

int main(int argc, char** argv) {

    if(validate_cli_args(argc, argv))
        exit(EXIT_FAILURE);

    char* search_dir = argv[1];
    char* search_term = argv[2];
    num_of_threads = atoi(argv[3]); // assumed argv[3] is a valid integer

    // initialize queue of folders
    if(init_queue(search_dir))
        exit(EXIT_FAILURE);

    // initialize mutexes & condition variables
    if(init_mutexes_and_cvs())
        exit(EXIT_FAILURE);

    pthread_t* threads = malloc(sizeof(pthread_t) * num_of_threads);
    if(threads == NULL){
        fprintf(stderr, "Error: malloc() failed.\n");
        exit(EXIT_FAILURE);
    }

    // create threads
    if(create_threads(threads, search_term))
        exit(EXIT_FAILURE);

    // wait for all threads to be created and waiting and then signal them to start

    pthread_mutex_lock(&lock_running);
    while(!all_threads_created){
        pthread_cond_wait(&cv_all_threads_created, &lock_running);
    }

    running = true;
    pthread_mutex_unlock(&lock_running);
    pthread_cond_broadcast(&cv_running);

    // join threads
    bool* no_thread_with_error = malloc(sizeof(bool));
    *no_thread_with_error = true;
    if(join_threads(threads, no_thread_with_error))
        exit(EXIT_FAILURE);

    printf("Done searching, found %d files\n", counter);

    // destroy threads
    if(destroy_mutexes_and_cvs())
        exit(EXIT_FAILURE);

    if(q != NULL)
        free(q);

    if(*no_thread_with_error)
        exit(0);
    else
        exit(1);
}

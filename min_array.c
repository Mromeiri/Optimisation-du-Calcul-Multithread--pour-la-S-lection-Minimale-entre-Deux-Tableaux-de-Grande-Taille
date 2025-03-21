// min_array.c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <sched.h>
#endif

#define ARRAY_SIZE 100000000
#define BLOCK_SIZE 2048
#define NB_MEASURE 10

// Tableaux globaux
double *A, *B, *C;
int nb_threads;
int migration_allowed;
char method[16];

// Pour la méthode farming : compteur global et mutex
int current_block = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Pour stocker le nombre de blocs traités par chaque thread (méthode farming)
int *blocks_processed;

// Nombre total de blocs à traiter
int total_blocks;

// Structure pour passer les informations aux threads
typedef struct {
    int id;
} thread_arg_t;

//
// Fonction utilitaire pour mesurer le temps
//
double get_time_in_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

//
// Fonction de fixation d'affinité (seulement sur Linux)
//
void set_thread_affinity(int thread_id) {
#ifdef __linux__
    if (!migration_allowed) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(thread_id, &cpuset);
        int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (s != 0) {
            fprintf(stderr, "Erreur setting affinity pour thread %d: %s\n", thread_id, strerror(errno));
        }
    }
#endif
}

//
// 1. Répartition cyclique par élément
//
void *thread_cyclic(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    int id = targ->id;
    set_thread_affinity(id);
    for (long i = id; i < ARRAY_SIZE; i += nb_threads) {
        C[i] = (A[i] < B[i]) ? A[i] : B[i];
    }
    return NULL;
}

//
// 2. Répartition cyclique par blocs
//
void *thread_block(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    int id = targ->id;
    set_thread_affinity(id);
    int nb_blocks = (ARRAY_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int blk = id; blk < nb_blocks; blk += nb_threads) {
        int start = blk * BLOCK_SIZE;
        int end = start + BLOCK_SIZE;
        if (end > ARRAY_SIZE)
            end = ARRAY_SIZE;
        for (int i = start; i < end; i++) {
            C[i] = (A[i] < B[i]) ? A[i] : B[i];
        }
    }
    return NULL;
}

//
// 3. Farming : répartition dynamique par blocs
//
void *thread_farming(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    int id = targ->id;
    set_thread_affinity(id);
    int local_count = 0;
    while (1) {
        int blk;
        pthread_mutex_lock(&mutex);
        blk = current_block;
        current_block++;
        pthread_mutex_unlock(&mutex);
        if (blk * BLOCK_SIZE >= ARRAY_SIZE)
            break;
        int start = blk * BLOCK_SIZE;
        int end = start + BLOCK_SIZE;
        if (end > ARRAY_SIZE)
            end = ARRAY_SIZE;
        for (int i = start; i < end; i++) {
            C[i] = (A[i] < B[i]) ? A[i] : B[i];
        }
        local_count++;
    }
    blocks_processed[id] = local_count;
    return NULL;
}

//
// Initialisation des tableaux
//
void init_arrays() {
    A = (double*)malloc(sizeof(double) * ARRAY_SIZE);
    B = (double*)malloc(sizeof(double) * ARRAY_SIZE);
    C = (double*)malloc(sizeof(double) * ARRAY_SIZE);
    if (!A || !B || !C) {
        fprintf(stderr, "Erreur d'allocation mémoire.\n");
        exit(EXIT_FAILURE);
    }
    // Remplissage des tableaux (exemple : A[i] = i, B[i] = ARRAY_SIZE - i)
    for (long i = 0; i < ARRAY_SIZE; i++) {
        A[i] = (double)i;
        B[i] = (double)(ARRAY_SIZE - i);
    }
}

//
// Libération des tableaux
//
void free_arrays() {
    free(A);
    free(B);
    free(C);
}

//
// Fonction principale
//
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <method: cyclic|block|farming> <nb_threads> <migration: 0|1>\n", argv[0]);
        return EXIT_FAILURE;
    }
    strncpy(method, argv[1], sizeof(method) - 1);
    nb_threads = atoi(argv[2]);
    migration_allowed = atoi(argv[3]);
    if (nb_threads < 1) {
        fprintf(stderr, "Le nombre de threads doit être >= 1.\n");
        return EXIT_FAILURE;
    }
    
    init_arrays();

    if (strcmp(method, "farming") == 0) {
        blocks_processed = (int*)calloc(nb_threads, sizeof(int));
        total_blocks = (ARRAY_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
    }

    double total_time = 0.0;
    for (int measure = 0; measure < NB_MEASURE; measure++) {
        if (strcmp(method, "farming") == 0) {
            current_block = 0;
            memset(blocks_processed, 0, nb_threads * sizeof(int));
        }
        pthread_t *threads = (pthread_t*)malloc(nb_threads * sizeof(pthread_t));
        thread_arg_t *targs = (thread_arg_t*)malloc(nb_threads * sizeof(thread_arg_t));
        double start_time = get_time_in_seconds();
        for (int i = 0; i < nb_threads; i++) {
            targs[i].id = i;
            int ret;
            if (strcmp(method, "cyclic") == 0) {
                ret = pthread_create(&threads[i], NULL, thread_cyclic, &targs[i]);
            } else if (strcmp(method, "block") == 0) {
                ret = pthread_create(&threads[i], NULL, thread_block, &targs[i]);
            } else if (strcmp(method, "farming") == 0) {
                ret = pthread_create(&threads[i], NULL, thread_farming, &targs[i]);
            } else {
                fprintf(stderr, "Méthode inconnue: %s\n", method);
                exit(EXIT_FAILURE);
            }
            if (ret != 0) {
                fprintf(stderr, "Erreur lors de la création du thread %d\n", i);
                exit(EXIT_FAILURE);
            }
        }
        for (int i = 0; i < nb_threads; i++) {
            pthread_join(threads[i], NULL);
        }
        double end_time = get_time_in_seconds();
        total_time += (end_time - start_time);
        free(threads);
        free(targs);
    }
    double average_time = total_time / NB_MEASURE;

    int min_blocks = -1, max_blocks = -1;
    if (strcmp(method, "farming") == 0) {
        for (int i = 0; i < nb_threads; i++) {
            int count = blocks_processed[i];
            if (min_blocks == -1 || count < min_blocks)
                min_blocks = count;
            if (max_blocks == -1 || count > max_blocks)
                max_blocks = count;
        }
    }

    // Affichage des résultats en format CSV
    // Pour farming : méthode, nb_threads, migration, temps moyen, min_blocks, max_blocks
    // Pour cyclic et block : méthode, nb_threads, migration, temps moyen
    if (strcmp(method, "farming") == 0) {
        printf("%s,%d,%d,%.6f,%d,%d\n", method, nb_threads, migration_allowed, average_time, min_blocks, max_blocks);
    } else {
        printf("%s,%d,%d,%.6f\n", method, nb_threads, migration_allowed, average_time);
    }

    if (strcmp(method, "farming") == 0) {
        free(blocks_processed);
    }
    free_arrays();
    return 0;
}

/*
 * waterwave.h
 * Simulación de ondas de agua usando diferencias finitas
 * Conversión de C++ a C
 */

#ifndef WATERWAVE_H
#define WATERWAVE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <assert.h>

/* ============================================
 * CONFIGURACIÓN Y CONSTANTES
 * ============================================ */

// Estructura para almacenar la configuración
typedef struct {
    size_t grid_size;
    size_t max_drops;
    size_t drop_step;
    size_t step_count;
    size_t dump_every;
    size_t threads;
    float gravity;
    float d_t;
    float d_x;
    float d_y;
} Config;

// Variable global de configuración (se inicializa en main)
extern Config g_config;

/* ============================================
 * ESTRUCTURA: GRID
 * ============================================ */

typedef struct {
    size_t size;      // Tamaño del grid (NxN)
    float* cells;     // Array de celdas (size * size elementos)
} Grid;

/* ============================================
 * ESTRUCTURA: WORK ITEM (para ThreadPool)
 * ============================================ */

typedef struct {
    void (*func)(void*);  // Puntero a función
    void* arg;            // Argumento de la función
} WorkItem;

/* ============================================
 * ESTRUCTURA: THREADPOOL
 * ============================================ */

typedef struct {
    pthread_t* threads;           // Array de hilos
    size_t num_threads;           // Número de hilos
    
    WorkItem* work_queue;         // Cola de trabajos
    size_t queue_size;            // Tamaño actual de la cola
    size_t queue_capacity;        // Capacidad máxima de la cola
    size_t queue_front;           // Índice frontal
    size_t queue_back;            // Índice trasero
    
    pthread_mutex_t work_mutex;   // Mutex para la cola
    pthread_cond_t has_work;      // Condición: hay trabajo disponible
    
    pthread_mutex_t work_done_mutex;  // Mutex para sincronización
    pthread_cond_t work_done;         // Condición: trabajo terminado
    
    atomic_size_t pending_work;   // Contador atómico de trabajos pendientes
    bool should_exit;             // Flag para terminar hilos
} ThreadPool;

/* ============================================
 * ESTRUCTURA: SIMULATION STATE
 * ============================================ */

typedef struct {
    size_t step;              // Paso actual de simulación
    size_t drop_count;        // Número de gotas generadas
    
    size_t* pending_drop_positions;  // Posiciones predefinidas (si las hay)
    size_t pending_positions_size;   // Tamaño del array
    
    // Grids principales (altura y momentos)
    Grid h;   // Altura de la superficie del agua
    Grid u;   // Momento en dirección X
    Grid v;   // Momento en dirección Y
    
    // Grids de derivadas (valores en las caras)
    Grid hx, hy;  // Derivadas de altura
    Grid ux, uy;  // Derivadas de momento X
    Grid vx, vy;  // Derivadas de momento Y
    
    ThreadPool* threadpool;  // Pool de hilos (si se usa paralelización)

    void* work_data_pool;

} SimulationState;

/* ============================================
 * ESTRUCTURA: DATOS PARA CALLBACKS
 * ============================================ */

// Estructura para pasar datos a las funciones callback
typedef struct {
    SimulationState* state;
    size_t i;
    size_t j;
} CallbackData;

/* ============================================
 * FUNCIONES: CONFIGURACIÓN
 * ============================================ */

// Lee la configuración desde un archivo
bool config_load(const char* filename, Config* config);

// Carga configuración con valores por defecto
void config_set_defaults(Config* config);

// Imprime la configuración actual
void config_print(const Config* config);

/* ============================================
 * FUNCIONES: AUXILIARES
 * ============================================ */

// Calcula el índice lineal en un grid 2D
static inline size_t index_for(size_t x, size_t y, size_t size) {
    return y * size + x;
}

/* ============================================
 * FUNCIONES: GRID
 * ============================================ */

// Crea un grid de tamaño size x size con valor inicial
Grid grid_create(size_t size, float initial_value);

// Destruye un grid y libera su memoria
void grid_destroy(Grid* g);

// Obtiene el valor en la posición (x, y)
float grid_get(const Grid* g, size_t x, size_t y);

// Establece el valor en la posición (x, y)
void grid_set(Grid* g, size_t x, size_t y, float value);

// Copia una columna con un factor de escala
void grid_copy_col(Grid* g, size_t from_x, size_t to_x, float factor);

// Copia una fila con un factor de escala
void grid_copy_row(Grid* g, size_t from_y, size_t to_y, float factor);

// Imprime un grid (para debugging)
void grid_print(const Grid* g, const char* name);

/* ============================================
 * FUNCIONES: THREADPOOL
 * ============================================ */

// Crea un pool de hilos
ThreadPool* threadpool_create(size_t num_threads);

// Destruye el pool de hilos
void threadpool_destroy(ThreadPool* tp);

// Añade un trabajo a la cola
void threadpool_enqueue(ThreadPool* tp, void (*func)(void*), void* arg);

// Espera a que todos los trabajos terminen
void threadpool_join(ThreadPool* tp);

// Función worker (ejecutada por cada hilo)
void* threadpool_worker(void* arg);

/* ============================================
 * FUNCIONES: SIMULATION STATE
 * ============================================ */

// Crea el estado de simulación
SimulationState* simulation_create();

// Destruye el estado de simulación
void simulation_destroy(SimulationState* s);

// Obtiene la siguiente coordenada para una gota (aleatoria o predefinida)
size_t simulation_next_drop_coordinate(SimulationState* s);

// Genera una gota si corresponde
void simulation_maybe_spawn_drop(SimulationState* s);

// Refleja las condiciones de frontera
void simulation_reflect_boundaries(SimulationState* s);

// Paraleliza un bucle doble
void simulation_maybe_parallelize_loop(
    SimulationState* s,
    size_t x_start, size_t x_end,
    size_t y_start, size_t y_end,
    void (*callback)(size_t, size_t, void*),
    void* callback_data
);

// Actualiza en dirección X
void simulation_update_in_x_direction(SimulationState* s);

// Actualiza en dirección Y
void simulation_update_in_y_direction(SimulationState* s);

// Promedia los momentos
void simulation_average_momentums(SimulationState* s);

// Imprime el estado actual (si corresponde)
void simulation_maybe_plot(const SimulationState* s);

// Ejecuta un paso de simulación
bool simulation_tick(SimulationState* s);

/* ============================================
 * CALLBACKS PARA PARALELIZACIÓN
 * ============================================ */

void callback_update_x(size_t i, size_t j, void* data);
void callback_update_y(size_t i, size_t j, void* data);
void callback_average(size_t i, size_t j, void* data);

#endif // WATERWAVE_H
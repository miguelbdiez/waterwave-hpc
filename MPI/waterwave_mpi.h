/*
 * waterwave_mpi.h
 * Simulación de ondas de agua usando MPI (Message Passing Interface)

 */

#ifndef WATERWAVE_MPI_H
#define WATERWAVE_MPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <mpi.h>

/* ============================================
 * ESTRUCTURAS BÁSICAS
 * ============================================ */

// Punto 2D
typedef struct {
    uint32_t x;
    uint32_t y;
} Point;

// Tamaño 2D
typedef struct {
    uint32_t w;  // width
    uint32_t h;  // height
} Size;

// Rectángulo
typedef struct {
    Point p;
    Size s;
} Rect;

/* ============================================
 * CONFIGURACIÓN
 * ============================================ */

typedef struct {
    uint32_t grid_size;
    uint32_t max_drops;
    uint32_t drop_step;
    uint32_t step_count;
    uint32_t dump_every;
    
    // Constantes físicas
    float gravity;
    float d_t;
    float d_x;
    float d_y;
} Config;

/* ============================================
 * ESTADO MPI GLOBAL
 * ============================================ */

typedef struct {
    int proc_count;      // Total de procesos MPI
    int pid;             // ID de este proceso (rank)
    int coordinator;     // Proceso coordinador (normalmente 0)
} MPIState;

/* ============================================
 * GRID LOCAL
 * ============================================ */

typedef struct {
    Size size;           // Dimensiones del grid local
    float* cells;        // Array de celdas [size.h][size.w]
} Grid;

/* ============================================
 * ESTADO DE SIMULACIÓN
 * ============================================ */

typedef struct {
    uint32_t step;
    uint32_t drop_count;
    
    // Información del grid local
    uint32_t row_count;      // Número de filas locales
    uint32_t initial_row;    // Fila global inicial de este proceso
    
    // Grids principales (altura y momentos)
    Grid h;   // Altura del agua
    Grid u;   // Momento en X
    Grid v;   // Momento en Y
    
    // Grids de derivadas
    Grid hx, hy;
    Grid ux, uy;
    Grid vx, vy;
    
    // Posiciones predefinidas de gotas (opcional)
    uint32_t* pending_drop_positions;
    size_t pending_positions_size;
} SimulationState;

/* ============================================
 * VARIABLES GLOBALES
 * ============================================ */

extern Config g_config;
extern MPIState g_mpi;

/* ============================================
 * MACROS DE UTILIDAD
 * ============================================ */

// Macro para verificar errores de MPI
#define MPI_CHECK(expr) \
    do { \
        int result = (expr); \
        if (result != MPI_SUCCESS) { \
            fprintf(stderr, "[%d] MPI Error in %s: %d\n", g_mpi.pid, #expr, result); \
            MPI_Abort(MPI_COMM_WORLD, result); \
        } \
    } while(0)

// Log con rank del proceso
#define LOG(fmt, ...) \
    fprintf(stderr, "[%d] " fmt, g_mpi.pid, ##__VA_ARGS__)

/* ============================================
 * FUNCIONES DE CONFIGURACIÓN
 * ============================================ */

void config_set_defaults(Config* config);
bool config_load(const char* filename, Config* config);
void config_print(const Config* config);

/* ============================================
 * FUNCIONES DE GRID
 * ============================================ */

Grid grid_create(Size size, float initial_value);
void grid_destroy(Grid* g);
float grid_get(const Grid* g, uint32_t x, uint32_t y);
void grid_set(Grid* g, uint32_t x, uint32_t y, float value);
float* grid_get_row(Grid* g, uint32_t y);
const float* grid_get_row_const(const Grid* g, uint32_t y);
uint32_t grid_row_size(const Grid* g);
void grid_copy_col(Grid* g, uint32_t from_x, uint32_t to_x, float factor);
void grid_copy_row(Grid* g, uint32_t from_y, uint32_t to_y, float factor);

/* ============================================
 * FUNCIONES MPI
 * ============================================ */

// Inicializar estado MPI
void mpi_state_init(int* argc, char*** argv);
void mpi_state_finalize(void);
bool mpi_is_coordinator(void);

// Broadcast
void mpi_bcast_point(Point* point);

// Comunicación de filas (asíncrona y síncrona)
void mpi_async_send_row(const float* src, uint32_t count, int dest);
void mpi_sync_recv_row(float* dest, uint32_t count, int src);

/* ============================================
 * FUNCIONES DE SIMULACIÓN
 * ============================================ */

// Cálculo de filas locales
uint32_t local_row_count(void);
uint32_t initial_row_for_process(void);

// Crear y destruir simulación
SimulationState* simulation_create(void);
void simulation_destroy(SimulationState* s);

// Conversión coordenadas global → local
bool global_to_local(const SimulationState* s, Point global, Point* local);

// Generación de gotas
uint32_t simulation_next_drop_coordinate(SimulationState* s);
void simulation_maybe_spawn_drop(SimulationState* s);

// Sincronización con vecinos
void simulation_reflect_boundaries_and_sync(SimulationState* s);
void simulation_sync_momentums(SimulationState* s);

// Algoritmo de simulación
void simulation_update_in_x_direction(SimulationState* s);
void simulation_update_in_y_direction(SimulationState* s);
void simulation_average_momentums(SimulationState* s);

// Callbacks para loops
void callback_update_x(uint32_t i, uint32_t j, void* data);
void callback_update_y(uint32_t i, uint32_t j, void* data);
void callback_average(uint32_t i, uint32_t j, void* data);

// Visualización
void simulation_maybe_plot(const SimulationState* s);

// Paso de simulación
bool simulation_tick(SimulationState* s);

#endif // WATERWAVE_MPI_H

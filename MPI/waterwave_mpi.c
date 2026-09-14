/*
 * waterwave_mpi.c
 * Implementación de simulación de ondas con MPI
 */

#include "waterwave_mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>

/* ============================================
 * VARIABLES GLOBALES
 * ============================================ */

Config g_config;
MPIState g_mpi;

/* ============================================
 * FUNCIONES DE CONFIGURACIÓN
 * ============================================ */

void config_set_defaults(Config* config) {
    config->grid_size = 64;
    config->max_drops = 5;
    config->drop_step = 500;
    config->step_count = 1000;
    config->dump_every = 100;
    
    config->gravity = 9.8f;
    config->d_t = 0.02f;
    config->d_x = 1.0f;
    config->d_y = 1.0f;
}

static uint32_t get_env_uint(const char* name, uint32_t default_value) {
    const char* env = getenv(name);
    if (env) {
        return (uint32_t)atoi(env);
    }
    return default_value;
}

static float get_env_float(const char* name, float default_value) {
    const char* env = getenv(name);
    if (env) {
        return (float)atof(env);
    }
    return default_value;
}

bool config_load(const char* filename, Config* config) {
    // Primero cargar valores por defecto
    config_set_defaults(config);
    
    // Intentar cargar desde archivo
    FILE* file = fopen(filename, "r");
    if (file) {
        // Leer del archivo (formato KEY=VALUE)
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            char key[128];
            char value[128];
            
            if (sscanf(line, "%127[^=]=%127s", key, value) == 2) {
                if (strcmp(key, "GRID_SIZE") == 0) config->grid_size = atoi(value);
                else if (strcmp(key, "MAX_DROPS") == 0) config->max_drops = atoi(value);
                else if (strcmp(key, "DROP_STEP") == 0) config->drop_step = atoi(value);
                else if (strcmp(key, "STEP_COUNT") == 0) config->step_count = atoi(value);
                else if (strcmp(key, "DUMP_EVERY") == 0) config->dump_every = atoi(value);
                else if (strcmp(key, "GRAVITY") == 0) config->gravity = (float)atof(value);
                else if (strcmp(key, "D_T") == 0) config->d_t = (float)atof(value);
                else if (strcmp(key, "D_X") == 0) config->d_x = (float)atof(value);
                else if (strcmp(key, "D_Y") == 0) config->d_y = (float)atof(value);
            }
        }
        fclose(file);
    }
    
    // Variables de entorno SOBRESCRIBEN valores del archivo
    const char* env;
    if ((env = getenv("GRID_SIZE")) != NULL) config->grid_size = atoi(env);
    if ((env = getenv("MAX_DROPS")) != NULL) config->max_drops = atoi(env);
    if ((env = getenv("DROP_STEP")) != NULL) config->drop_step = atoi(env);
    if ((env = getenv("STEP_COUNT")) != NULL) config->step_count = atoi(env);
    if ((env = getenv("DUMP_EVERY")) != NULL) config->dump_every = atoi(env);
    if ((env = getenv("GRAVITY")) != NULL) config->gravity = (float)atof(env);
    if ((env = getenv("D_T")) != NULL) config->d_t = (float)atof(env);
    if ((env = getenv("D_X")) != NULL) config->d_x = (float)atof(env);
    if ((env = getenv("D_Y")) != NULL) config->d_y = (float)atof(env);
    
    return true;
}

void config_print(const Config* config) {
    if (!mpi_is_coordinator()) return;  // Solo el coordinador imprime
    
    printf("===========================================\n");
    printf("  CONFIGURACIÓN DE LA SIMULACIÓN (MPI)\n");
    printf("===========================================\n");
    printf("  Tamaño del grid:      %u x %u\n", config->grid_size, config->grid_size);
    printf("  Procesos MPI:         %d\n", g_mpi.proc_count);
    printf("  Máximo de gotas:      %u\n", config->max_drops);
    printf("  Paso entre gotas:     %u\n", config->drop_step);
    printf("  Pasos totales:        %u\n", config->step_count);
    printf("  Salida cada:          %u pasos\n", config->dump_every);
    printf("  Gravedad:             %.2f m/s²\n", config->gravity);
    printf("  Delta tiempo (dt):    %.4f s\n", config->d_t);
    printf("  Delta espacio (dx):   %.2f m\n", config->d_x);
    printf("  Delta espacio (dy):   %.2f m\n", config->d_y);
    printf("===========================================\n\n");
}

/* ============================================
 * FUNCIONES DE GRID
 * ============================================ */

static inline uint32_t grid_index(const Grid* g, uint32_t x, uint32_t y) {
    return y * g->size.w + x;
}

Grid grid_create(Size size, float initial_value) {
    Grid g;
    g.size = size;
    
    size_t total_cells = (size_t)size.w * size.h;
    g.cells = (float*)malloc(total_cells * sizeof(float));
    
    if (!g.cells) {
        fprintf(stderr, "Error: No se pudo asignar memoria para grid %ux%u\n", 
                size.w, size.h);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    // Inicializar
    for (size_t i = 0; i < total_cells; i++) {
        g.cells[i] = initial_value;
    }
    
    return g;
}

void grid_destroy(Grid* g) {
    if (g && g->cells) {
        free(g->cells);
        g->cells = NULL;
    }
}

float grid_get(const Grid* g, uint32_t x, uint32_t y) {
    return g->cells[grid_index(g, x, y)];
}

void grid_set(Grid* g, uint32_t x, uint32_t y, float value) {
    assert(!isnan(value));
    assert(isfinite(value));
    g->cells[grid_index(g, x, y)] = value;
}

float* grid_get_row(Grid* g, uint32_t y) {
    return &g->cells[grid_index(g, 0, y)];
}

const float* grid_get_row_const(const Grid* g, uint32_t y) {
    return &g->cells[grid_index(g, 0, y)];
}

uint32_t grid_row_size(const Grid* g) {
    return g->size.w;
}

void grid_copy_col(Grid* g, uint32_t from_x, uint32_t to_x, float factor) {
    for (uint32_t y = 0; y < g->size.h; y++) {
        float src = grid_get(g, from_x, y);
        grid_set(g, to_x, y, src * factor);
    }
}

void grid_copy_row(Grid* g, uint32_t from_y, uint32_t to_y, float factor) {
    for (uint32_t x = 0; x < g->size.w; x++) {
        float src = grid_get(g, x, from_y);
        grid_set(g, x, to_y, src * factor);
    }
}

/* ============================================
 * FUNCIONES MPI
 * ============================================ */

void mpi_state_init(int* argc, char*** argv) {
    MPI_CHECK(MPI_Init(argc, argv));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &g_mpi.proc_count));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &g_mpi.pid));
    g_mpi.coordinator = 0;
    
    LOG("MPI inicializado (%d procesos totales)\n", g_mpi.proc_count);
}

void mpi_state_finalize(void) {
    MPI_CHECK(MPI_Finalize());
}

bool mpi_is_coordinator(void) {
    return g_mpi.pid == g_mpi.coordinator;
}

void mpi_bcast_point(Point* point) {
    // Broadcast como array de uint32_t
    uint32_t data[2] = {point->x, point->y};
    MPI_CHECK(MPI_Bcast(data, 2, MPI_UINT32_T, g_mpi.coordinator, MPI_COMM_WORLD));
    point->x = data[0];
    point->y = data[1];
}

void mpi_async_send_row(const float* src, uint32_t count, int dest) {
    MPI_Request req;
    MPI_CHECK(MPI_Isend(src, count, MPI_FLOAT, dest, 0, MPI_COMM_WORLD, &req));
    MPI_Request_free(&req);  // Fire and forget
}

void mpi_sync_recv_row(float* dest, uint32_t count, int src) {
    MPI_CHECK(MPI_Recv(dest, count, MPI_FLOAT, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
}

/* ============================================
 * FUNCIONES DE CÁLCULO LOCAL
 * ============================================ */

uint32_t local_row_count(void) {
    uint32_t base_count = g_config.grid_size / g_mpi.proc_count;
    
    // El último proceso toma las filas restantes
    if (g_mpi.pid == g_mpi.proc_count - 1) {
        base_count += g_config.grid_size % g_mpi.proc_count;
    }
    
    return base_count;
}

uint32_t initial_row_for_process(void) {
    return (g_config.grid_size / g_mpi.proc_count) * g_mpi.pid;
}

/* ============================================
 * SIMULACIÓN - Continuará en siguiente parte
 * ============================================ */

/* ============================================
 * CREACIÓN Y DESTRUCCIÓN DE SIMULACIÓN
 * ============================================ */

SimulationState* simulation_create(void) {
    SimulationState* s = (SimulationState*)malloc(sizeof(SimulationState));
    if (!s) {
        fprintf(stderr, "[%d] Error: No se pudo asignar memoria para SimulationState\n", 
                g_mpi.pid);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    s->step = 0;
    s->drop_count = 0;
    s->row_count = local_row_count();
    s->initial_row = initial_row_for_process();
    s->pending_drop_positions = NULL;
    s->pending_positions_size = 0;
    
    LOG("Grid local: %u filas (fila inicial global: %u)\n", 
        s->row_count, s->initial_row);
    
    // Crear grids locales
    // Tamaño: (ancho_global + 2) x (filas_locales + 2)
    Size main_size = {g_config.grid_size + 2, s->row_count + 2};
    Size deriv_size = {g_config.grid_size + 1, s->row_count + 1};
    
    s->h = grid_create(main_size, 1.0f);
    s->u = grid_create(main_size, 0.0f);
    s->v = grid_create(main_size, 0.0f);
    
    s->hx = grid_create(deriv_size, 0.0f);
    s->hy = grid_create(deriv_size, 0.0f);
    s->ux = grid_create(deriv_size, 0.0f);
    s->uy = grid_create(deriv_size, 0.0f);
    s->vx = grid_create(deriv_size, 0.0f);
    s->vy = grid_create(deriv_size, 0.0f);
    
    LOG("Simulación local creada exitosamente\n");
    
    return s;
}

void simulation_destroy(SimulationState* s) {
    if (!s) return;
    
    grid_destroy(&s->h);
    grid_destroy(&s->u);
    grid_destroy(&s->v);
    grid_destroy(&s->hx);
    grid_destroy(&s->hy);
    grid_destroy(&s->ux);
    grid_destroy(&s->uy);
    grid_destroy(&s->vx);
    grid_destroy(&s->vy);
    
    if (s->pending_drop_positions) {
        free(s->pending_drop_positions);
    }
    
    free(s);
}

/* ============================================
 * CONVERSIÓN COORDENADAS GLOBAL → LOCAL
 * ============================================ */

bool global_to_local(const SimulationState* s, Point global, Point* local) {
    // Verificar si la coordenada Y está en nuestro rango
    if (global.y < s->initial_row) {
        return false;  // Está antes de nuestras filas
    }
    
    uint32_t row_relative = global.y - s->initial_row;
    
    if (row_relative >= s->row_count) {
        return false;  // Está después de nuestras filas
    }
    
    // Coordenada está en nuestro rango local
    local->x = global.x;
    local->y = row_relative;
    
    return true;
}

/* ============================================
 * GENERACIÓN DE GOTAS
 * ============================================ */

uint32_t simulation_next_drop_coordinate(SimulationState* s) {
    // Si hay posiciones predefinidas, usar esas
    if (s->pending_positions_size > 0) {
        s->pending_positions_size--;
        return s->pending_drop_positions[s->pending_positions_size];
    }
    
    // Generar coordenada aleatoria
    return rand() % g_config.grid_size;
}

void simulation_maybe_spawn_drop(SimulationState* s) {
    // Solo generar en múltiplos de DROP_STEP
    if (s->step % g_config.drop_step != 0) {
        return;
    }
    
    // Límite de gotas
    if (s->drop_count >= g_config.max_drops) {
        return;
    }
    
    s->drop_count++;
    
    // Solo el coordinador genera la posición
    Point pos = {0, 0};
    if (mpi_is_coordinator()) {
        pos.x = simulation_next_drop_coordinate(s);
        pos.y = simulation_next_drop_coordinate(s);
        LOG("Generando gota en (%u, %u)\n", pos.x, pos.y);
    }
    
    // Broadcast a todos los procesos
    mpi_bcast_point(&pos);
    
    // Parámetros de la gota
    const uint32_t width = 21;
    const float height = 1.5f;
    
    const uint32_t i_min = (pos.x > width) ? (pos.x - width) : 0;
    const uint32_t j_min = (pos.y > width) ? (pos.y - width) : 0;
    const uint32_t i_max = (pos.x + width < g_config.grid_size - 1) ? 
                           (pos.x + width) : (g_config.grid_size - 1);
    const uint32_t j_max = (pos.y + width < g_config.grid_size - 1) ? 
                           (pos.y + width) : (g_config.grid_size - 1);
    
    // Aplicar gota solo en celdas locales
    for (uint32_t i = i_min; i <= i_max; i++) {
        for (uint32_t j = j_min; j <= j_max; j++) {
            Point global = {i, j};
            Point local;
            
            if (!global_to_local(s, global, &local)) {
                continue;  // Esta celda no está en mi grid local
            }
            
            // Calcular factor gaussiano
            float dx = ((float)i - (float)pos.x) / (float)width;
            float dy = ((float)j - (float)pos.y) / (float)width;
            float factor = expf(-5.0f * (dx * dx + dy * dy));
            
            // Aplicar perturbación (coordenadas locales + offset por bordes)
            float current_h = grid_get(&s->h, local.x + 1, local.y + 1);
            grid_set(&s->h, local.x + 1, local.y + 1, current_h + height * factor);
        }
    }
}

/* ============================================
 * SINCRONIZACIÓN DE FRONTERAS
 * ============================================ */

void simulation_reflect_boundaries_and_sync(SimulationState* s) {
    // ===== SINCRONIZACIÓN VERTICAL (entre procesos) =====
    
    // Si soy el primer proceso (tengo la fila 0 global)
    if (s->initial_row == 0) {
        // Reflejo en el borde superior
        grid_copy_row(&s->h, 1, 0, 1.0f);
        grid_copy_row(&s->u, 1, 0, 1.0f);
        grid_copy_row(&s->v, 1, 0, -1.0f);  // Momento Y se invierte
    } else {
        // Enviar mi primera fila al vecino de arriba
        mpi_async_send_row(grid_get_row(&s->h, 1), grid_row_size(&s->h), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->u, 1), grid_row_size(&s->u), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->v, 1), grid_row_size(&s->v), g_mpi.pid - 1);
    }
    
    // Si soy el último proceso (tengo la última fila global)
    if (s->initial_row + s->row_count == g_config.grid_size) {
        // Reflejo en el borde inferior
        grid_copy_row(&s->h, s->row_count, s->row_count + 1, 1.0f);
        grid_copy_row(&s->u, s->row_count, s->row_count + 1, 1.0f);
        grid_copy_row(&s->v, s->row_count, s->row_count + 1, -1.0f);
    } else {
        // Enviar mi última fila al vecino de abajo
        mpi_async_send_row(grid_get_row(&s->h, s->row_count), 
                          grid_row_size(&s->h), g_mpi.pid + 1);
        mpi_async_send_row(grid_get_row(&s->u, s->row_count), 
                          grid_row_size(&s->u), g_mpi.pid + 1);
        mpi_async_send_row(grid_get_row(&s->v, s->row_count), 
                          grid_row_size(&s->v), g_mpi.pid + 1);
        
        // Recibir primera fila del vecino de abajo (para mi fila fantasma inferior)
        mpi_sync_recv_row(grid_get_row(&s->h, s->row_count + 1), 
                         grid_row_size(&s->h), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->u, s->row_count + 1), 
                         grid_row_size(&s->u), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->v, s->row_count + 1), 
                         grid_row_size(&s->v), g_mpi.pid + 1);
    }
    
    // Recibir última fila del vecino de arriba (para mi fila fantasma superior)
    if (s->initial_row != 0) {
        mpi_sync_recv_row(grid_get_row(&s->h, 0), grid_row_size(&s->h), g_mpi.pid - 1);
        mpi_sync_recv_row(grid_get_row(&s->u, 0), grid_row_size(&s->u), g_mpi.pid - 1);
        mpi_sync_recv_row(grid_get_row(&s->v, 0), grid_row_size(&s->v), g_mpi.pid - 1);
    }
    
    // ===== BORDES LATERALES (columnas) =====
    // Estos son iguales para todos los procesos
    
    // Altura (h)
    grid_copy_col(&s->h, 1, 0, 1.0f);
    grid_copy_col(&s->h, g_config.grid_size, g_config.grid_size + 1, 1.0f);
    
    // Momento X (u) - se invierte en bordes verticales
    grid_copy_col(&s->u, 1, 0, -1.0f);
    grid_copy_col(&s->u, g_config.grid_size, g_config.grid_size + 1, -1.0f);
    
    // Momento Y (v)
    grid_copy_col(&s->v, 1, 0, 1.0f);
    grid_copy_col(&s->v, g_config.grid_size, g_config.grid_size + 1, 1.0f);
}

void simulation_sync_momentums(SimulationState* s) {
    // Sincronizar derivadas de momentos (después de calcular)
    
    // Si no soy el primero, enviar mi fila 0 hacia arriba
    if (s->initial_row != 0) {
        mpi_async_send_row(grid_get_row(&s->hx, 0), grid_row_size(&s->hx), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->hy, 0), grid_row_size(&s->hy), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->ux, 0), grid_row_size(&s->ux), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->uy, 0), grid_row_size(&s->uy), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->vx, 0), grid_row_size(&s->vx), g_mpi.pid - 1);
        mpi_async_send_row(grid_get_row(&s->vy, 0), grid_row_size(&s->vy), g_mpi.pid - 1);
    }
    
    // Si no soy el último, recibir fila m_row_count del vecino de abajo
    if (s->initial_row + s->row_count != g_config.grid_size) {
        mpi_sync_recv_row(grid_get_row(&s->hx, s->row_count), grid_row_size(&s->hx), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->hy, s->row_count), grid_row_size(&s->hy), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->ux, s->row_count), grid_row_size(&s->ux), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->uy, s->row_count), grid_row_size(&s->uy), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->vx, s->row_count), grid_row_size(&s->vx), g_mpi.pid + 1);
        mpi_sync_recv_row(grid_get_row(&s->vy, s->row_count), grid_row_size(&s->vy), g_mpi.pid + 1);
    }
}

/* ============================================
 * CALLBACKS PARA EL ALGORITMO
 * ============================================ */

void callback_update_x(uint32_t i, uint32_t j, void* data) {
    SimulationState* s = (SimulationState*)data;
    
    float h1 = grid_get(&s->h, i + 1, j + 1);
    float h2 = grid_get(&s->h, i, j + 1);
    float u1 = grid_get(&s->u, i + 1, j + 1);
    float u2 = grid_get(&s->u, i, j + 1);
    float v1 = grid_get(&s->v, i + 1, j + 1);
    float v2 = grid_get(&s->v, i, j + 1);
    
    // Altura
    float hx_ = (h1 + h2) * 0.5f - g_config.d_t / (2.0f * g_config.d_x) * (u1 - u2);
    
    // Momento X
    float ux_ = (u1 + u2) * 0.5f -
                g_config.d_t / (2.0f * g_config.d_x) *
                    ((u1 * u1 / h1 + 0.5f * g_config.gravity * h1 * h1) -
                     (u2 * u2 / h2 + 0.5f * g_config.gravity * h2 * h2));
    
    // Momento Y
    float vx_ = (v1 + v2) * 0.5f -
                g_config.d_t / (2.0f * g_config.d_x) * ((u1 * v1 / h1) - (u2 * v2 / h2));
    
    grid_set(&s->hx, i, j, hx_);
    grid_set(&s->ux, i, j, ux_);
    grid_set(&s->vx, i, j, vx_);
}

void callback_update_y(uint32_t i, uint32_t j, void* data) {
    SimulationState* s = (SimulationState*)data;
    
    float h1 = grid_get(&s->h, i + 1, j + 1);
    float h2 = grid_get(&s->h, i + 1, j);
    float v1 = grid_get(&s->v, i + 1, j + 1);
    float v2 = grid_get(&s->v, i + 1, j);
    float u1 = grid_get(&s->u, i + 1, j + 1);
    float u2 = grid_get(&s->u, i + 1, j);
    
    // Altura
    float hy_ = (h1 + h2) * 0.5f - g_config.d_t / (2.0f * g_config.d_y) * (v1 - v2);
    
    // Momento X
    float uy_ = (u1 + u2) * 0.5f -
                g_config.d_t / (2.0f * g_config.d_y) * ((v1 * u1 / h1) - (v2 * u2 / h2));
    
    // Momento Y
    float vy_ = (v1 + v2) * 0.5f -
                g_config.d_t / (2.0f * g_config.d_y) *
                    ((v1 * v1 / h1 + 0.5f * g_config.gravity * h1 * h1) -
                     (v2 * v2 / h2 + 0.5f * g_config.gravity * h2 * h2));
    
    grid_set(&s->hy, i, j, hy_);
    grid_set(&s->uy, i, j, uy_);
    grid_set(&s->vy, i, j, vy_);
}

void callback_average(uint32_t i, uint32_t j, void* data) {
    SimulationState* s = (SimulationState*)data;
    
    #define SQ(x) ((x) * (x))
    
    // Altura
    float h_ = grid_get(&s->h, i, j) -
               (g_config.d_t / g_config.d_x) * 
                   (grid_get(&s->ux, i, j - 1) - grid_get(&s->ux, i - 1, j - 1)) -
               (g_config.d_t / g_config.d_y) * 
                   (grid_get(&s->vy, i - 1, j) - grid_get(&s->vy, i - 1, j - 1));
    
    // Momento X
    float ux_i_j1 = grid_get(&s->ux, i, j - 1);
    float hx_i_j1 = grid_get(&s->hx, i, j - 1);
    float ux_i1_j1 = grid_get(&s->ux, i - 1, j - 1);
    float hx_i1_j1 = grid_get(&s->hx, i - 1, j - 1);
    float vy_i1_j = grid_get(&s->vy, i - 1, j);
    float uy_i1_j = grid_get(&s->uy, i - 1, j);
    float hy_i1_j = grid_get(&s->hy, i - 1, j);
    float vy_i1_j1 = grid_get(&s->vy, i - 1, j - 1);
    float uy_i1_j1 = grid_get(&s->uy, i - 1, j - 1);
    float hy_i1_j1 = grid_get(&s->hy, i - 1, j - 1);
    
    float u_ = grid_get(&s->u, i, j) -
               (g_config.d_t / g_config.d_x) *
                   ((SQ(ux_i_j1) / hx_i_j1 + 0.5f * g_config.gravity * SQ(hx_i_j1)) -
                    (SQ(ux_i1_j1) / hx_i1_j1 + 0.5f * g_config.gravity * SQ(hx_i1_j1))) -
               (g_config.d_t / g_config.d_y) * 
                   ((vy_i1_j * uy_i1_j / hy_i1_j) -
                    (vy_i1_j1 * uy_i1_j1 / hy_i1_j1));
    
    // Momento Y
    float vx_i_j1 = grid_get(&s->vx, i, j - 1);
    float vx_i1_j1 = grid_get(&s->vx, i - 1, j - 1);
    
    float v_ = grid_get(&s->v, i, j) -
               (g_config.d_t / g_config.d_x) * 
                   ((ux_i_j1 * vx_i_j1 / hx_i_j1) -
                    (ux_i1_j1 * vx_i1_j1 / hx_i1_j1)) -
               (g_config.d_t / g_config.d_y) *
                   ((SQ(vy_i1_j) / hy_i1_j + 0.5f * g_config.gravity * SQ(hy_i1_j)) -
                    (SQ(vy_i1_j1) / hy_i1_j1 + 0.5f * g_config.gravity * SQ(hy_i1_j1)));
    
    grid_set(&s->h, i, j, h_);
    grid_set(&s->u, i, j, u_);
    grid_set(&s->v, i, j, v_);
    
    #undef SQ
}

/* ============================================
 * FUNCIONES DEL ALGORITMO
 * ============================================ */

void simulation_update_in_x_direction(SimulationState* s) {
    // Procesar solo mis filas locales
    for (uint32_t i = 0; i < g_config.grid_size + 1; i++) {
        for (uint32_t j = 0; j < s->row_count; j++) {
            callback_update_x(i, j, s);
        }
    }
}

void simulation_update_in_y_direction(SimulationState* s) {
    // Procesar solo mis filas locales
    for (uint32_t i = 0; i < g_config.grid_size; i++) {
        for (uint32_t j = 0; j < s->row_count + 1; j++) {
            callback_update_y(i, j, s);
        }
    }
}

void simulation_average_momentums(SimulationState* s) {
    // Procesar solo mis filas locales
    for (uint32_t i = 1; i < g_config.grid_size + 1; i++) {
        for (uint32_t j = 1; j < s->row_count + 1; j++) {
            callback_average(i, j, s);
        }
    }
}

/* ============================================
 * VISUALIZACIÓN
 * ============================================ */

void simulation_maybe_plot(const SimulationState* s) {
    if (s->step % g_config.dump_every != 0) {
        return;
    }
    
    // Cada proceso imprime su parte en orden
    // (sincronización simple con MPI_Send/Recv de un byte)
    
    if (s->initial_row == 0) {
        // El primer proceso imprime primero
        if (mpi_is_coordinator()) {
            printf("\n========== PASO %u ==========\n", s->step);
            printf("h (proceso %d, filas %u-%u):\n", 
                   g_mpi.pid, s->initial_row, s->initial_row + s->row_count - 1);
        }
        
        // Imprimir mi grid (solo centro para no saturar)
        uint32_t center_y = s->row_count / 2;
        uint32_t center_x = g_config.grid_size / 2;
        
        for (uint32_t j = (center_y > 2 ? center_y - 2 : 0); 
             j <= (center_y + 2 < s->row_count ? center_y + 2 : s->row_count - 1); 
             j++) {
            for (uint32_t i = (center_x > 2 ? center_x - 2 : 0); 
                 i <= (center_x + 2 < g_config.grid_size ? center_x + 2 : g_config.grid_size - 1); 
                 i++) {
                printf("%.3f ", grid_get(&s->h, i + 1, j + 1));
            }
            printf("\n");
        }
    } else {
        // Esperar señal del proceso anterior
        char signal;
        MPI_CHECK(MPI_Recv(&signal, 1, MPI_CHAR, g_mpi.pid - 1, 0, 
                          MPI_COMM_WORLD, MPI_STATUS_IGNORE));
        
        // Imprimir mi parte (solo si no es muy grande)
        if (g_config.grid_size <= 64) {
            printf("h (proceso %d, filas %u-%u):\n", 
                   g_mpi.pid, s->initial_row, s->initial_row + s->row_count - 1);
            
            // Imprimir solo algunas filas para no saturar
            uint32_t rows_to_print = (s->row_count < 3) ? s->row_count : 3;
            for (uint32_t j = 0; j < rows_to_print; j++) {
                for (uint32_t i = 0; i < g_config.grid_size && i < 10; i++) {
                    printf("%.3f ", grid_get(&s->h, i + 1, j + 1));
                }
                printf("...\n");
            }
        }
    }
    
    // Señalar al siguiente proceso
    if (s->initial_row + s->row_count != g_config.grid_size) {
        char signal = 0;
        MPI_CHECK(MPI_Send(&signal, 1, MPI_CHAR, g_mpi.pid + 1, 0, MPI_COMM_WORLD));
    }
}

/* ============================================
 * PASO DE SIMULACIÓN
 * ============================================ */

bool simulation_tick(SimulationState* s) {
    simulation_maybe_spawn_drop(s);
    s->step++;
    simulation_reflect_boundaries_and_sync(s);
    simulation_maybe_plot(s);
    simulation_update_in_x_direction(s);
    simulation_update_in_y_direction(s);
    simulation_sync_momentums(s);
    simulation_average_momentums(s);
    
    return s->step != g_config.step_count;
}

/* ============================================
 * MAIN
 * ============================================ */

int main(int argc, char** argv) {
    // Inicializar MPI
    mpi_state_init(&argc, &argv);
    
    // Inicializar semilla aleatoria (diferente por proceso)
    srand((unsigned int)(time(NULL) + g_mpi.pid));
    
    // Cargar configuración
    config_set_defaults(&g_config);
    const char* config_file = (argc > 1) ? argv[1] : "config_mpi.txt";
    config_load(config_file, &g_config);
    config_print(&g_config);
    
    // Verificar que el grid se puede dividir
    if (g_config.grid_size < (uint32_t)g_mpi.proc_count) {
        if (mpi_is_coordinator()) {
            fprintf(stderr, "Error: Grid demasiado pequeño (%u) para %d procesos\n", 
                    g_config.grid_size, g_mpi.proc_count);
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    // Crear simulación
    SimulationState* state = simulation_create();
    
    // Timer
    double start_time = MPI_Wtime();
    
    // Bucle principal
    if (mpi_is_coordinator()) {
        printf("Iniciando simulación con %d procesos MPI...\n", g_mpi.proc_count);
    }
    
    while (simulation_tick(state)) {
        // Progreso (solo coordinador)
        if (mpi_is_coordinator() && state->step % 100 == 0) {
            uint32_t percent = (state->step * 100) / g_config.step_count;
            printf("Progreso: %u%%\n", percent);
            fflush(stdout);
        }
    }
    
    // Barrera para sincronizar todos antes de medir tiempo
    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();
    double elapsed = end_time - start_time;
    
    // Imprimir resultados (solo coordinador)
    if (mpi_is_coordinator()) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════╗\n");
        printf("║           SIMULACIÓN COMPLETADA (MPI)             ║\n");
        printf("╚═══════════════════════════════════════════════════╝\n");
        printf("\n");
        printf("===========================================\n");
        printf("  CONFIGURACIÓN\n");
        printf("===========================================\n");
        printf("  Grid global:          %u x %u\n", g_config.grid_size, g_config.grid_size);
        printf("  Procesos MPI:         %d\n", g_mpi.proc_count);
        printf("  Steps totales:        %u\n", state->step);
        printf("  Gotas generadas:      %u / %u\n", state->drop_count, g_config.max_drops);
        printf("===========================================\n");
        printf("  RENDIMIENTO\n");
        printf("===========================================\n");
        printf("  Tiempo transcurrido:  %.3f segundos\n", elapsed);
        printf("  Formato mm:ss.ss:     %d:%05.2f\n", 
               (int)(elapsed / 60), fmod(elapsed, 60));
        printf("  Steps por segundo:    %.0f\n", state->step / elapsed);
        printf("===========================================\n");
        printf("\n");
    }
    
    // Limpiar
    simulation_destroy(state);
    
    // Finalizar MPI
    mpi_state_finalize();
    
    return 0;
}




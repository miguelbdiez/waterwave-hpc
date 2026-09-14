/*
 * waterwave.c
 * Implementación principal de la simulación
 */

#include "waterwave.h"
#include <omp.h>
/* ============================================
 * VARIABLE GLOBAL: CONFIGURACIÓN
 * ============================================ */

Config g_config;

/* ============================================
 * ESTRUCTURA PARA PASAR DATOS AL WORKER
 * ============================================ */



/* ============================================
 * IMPLEMENTACIÓN: FUNCIONES DE CONFIGURACIÓN
 * ============================================ */

void config_set_defaults(Config* config) {
    config->grid_size = 64;
    config->max_drops = 5;
    config->drop_step = 500;
    config->step_count = 1000;
    config->dump_every = 100;
    config->threads = 1;
    config->gravity = 9.8f;
    config->d_t = 0.02f;
    config->d_x = 1.0f;
    config->d_y = 1.0f;
}

bool config_load(const char* filename, Config* config) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Advertencia: No se pudo abrir '%s', usando valores por defecto\n", filename);
        config_set_defaults(config);
        return false;
    }
    
    // Primero cargar defaults
    config_set_defaults(config);
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // ... código existente del parseo ...
    }
    
    fclose(file);
    
    // ===== AÑADIR ESTE BLOQUE COMPLETO AQUÍ =====
    // Sobrescribir con variables de entorno (tienen prioridad máxima)
    char* env;
    if ((env = getenv("GRID_SIZE")) != NULL) {
        config->grid_size = (size_t)atoi(env);
    }
    if ((env = getenv("THREADS")) != NULL) {
        config->threads = (size_t)atoi(env);
    }
    if ((env = getenv("STEP_COUNT")) != NULL) {
        config->step_count = (size_t)atoi(env);
    }
    if ((env = getenv("DUMP_EVERY")) != NULL) {
        config->dump_every = (size_t)atoi(env);
    }
    // ============================================
    
    return true;
}
/*
bool config_load(const char* filename, Config* config) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Advertencia: No se pudo abrir '%s', usando valores por defecto\n", filename);
        //config_set_defaults(config);
        return false;
    }
    
    // Primero cargar defaults
  //  config_set_defaults(config);
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Ignorar comentarios y líneas vacías
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        
        // Parsear línea (formato: CLAVE=VALOR)
        char key[64];
        char value[64];
        if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
            // Comparar clave y asignar valor
            if (strcmp(key, "GRID_SIZE") == 0) {
                config->grid_size = (size_t)atoi(value);
            } else if (strcmp(key, "MAX_DROPS") == 0) {
                config->max_drops = (size_t)atoi(value);
            } else if (strcmp(key, "DROP_STEP") == 0) {
                config->drop_step = (size_t)atoi(value);
            } else if (strcmp(key, "STEP_COUNT") == 0) {
                config->step_count = (size_t)atoi(value);
            } else if (strcmp(key, "DUMP_EVERY") == 0) {
                config->dump_every = (size_t)atoi(value);
            } else if (strcmp(key, "THREADS") == 0) {
                config->threads = (size_t)atoi(value);
            } else if (strcmp(key, "GRAVITY") == 0) {
                config->gravity = (float)atof(value);
            } else if (strcmp(key, "D_T") == 0) {
                config->d_t = (float)atof(value);
            } else if (strcmp(key, "D_X") == 0) {
                config->d_x = (float)atof(value);
            } else if (strcmp(key, "D_Y") == 0) {
                config->d_y = (float)atof(value);
            }
        }
    }
    
    fclose(file);
    return true;
}*/
void config_print(const Config* config) {
    printf("===========================================\n");
    printf("  CONFIGURACIÓN DE LA SIMULACIÓN\n");
    printf("===========================================\n");
    printf("  Tamaño del grid:      %zu x %zu\n", config->grid_size, config->grid_size);
    printf("  Máximo de gotas:      %zu\n", config->max_drops);
    printf("  Paso entre gotas:     %zu\n", config->drop_step);
    printf("  Pasos totales:        %zu\n", config->step_count);
    printf("  Salida cada:          %zu pasos\n", config->dump_every);
    printf("  Hilos:                %zu\n", config->threads);
    printf("  Gravedad:             %.2f m/s²\n", config->gravity);
    printf("  Delta tiempo (dt):    %.4f s\n", config->d_t);
    printf("  Delta espacio (dx):   %.2f m\n", config->d_x);
    printf("  Delta espacio (dy):   %.2f m\n", config->d_y);
    printf("===========================================\n\n");
}

/* ============================================
 * IMPLEMENTACIÓN: FUNCIONES DE GRID
 * ============================================ */

Grid grid_create(size_t size, float initial_value) {
    Grid g;
    g.size = size;
    g.cells = (float*)malloc(size * size * sizeof(float));
    
    if (!g.cells) {
        fprintf(stderr, "Error: No se pudo asignar memoria para grid de %zu x %zu\n", size, size);
        exit(EXIT_FAILURE);
    }
    
    // Inicializar todas las celdas con el valor inicial
    for (size_t i = 0; i < size * size; i++) {
        g.cells[i] = initial_value;
    }
    
    return g;
}

void grid_destroy(Grid* g) {
    if (g && g->cells) {
        free(g->cells);
        g->cells = NULL;
        g->size = 0;
    }
}

float grid_get(const Grid* g, size_t x, size_t y) {
    assert(g != NULL);
    assert(g->cells != NULL);
    assert(x < g->size && y < g->size);
    return g->cells[index_for(x, y, g->size)];
}

void grid_set(Grid* g, size_t x, size_t y, float value) {
    assert(g != NULL);
    assert(g->cells != NULL);
    assert(x < g->size && y < g->size);
    assert(!isnan(value));
    assert(isfinite(value));
    g->cells[index_for(x, y, g->size)] = value;
}

void grid_copy_col(Grid* g, size_t from_x, size_t to_x, float factor) {
    for (size_t y = 0; y < g->size; y++) {
        float src = grid_get(g, from_x, y);
        grid_set(g, to_x, y, src * factor);
    }
}

void grid_copy_row(Grid* g, size_t from_y, size_t to_y, float factor) {
    for (size_t x = 0; x < g->size; x++) {
        float src = grid_get(g, x, from_y);
        grid_set(g, x, to_y, src * factor);
    }
}

void grid_print(const Grid* g, const char* name) {
    printf("%s:\n", name);
    for (size_t j = 0; j < g->size; j++) {
        for (size_t i = 0; i < g->size; i++) {
            printf("\t%.2f", grid_get(g, i, j));
        }
        printf("\n");
    }
    printf("\n");
}

/* ============================================
 * IMPLEMENTACIÓN: FUNCIONES DE SIMULATION
 * ============================================ */

SimulationState* simulation_create() {
    SimulationState* s = (SimulationState*)malloc(sizeof(SimulationState));
    if (!s) {
        fprintf(stderr, "Error: No se pudo asignar memoria para SimulationState\n");
        exit(EXIT_FAILURE);
    }
    
    // Inicializar variables
    s->step = 0;
    s->drop_count = 0;
    s->pending_drop_positions = NULL;
    s->pending_positions_size = 0;
    
    // Crear grids (tamaño + 2 para bordes fantasma)
    size_t main_size = g_config.grid_size + 2;
    size_t deriv_size = g_config.grid_size + 1;
    
    printf("Creando grids principales (%zu x %zu)...\n", main_size, main_size);
    s->h = grid_create(main_size, 1.0f);  // Altura inicial: 1.0
    s->u = grid_create(main_size, 0.0f);  // Momento X inicial: 0.0
    s->v = grid_create(main_size, 0.0f);  // Momento Y inicial: 0.0
    
    printf("Creando grids de derivadas (%zu x %zu)...\n", deriv_size, deriv_size);
    s->hx = grid_create(deriv_size, 0.0f);
    s->hy = grid_create(deriv_size, 0.0f);
    s->ux = grid_create(deriv_size, 0.0f);
    s->uy = grid_create(deriv_size, 0.0f);
    s->vx = grid_create(deriv_size, 0.0f);
    s->vy = grid_create(deriv_size, 0.0f);
    
// Configurar número de hilos OpenMP
if (g_config.threads > 0) {
    omp_set_num_threads(g_config.threads);
    printf("Configurando OpenMP con %zu hilos...\n", g_config.threads);
}
    printf("Simulación creada exitosamente.\n\n");
    
    return s;
}

void simulation_destroy(SimulationState* s) {
    if (!s) return;
    
    printf("Destruyendo simulación...\n");
    
    // Destruir grids
    grid_destroy(&s->h);
    grid_destroy(&s->u);
    grid_destroy(&s->v);
    grid_destroy(&s->hx);
    grid_destroy(&s->hy);
    grid_destroy(&s->ux);
    grid_destroy(&s->uy);
    grid_destroy(&s->vx);
    grid_destroy(&s->vy);
    

    
    // Liberar posiciones predefinidas
    if (s->pending_drop_positions) {
        free(s->pending_drop_positions);
    }
    
    free(s);
    printf("Simulación destruida.\n");
}

size_t simulation_next_drop_coordinate(SimulationState* s) {
    // Si hay posiciones predefinidas, usar esas
    if (s->pending_positions_size > 0) {
        s->pending_positions_size--;
        return s->pending_drop_positions[s->pending_positions_size];
    }
    
    // Si no, generar coordenada aleatoria
    return rand() % g_config.grid_size;
}

void simulation_maybe_spawn_drop(SimulationState* s) {

     if (s->step % g_config.drop_step != 0 && s->step != 0)
        return;
    // Solo generar gota cada DROP_STEP pasos
    if (s->step % g_config.drop_step != 0)
        return;
    
    // Solo generar hasta MAX_DROPS
    if (s->drop_count >= g_config.max_drops)
        return;
    
    s->drop_count++;
    
    // Obtener coordenadas aleatorias para el centro de la gota
    // size_t x = simulation_next_drop_coordinate(s);
    // size_t y = simulation_next_drop_coordinate(s);

    size_t x = g_config.grid_size / 2;  // Centro X
    size_t y = g_config.grid_size / 2;  // Centro Y
    
    // Parámetros de la gota
    const size_t width = 21;      // Radio de influencia
    const float height = 1.5f;    // Altura máxima de la perturbación
    
    // Calcular área afectada (asegurando que no salga del grid)
    size_t i_min = (x > width) ? (x - width) : 0;
    size_t j_min = (y > width) ? (y - width) : 0;
    size_t i_max = (x + width < g_config.grid_size - 1) ? (x + width) : (g_config.grid_size - 1);
    size_t j_max = (y + width < g_config.grid_size - 1) ? (y + width) : (g_config.grid_size - 1);
    
    // Aplicar distribución gaussiana
    for (size_t i = i_min; i <= i_max; i++) {
        for (size_t j = j_min; j <= j_max; j++) {
            // Distancia normalizada al centro
            float dx = ((float)i - (float)x) / (float)width;
            float dy = ((float)j - (float)y) / (float)width;
            
            // Factor gaussiano: exp(-5 * (dx² + dy²))
            float factor = expf(-5.0f * (dx * dx + dy * dy));
            
            // Añadir perturbación a la altura
            float current_h = grid_get(&s->h, i, j);
            grid_set(&s->h, i, j, current_h + height * factor);
        }
    }
}

void simulation_reflect_boundaries(SimulationState* s) {
    // Altura (h) - reflejar en todos los bordes con factor 1.0
    grid_copy_row(&s->h, 1, 0, 1.0f);
    grid_copy_row(&s->h, g_config.grid_size, g_config.grid_size + 1, 1.0f);
    grid_copy_col(&s->h, 1, 0, 1.0f);
    grid_copy_col(&s->h, g_config.grid_size, g_config.grid_size + 1, 1.0f);
    
    // Momento X (u) - reflejar con signo negativo en bordes verticales
    grid_copy_row(&s->u, 1, 0, 1.0f);
    grid_copy_row(&s->u, g_config.grid_size, g_config.grid_size + 1, 1.0f);
    grid_copy_col(&s->u, 1, 0, -1.0f);  // Signo negativo
    grid_copy_col(&s->u, g_config.grid_size, g_config.grid_size + 1, -1.0f);
    
    // Momento Y (v) - reflejar con signo negativo en bordes horizontales
    grid_copy_row(&s->v, 1, 0, -1.0f);  // Signo negativo
    grid_copy_row(&s->v, g_config.grid_size, g_config.grid_size + 1, -1.0f);
    grid_copy_col(&s->v, 1, 0, 1.0f);
    grid_copy_col(&s->v, g_config.grid_size, g_config.grid_size + 1, 1.0f);
}




void simulation_update_in_x_direction(SimulationState* s) {
    #pragma omp parallel for collapse(2)
    for (size_t i = 0; i < g_config.grid_size + 1; i++) {
        for (size_t j = 0; j < g_config.grid_size; j++) {
            // Inline del cálculo
            float h1 = grid_get(&s->h, i + 1, j + 1);
            float h2 = grid_get(&s->h, i, j + 1);
            float u1 = grid_get(&s->u, i + 1, j + 1);
            float u2 = grid_get(&s->u, i, j + 1);
            float v1 = grid_get(&s->v, i + 1, j + 1);
            float v2 = grid_get(&s->v, i, j + 1);
            
            float hx_ = (h1 + h2) * 0.5f - g_config.d_t / (2.0f * g_config.d_x) * (u1 - u2);
            
            float ux_ = (u1 + u2) * 0.5f -
                        g_config.d_t / (2.0f * g_config.d_x) *
                        ((u1 * u1 / h1 + 0.5f * g_config.gravity * h1 * h1) -
                         (u2 * u2 / h2 + 0.5f * g_config.gravity * h2 * h2));
            
            float vx_ = (v1 + v2) * 0.5f -
                        g_config.d_t / (2.0f * g_config.d_x) *
                        ((u1 * v1 / h1) - (u2 * v2 / h2));
            
            grid_set(&s->hx, i, j, hx_);
            grid_set(&s->ux, i, j, ux_);
            grid_set(&s->vx, i, j, vx_);
        }
    }
}

void simulation_update_in_y_direction(SimulationState* s) {
    #pragma omp parallel for collapse(2)
    for (size_t i = 0; i < g_config.grid_size; i++) {
        for (size_t j = 0; j < g_config.grid_size + 1; j++) {
            float h1 = grid_get(&s->h, i + 1, j + 1);
            float h2 = grid_get(&s->h, i + 1, j);
            float v1 = grid_get(&s->v, i + 1, j + 1);
            float v2 = grid_get(&s->v, i + 1, j);
            float u1 = grid_get(&s->u, i + 1, j + 1);
            float u2 = grid_get(&s->u, i + 1, j);
            
            float hy_ = (h1 + h2) * 0.5f - g_config.d_t / (2.0f * g_config.d_y) * (v1 - v2);
            
            float uy_ = (u1 + u2) * 0.5f -
                        g_config.d_t / (2.0f * g_config.d_y) *
                        ((v1 * u1 / h1) - (v2 * u2 / h2));
            
            float vy_ = (v1 + v2) * 0.5f -
                        g_config.d_t / (2.0f * g_config.d_y) *
                        ((v1 * v1 / h1 + 0.5f * g_config.gravity * h1 * h1) -
                         (v2 * v2 / h2 + 0.5f * g_config.gravity * h2 * h2));
            
            grid_set(&s->hy, i, j, hy_);
            grid_set(&s->uy, i, j, uy_);
            grid_set(&s->vy, i, j, vy_);
        }
    }
}
void simulation_average_momentums(SimulationState* s) {
    #pragma omp parallel for collapse(2)
    for (size_t i = 1; i < g_config.grid_size + 1; i++) {
        for (size_t j = 1; j < g_config.grid_size + 1; j++) {
            // height
            float h_ = grid_get(&s->h, i, j) -
                      (g_config.d_t / g_config.d_x) * 
                      (grid_get(&s->ux, i, j-1) - grid_get(&s->ux, i-1, j-1)) -
                      (g_config.d_t / g_config.d_y) * 
                      (grid_get(&s->vy, i-1, j) - grid_get(&s->vy, i-1, j-1));
            
            // x momentum  
            float ux_i_jm1 = grid_get(&s->ux, i, j-1);
            float ux_im1_jm1 = grid_get(&s->ux, i-1, j-1);
            float hx_i_jm1 = grid_get(&s->hx, i, j-1);
            float hx_im1_jm1 = grid_get(&s->hx, i-1, j-1);
            
            float u_ = grid_get(&s->u, i, j) -
                      (g_config.d_t / g_config.d_x) *
                      ((ux_i_jm1 * ux_i_jm1 / hx_i_jm1 + 
                        0.5f * g_config.gravity * hx_i_jm1 * hx_i_jm1) -
                       (ux_im1_jm1 * ux_im1_jm1 / hx_im1_jm1 + 
                        0.5f * g_config.gravity * hx_im1_jm1 * hx_im1_jm1)) -
                      (g_config.d_t / g_config.d_y) *
                      ((grid_get(&s->vy, i-1, j) * grid_get(&s->uy, i-1, j) / grid_get(&s->hy, i-1, j)) -
                       (grid_get(&s->vy, i-1, j-1) * grid_get(&s->uy, i-1, j-1) / grid_get(&s->hy, i-1, j-1)));
            
            // y momentum
            float vy_im1_j = grid_get(&s->vy, i-1, j);
            float vy_im1_jm1 = grid_get(&s->vy, i-1, j-1);
            float hy_im1_j = grid_get(&s->hy, i-1, j);
            float hy_im1_jm1 = grid_get(&s->hy, i-1, j-1);
            
            float v_ = grid_get(&s->v, i, j) -
                      (g_config.d_t / g_config.d_x) *
                      ((grid_get(&s->ux, i, j-1) * grid_get(&s->vx, i, j-1) / grid_get(&s->hx, i, j-1)) -
                       (grid_get(&s->ux, i-1, j-1) * grid_get(&s->vx, i-1, j-1) / grid_get(&s->hx, i-1, j-1))) -
                      (g_config.d_t / g_config.d_y) *
                      ((vy_im1_j * vy_im1_j / hy_im1_j + 
                        0.5f * g_config.gravity * hy_im1_j * hy_im1_j) -
                       (vy_im1_jm1 * vy_im1_jm1 / hy_im1_jm1 + 
                        0.5f * g_config.gravity * hy_im1_jm1 * hy_im1_jm1));
            
            grid_set(&s->h, i, j, h_);
            grid_set(&s->u, i, j, u_);
            grid_set(&s->v, i, j, v_);
        }
    }
}

void simulation_maybe_plot(const SimulationState* s) {
    // Solo imprimir cada DUMP_EVERY pasos
    if (s->step % g_config.dump_every != 0)
        return;
    
    printf("\n========== PASO %zu ==========\n", s->step);
    
    // Imprimir solo un resumen (imprimir todo sería demasiado)
    printf("Altura (h) - Centro del grid:\n");
    size_t center = g_config.grid_size / 2;
    for (size_t i = center - 2; i <= center + 2; i++) {
        for (size_t j = center - 2; j <= center + 2; j++) {
            printf("%.3f ", grid_get(&s->h, i, j));
        }
        printf("\n");
    }
    printf("\n");
}

bool simulation_tick(SimulationState* s) {
    simulation_maybe_spawn_drop(s);
    s->step++;
    simulation_reflect_boundaries(s);
    simulation_maybe_plot(s);
    simulation_update_in_x_direction(s);
    simulation_update_in_y_direction(s);
    simulation_average_momentums(s);
    
    return s->step < g_config.step_count;
}


/* ============================================
 * FUNCIÓN MAIN
 * ============================================ */

int main(int argc, char* argv[]) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║   SIMULACIÓN DE ONDAS DE AGUA                    ║\n");
    printf("║   Método de Diferencias Finitas                  ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // 1. Cargar configuración
    const char* config_file = "config.txt";
    if (argc > 1) {
        config_file = argv[1];
    }
    
    printf("Cargando configuración desde '%s'...\n", config_file);
    config_load(config_file, &g_config);
    config_print(&g_config);
    
    // 2. Inicializar semilla aleatoria
    srand((unsigned int)time(NULL));
    
    // 3. Crear simulación
    SimulationState* state = simulation_create();
    
    // ===== INICIAR TIMER =====
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // 4. Ejecutar simulación
    printf("Iniciando simulación...\n");
    printf("Progreso: ");
    fflush(stdout);
    
    size_t last_percent = 0;
    while (simulation_tick(state)) {
        // Mostrar progreso
        size_t percent = (state->step * 100) / g_config.step_count;
        if (percent != last_percent && percent % 10 == 0) {
            printf("%zu%% ", percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
    
    // ===== FINALIZAR TIMER =====
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    // Calcular tiempo transcurrido
    double elapsed_seconds = (end_time.tv_sec - start_time.tv_sec) +
                            (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    
    printf("100%%\n\n");
    
    // ===== RESUMEN DETALLADO =====
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║           SIMULACIÓN COMPLETADA                   ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("===========================================\n");
    printf("  CONFIGURACIÓN\n");
    printf("===========================================\n");
    printf("  Grid:                 %zu x %zu\n", g_config.grid_size, g_config.grid_size);
    printf("  Hilos:                %zu\n", g_config.threads);
    printf("  Steps totales:        %zu\n", state->step);
    printf("  Gotas generadas:      %zu / %zu\n", state->drop_count, g_config.max_drops);
    printf("  Drop cada:            %zu steps\n", g_config.drop_step);
    printf("===========================================\n");
    printf("  RENDIMIENTO\n");
    printf("===========================================\n");
    printf("  Tiempo transcurrido:  %.3f segundos\n", elapsed_seconds);
    printf("  Formato mm:ss.ss:     %d:%05.2f\n", 
           (int)(elapsed_seconds / 60), 
           fmod(elapsed_seconds, 60));
    printf("  Steps por segundo:    %.0f\n", state->step / elapsed_seconds);
    printf("===========================================\n");
    
    // 5. Limpiar y salir
    simulation_destroy(state);
    
    printf("\n");
    
    return EXIT_SUCCESS;
}
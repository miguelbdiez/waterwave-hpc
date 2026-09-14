/*
 * threadpool.c
 * Implementación del sistema de pool de hilos
 */

#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ============================================
 * MACROS DE AYUDA
 * ============================================ */

// Macro para verificar errores de pthread
#define PTHREAD_CHECK(call) \
    do { \
        int result = (call); \
        if (result != 0) { \
            fprintf(stderr, "Error pthread en %s:%d - código %d\n", \
                    __FILE__, __LINE__, result); \
        } \
    } while(0)

/* ============================================
 * FUNCIÓN WORKER (ejecutada por cada hilo)
 * ============================================ */

void* threadpool_worker(void* arg) {
    ThreadPool* tp = (ThreadPool*)arg;
    
    while (true) {
        // ===== BLOQUEAR MUTEX Y ESPERAR TRABAJO =====
        PTHREAD_CHECK(pthread_mutex_lock(&tp->work_mutex));
        
        // Esperar mientras no haya trabajo Y no deba salir
        while (tp->queue_size == 0 && !tp->should_exit) {
            // pthread_cond_wait hace:
            // 1. Libera el mutex
            // 2. Duerme el hilo
            // 3. Cuando despierta, vuelve a bloquear el mutex
            pthread_cond_wait(&tp->has_work, &tp->work_mutex);
        }
        
        // Si debe salir, terminar el bucle
        if (tp->should_exit && tp->queue_size == 0) {
            PTHREAD_CHECK(pthread_mutex_unlock(&tp->work_mutex));
            break;
        }
        
        // ===== TOMAR TRABAJO DE LA COLA =====
        // Cola circular: tomar del frente
        WorkItem work = tp->work_queue[tp->queue_front];
        tp->queue_front = (tp->queue_front + 1) % tp->queue_capacity;
        tp->queue_size--;
        
        PTHREAD_CHECK(pthread_mutex_unlock(&tp->work_mutex));
        
        // ===== EJECUTAR TRABAJO (SIN MUTEX) =====
        // Esto es importante: ejecutamos el trabajo FUERA del mutex
        // para que otros hilos puedan tomar más trabajos mientras este ejecuta
        work.func(work.arg);
        
        // ===== DECREMENTAR CONTADOR Y SEÑALIZAR SI TERMINÓ TODO =====
        // atomic_fetch_sub retorna el valor ANTES de restar
        size_t prev_pending = atomic_fetch_sub(&tp->pending_work, 1);
        
        if (prev_pending == 1) {
            // Este era el último trabajo pendiente
            // Señalizar al hilo principal que todo terminó
            PTHREAD_CHECK(pthread_mutex_lock(&tp->done_mutex));
            pthread_cond_signal(&tp->work_done);
            PTHREAD_CHECK(pthread_mutex_unlock(&tp->done_mutex));
        }
    }
    
    return NULL;
}

/* ============================================
 * CREAR THREADPOOL
 * ============================================ */

ThreadPool* threadpool_create(size_t num_threads) {
    if (num_threads == 0) {
        fprintf(stderr, "Error: num_threads debe ser > 0\n");
        return NULL;
    }
    
    // Asignar memoria para la estructura
    ThreadPool* tp = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (!tp) {
        fprintf(stderr, "Error: No se pudo asignar memoria para ThreadPool\n");
        return NULL;
    }
    
    // Inicializar campos básicos
    tp->num_threads = num_threads;
    tp->should_exit = false;
    atomic_store(&tp->pending_work, 0);
    
    // Crear cola de trabajos (capacidad: num_threads * 4)
    tp->queue_capacity = num_threads * 4;
    tp->queue_size = 0;
    tp->queue_front = 0;
    tp->queue_back = 0;
    tp->work_queue = (WorkItem*)malloc(tp->queue_capacity * sizeof(WorkItem));
    if (!tp->work_queue) {
        fprintf(stderr, "Error: No se pudo asignar memoria para work_queue\n");
        free(tp);
        return NULL;
    }
    
    // Inicializar mutex y variables de condición
    PTHREAD_CHECK(pthread_mutex_init(&tp->work_mutex, NULL));
    PTHREAD_CHECK(pthread_cond_init(&tp->has_work, NULL));
    PTHREAD_CHECK(pthread_mutex_init(&tp->done_mutex, NULL));
    PTHREAD_CHECK(pthread_cond_init(&tp->work_done, NULL));
    
    // Crear array de hilos
    tp->threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    if (!tp->threads) {
        fprintf(stderr, "Error: No se pudo asignar memoria para threads\n");
        free(tp->work_queue);
        free(tp);
        return NULL;
    }
    
    // Crear los hilos trabajadores
    for (size_t i = 0; i < num_threads; i++) {
        int result = pthread_create(&tp->threads[i], NULL, threadpool_worker, tp);
        if (result != 0) {
            fprintf(stderr, "Error: No se pudo crear hilo %zu (código %d)\n", i, result);
            // En caso de error, destruir lo que se creó
            tp->should_exit = true;
            pthread_cond_broadcast(&tp->has_work);
            for (size_t j = 0; j < i; j++) {
                pthread_join(tp->threads[j], NULL);
            }
            free(tp->threads);
            free(tp->work_queue);
            pthread_mutex_destroy(&tp->work_mutex);
            pthread_cond_destroy(&tp->has_work);
            pthread_mutex_destroy(&tp->done_mutex);
            pthread_cond_destroy(&tp->work_done);
            free(tp);
            return NULL;
        }
    }
    
    return tp;
}

/* ============================================
 * DESTRUIR THREADPOOL
 * ============================================ */

void threadpool_destroy(ThreadPool* tp) {
    if (!tp) return;
    
    // Indicar a los hilos que deben terminar
    PTHREAD_CHECK(pthread_mutex_lock(&tp->work_mutex));
    tp->should_exit = true;
    PTHREAD_CHECK(pthread_mutex_unlock(&tp->work_mutex));
    
    // Despertar a todos los hilos
    pthread_cond_broadcast(&tp->has_work);
    
    // Esperar a que todos los hilos terminen
    for (size_t i = 0; i < tp->num_threads; i++) {
        pthread_join(tp->threads[i], NULL);
    }
    
    // Liberar recursos
    free(tp->threads);
    free(tp->work_queue);
    
    pthread_mutex_destroy(&tp->work_mutex);
    pthread_cond_destroy(&tp->has_work);
    pthread_mutex_destroy(&tp->done_mutex);
    pthread_cond_destroy(&tp->work_done);
    
    free(tp);
}

/* ============================================
 * AÑADIR TRABAJO A LA COLA
 * ============================================ */

void threadpool_enqueue(ThreadPool* tp, void (*func)(void*), void* arg) {
    if (!tp || !func) return;
    
    PTHREAD_CHECK(pthread_mutex_lock(&tp->work_mutex));
    
    // Verificar que hay espacio en la cola
    if (tp->queue_size >= tp->queue_capacity) {
        fprintf(stderr, "Advertencia: Cola de trabajos llena. Esperando espacio...\n");
        // En una implementación más robusta, aquí se podría:
        // 1. Esperar a que haya espacio
        // 2. Redimensionar la cola dinámicamente
        // 3. Ejecutar el trabajo en el hilo actual
        // Por simplicidad, esperamos un poco y reintentamos
        PTHREAD_CHECK(pthread_mutex_unlock(&tp->work_mutex));
        
        // Esperar a que se procesen algunos trabajos
        threadpool_join(tp);
        
        PTHREAD_CHECK(pthread_mutex_lock(&tp->work_mutex));
    }
    
    // Añadir trabajo a la cola (cola circular: añadir al final)
    tp->work_queue[tp->queue_back].func = func;
    tp->work_queue[tp->queue_back].arg = arg;
    tp->queue_back = (tp->queue_back + 1) % tp->queue_capacity;
    tp->queue_size++;
    
    // Incrementar contador de trabajos pendientes
    atomic_fetch_add(&tp->pending_work, 1);
    
    // Señalizar a UN hilo que hay trabajo disponible
    pthread_cond_signal(&tp->has_work);
    
    PTHREAD_CHECK(pthread_mutex_unlock(&tp->work_mutex));
}

/* ============================================
 * ESPERAR A QUE TODOS LOS TRABAJOS TERMINEN
 * ============================================ */

void threadpool_join(ThreadPool* tp) {
    if (!tp) return;
    
    PTHREAD_CHECK(pthread_mutex_lock(&tp->done_mutex));
    
    // Esperar mientras haya trabajos pendientes
    while (atomic_load(&tp->pending_work) > 0) {
        // pthread_cond_wait libera el mutex y espera la señal
        pthread_cond_wait(&tp->work_done, &tp->done_mutex);
    }
    
    PTHREAD_CHECK(pthread_mutex_unlock(&tp->done_mutex));
}
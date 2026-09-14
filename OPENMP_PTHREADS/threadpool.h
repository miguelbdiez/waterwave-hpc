/*
 * threadpool.h
 * Sistema de pool de hilos para paralelización
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

/* ============================================
 * ESTRUCTURA: WORK ITEM
 * ============================================ */

typedef struct {
    void (*func)(void*);  // Puntero a la función a ejecutar
    void* arg;            // Argumento para la función
} WorkItem;

/* ============================================
 * ESTRUCTURA: THREADPOOL
 * ============================================ */

typedef struct {
    // Array de hilos trabajadores
    pthread_t* threads;
    size_t num_threads;
    
    // Cola de trabajos (implementada como array circular)
    WorkItem* work_queue;
    size_t queue_capacity;
    size_t queue_size;
    size_t queue_front;  // Índice del frente de la cola
    size_t queue_back;   // Índice del final de la cola
    
    // Sincronización para la cola de trabajos
    pthread_mutex_t work_mutex;
    pthread_cond_t has_work;      // Señal: "hay trabajo disponible"
    
    // Sincronización para esperar finalización
    pthread_mutex_t done_mutex;
    pthread_cond_t work_done;     // Señal: "todo el trabajo terminó"
    
    // Contador de trabajos pendientes (atómico)
    atomic_size_t pending_work;
    
    // Flag para indicar que los hilos deben terminar
    bool should_exit;
    
} ThreadPool;

/* ============================================
 * FUNCIONES DEL THREADPOOL
 * ============================================ */

// Crea un pool de hilos con num_threads trabajadores
ThreadPool* threadpool_create(size_t num_threads);

// Destruye el pool de hilos y libera recursos
void threadpool_destroy(ThreadPool* tp);

// Añade un trabajo a la cola
// func: función a ejecutar
// arg: argumento para la función
void threadpool_enqueue(ThreadPool* tp, void (*func)(void*), void* arg);

// Espera a que todos los trabajos pendientes terminen
void threadpool_join(ThreadPool* tp);

// Función que ejecuta cada hilo trabajador (uso interno)
void* threadpool_worker(void* arg);

#endif // THREADPOOL_H
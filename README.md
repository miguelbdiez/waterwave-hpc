# Computación de alto rendimiento — Simulación de ondas en agua

La misma simulación numérica, **paralelizada de cinco formas distintas**, para comparar
modelos de programación paralela sobre el mismo problema y la misma máquina.

Asignatura: *Computación de Altas Prestaciones* — Máster Universitario en Ingeniería
Informática (USAL).

---

## El problema

`waterwave` resuelve las **ecuaciones de aguas someras** (shallow water equations) sobre una
malla bidimensional mediante un esquema de diferencias finitas de Lax-Wendroff en dos pasos.
En cada iteración se actualizan la altura de la lámina de agua y las dos componentes del
caudal, y se aplican condiciones de contorno reflectantes.

Es un problema de **stencil**: cada celda depende de sus vecinas en el paso anterior. Eso lo
hace idóneo para comparar estrategias de paralelización, porque el reparto del dominio y la
sincronización entre fronteras es justo donde cada modelo se comporta distinto.

## Las cinco versiones

| Versión | Modelo | Estrategia |
|---|---|---|
| [`PTHREADS/`](PTHREADS/) | Memoria compartida | *Thread pool* propio con barreras; reparto de la malla por bandas de filas |
| [`OPENMP/`](OPENMP/) | Memoria compartida | Directivas `#pragma omp parallel for` sobre los bucles de la malla |
| [`MPI/`](MPI/) | Memoria distribuida | Descomposición del dominio e intercambio de filas frontera (*halo exchange*) |
| [`OPENMP_PTHREADS/`](OPENMP_PTHREADS/) | Híbrido | Thread pool que a su vez paraleliza con OpenMP dentro de cada banda |
| [`OPENMP_MPI/`](OPENMP_MPI/) | Híbrido | MPI entre nodos, OpenMP dentro de cada nodo |

La versión con Pthreads implementa un **thread pool reutilizable** ([`threadpool.c`](PTHREADS/threadpool.c))
en lugar de crear y destruir hilos en cada iteración, que es donde se va el rendimiento en
una simulación con miles de pasos.

---

## Compilación y ejecución

Cada versión trae su propio Makefile:

```bash
# Pthreads
cd PTHREADS && make -f Makefile_pthreads
./waterwave 64 4                    # malla 64x64, 4 hilos

# OpenMP
cd OPENMP && make -f Makefile_openmp
OMP_NUM_THREADS=4 GRID_SIZE=64 STEP_COUNT=1000 DUMP_EVERY=1000 ./waterwave_openmp

# MPI
cd MPI && make -f Makefile_mpi
GRID_SIZE=64 STEP_COUNT=1000 DUMP_EVERY=1000 mpirun -np 4 ./waterwave_mpi

# Híbrido Pthreads + OpenMP
cd OPENMP_PTHREADS && make -f Makefile_pthreads_openmp
./waterwave_pthreads_openmp 64 2 3  # malla, hilos del pool, hilos OpenMP

# Híbrido MPI + OpenMP
cd OPENMP_MPI && make -f Makefile_mpi_openmp
mpirun -np 3 -x OMP_NUM_THREADS=2 ./waterwave_mpi_openmp config.txt
```

Requisitos: `gcc` con soporte OpenMP y una implementación de MPI (OpenMPI o MPICH).

Las cinco variantes se han comprobado sobre GCC 13 y OpenMPI en Ubuntu 24.04. El `Makefile`
de la variante híbrida MPI + OpenMP apuntaba a un fuente que no existe en su carpeta
(`waterwave_mpi.c` en lugar de `waterwave_mpi_openmp.c`), así que tal y como se entregó no
compilaba; está corregido.

---

## Resultados

El análisis completo de rendimiento —tiempos, aceleración y eficiencia para cada versión y
número de procesadores, con las gráficas correspondientes— está en
[`docs/waterwave_completo.pdf`](docs/waterwave_completo.pdf), y las tablas de medidas en
bruto en [`docs/waterwave_tablas.pdf`](docs/waterwave_tablas.pdf).

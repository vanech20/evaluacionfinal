#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <mpi.h>
#include <omp.h>
#include "image_processing.h"

#define TAG_PEDIR_TRABAJO 55
#define TAG_FIN 99
#define MAX_ARCHIVOS 600
#define TAREA_LIGERA 1
#define TAREA_BLUR 2

char archivos_global[MAX_ARCHIVOS][100];
int total_global = 0;
int index_ligeros = 0;
int index_blur = 0;
int kernel_blur = 111;
omp_lock_t lock_ligeros, lock_blur;

typedef struct {
    int tipo;
    char ruta[100];
} Tarea;

typedef struct {
    long long leidos;
    long long escritos;
    int imagenes;
} Resultados;

void generar_nombre_salida(const char* original, const char* sufijo, char* destino) {
    const char *nombre = strrchr(original, '/');
    nombre = (nombre) ? nombre + 1 : original;
    char base[256];
    strcpy(base, nombre);
    char *punto = strrchr(base, '.');
    if (punto) *punto = '\0';
    sprintf(destino, "results/%s_%s.bmp", base, sufijo);
}

int obtener_tamano_imagen(const char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) return 0;
    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, f);
    int width = *(int*)&header[18];
    int height = *(int*)&header[22];
    int row_padded = (width * 3 + 3) & (~3);
    int image_size = row_padded * height;
    fclose(f);
    return image_size;
}

void procesar_filtros_ligeros(const char* ruta, int rank, long long* leidos, long long* escritos, int* generadas) {
    int size = obtener_tamano_imagen(ruta);
    *leidos += size;
    *escritos += size * 5;
    *generadas += 5;

    const char *nombre = strrchr(ruta, '/');
    nombre = (nombre) ? nombre + 1 : ruta;
    char out1[300], out2[300], out3[300], out4[300], out5[300];

    generar_nombre_salida(nombre, "grayscale", out1);
    generar_nombre_salida(nombre, "mirrorh", out2);
    generar_nombre_salida(nombre, "mirrorv", out3);
    generar_nombre_salida(nombre, "gray_mirrorh", out4);
    generar_nombre_salida(nombre, "gray_mirrorv", out5);

    #pragma omp parallel sections
    {
        #pragma omp section
        grayscale(ruta, out1);
        #pragma omp section
        mirror_horizontal(ruta, out2);
        #pragma omp section
        mirror_vertical(ruta, out3);
        #pragma omp section
        grayscale_mirror_horizontal(ruta, out4);
        #pragma omp section
        grayscale_mirror_vertical(ruta, out5);
    }

    printf("[Rank %d] Filtros ligeros procesados: %s\n", rank, nombre);
    fflush(stdout);
}

void procesar_blur(const char* ruta, int rank, long long* leidos, long long* escritos, int* generadas) {
    int size = obtener_tamano_imagen(ruta);
    *leidos += size;
    *escritos += size;
    *generadas += 1;

    const char *nombre = strrchr(ruta, '/');
    nombre = (nombre) ? nombre + 1 : ruta;
    char out[300], sufijo[64];
    sprintf(sufijo, "blur_%d", kernel_blur);
    generar_nombre_salida(nombre, sufijo, out);
    blur_image(ruta, out, nombre, kernel_blur);

    printf("[Rank %d] Blur aplicado: %s\n", rank, nombre);
    fflush(stdout);
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    omp_init_lock(&lock_ligeros);
    omp_init_lock(&lock_blur);

    double inicio = MPI_Wtime();

    if (rank == 0) {
        if (argc < 2) {
            fprintf(stderr, "Uso: %s <kernel_blur>\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        kernel_blur = atoi(argv[1]);
        if (kernel_blur < 55 || kernel_blur > 155 || kernel_blur % 2 == 0) {
            fprintf(stderr, "Kernel inválido\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        DIR *dir = opendir("images/");
        struct dirent *ent;
        if (!dir) {
            perror("No se pudo abrir el directorio");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        while ((ent = readdir(dir)) != NULL && total_global < MAX_ARCHIVOS) {
            if (strstr(ent->d_name, ".bmp")) {
                sprintf(archivos_global[total_global], "images/%s", ent->d_name);
                total_global++;
            }
        }
        closedir(dir);
    }

    MPI_Bcast(&kernel_blur, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&total_global, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(archivos_global, MAX_ARCHIVOS * 100, MPI_CHAR, 0, MPI_COMM_WORLD);

    int ya_termino[128] = {0};
    Resultados local = {0, 0, 0};

    if (rank == 0) {
        int terminados = 0;
        while (terminados < size - 1) {
            int solicitante;
            MPI_Status s;
            MPI_Recv(&solicitante, 1, MPI_INT, MPI_ANY_SOURCE, TAG_PEDIR_TRABAJO, MPI_COMM_WORLD, &s);

            if (ya_termino[solicitante]) continue;

            Tarea t;
            int asignada = 0;

            omp_set_lock(&lock_ligeros);
            if (index_ligeros < total_global) {
                t.tipo = TAREA_LIGERA;
                strcpy(t.ruta, archivos_global[index_ligeros++]);
                asignada = 1;
            }
            omp_unset_lock(&lock_ligeros);

            if (!asignada) {
                omp_set_lock(&lock_blur);
                if (index_blur < total_global && solicitante != 10 && solicitante != 11) {
                    t.tipo = TAREA_BLUR;
                    strcpy(t.ruta, archivos_global[index_blur++]);
                    asignada = 1;
                }
                omp_unset_lock(&lock_blur);
            }

            if (!asignada) {
                t.tipo = -1;
                ya_termino[solicitante] = 1;
                terminados++;
            }

            MPI_Send(&t, sizeof(Tarea), MPI_BYTE, solicitante, 0, MPI_COMM_WORLD);
        }

        Resultados total = {0, 0, 0};
        for (int i = 1; i < size; i++) {
            Resultados rcv;
            MPI_Recv(&rcv, sizeof(Resultados), MPI_BYTE, i, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            total.leidos += rcv.leidos;
            total.escritos += rcv.escritos;
            total.imagenes += rcv.imagenes;
        }

        double fin = MPI_Wtime();
        double duracion = fin - inicio;
        int instrucciones = (total.imagenes) * 20;
        double MIPS = instrucciones / (1e6 * duracion);
        long long bytes_procesados = (total.leidos + total.escritos);
        double Bps = bytes_procesados / duracion;
        double MBps = Bps / (1024.0 * 1024.0);
        double GBps = Bps / (1024.0 * 1024.0 * 1024.0);

        FILE* f = fopen("results.txt", "w");
        fprintf(f, "Tiempo total: %.2fs\n", duracion);
        fprintf(f, "Total de imágenes generadas: %d\n", total.imagenes);
        fprintf(f, "Lecturas: %lld bytes\n", total.leidos);
        fprintf(f, "Escrituras: %lld bytes\n", total.escritos);
        fprintf(f, "Instrucciones: %d\n", instrucciones);
        fprintf(f, "MIPS estimado: %.10f\n", MIPS);
        fprintf(f, "Bytes procesados totales: %lld\n", bytes_procesados);
        fprintf(f, "Velocidad: %.2f MB/s (%.2f GB/s)\n", MBps, GBps);
        fclose(f);

    } else {
        while (1) {
            MPI_Send(&rank, 1, MPI_INT, 0, TAG_PEDIR_TRABAJO, MPI_COMM_WORLD);
            Tarea t;
            MPI_Recv(&t, sizeof(Tarea), MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (t.tipo == -1) break;
            else if (t.tipo == TAREA_LIGERA) {
                procesar_filtros_ligeros(t.ruta, rank, &local.leidos, &local.escritos, &local.imagenes);
            } else if (t.tipo == TAREA_BLUR && rank != 10 && rank != 11) {
                procesar_blur(t.ruta, rank, &local.leidos, &local.escritos, &local.imagenes);
            }
        }
        MPI_Send(&local, sizeof(Resultados), MPI_BYTE, 0, 100, MPI_COMM_WORLD);
    }

    // Eliminado el MPI_Barrier para evitar retardos innecesarios
    omp_destroy_lock(&lock_ligeros);
    omp_destroy_lock(&lock_blur);
    MPI_Finalize();
    return 0;
}

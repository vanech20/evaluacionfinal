// image_processing.h
#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#include <stdio.h>
#include <stdlib.h>

void grayscale(const char *input_path, const char *output_path) {
    FILE *image = fopen(input_path, "rb");
    FILE *outputImage = fopen(output_path, "wb");
    if (!image || !outputImage) return;

    unsigned char r, g, b;
    for (int i = 0; i < 54; i++) fputc(fgetc(image), outputImage);
    while (!feof(image)) {
        b = fgetc(image);
        g = fgetc(image);
        r = fgetc(image);
        if (feof(image)) break;
        unsigned char pixel = 0.21 * r + 0.72 * g + 0.07 * b;
        fputc(pixel, outputImage);
        fputc(pixel, outputImage);
        fputc(pixel, outputImage);
    }
    fclose(image);
    fclose(outputImage);
}

void mirror_horizontal(const char *input_path, const char *output_path) {
    FILE *image = fopen(input_path, "rb");
    FILE *outputImage = fopen(output_path, "wb");
    if (!image || !outputImage) return;

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int ancho = *(int*)&header[18];
    int alto = *(int*)&header[22];
    int padding = (4 - (ancho * 3) % 4) % 4;
    unsigned char *fila = (unsigned char *)malloc(ancho * 3 + padding);

    for (int i = 0; i < alto; i++) {
        fread(fila, sizeof(unsigned char), ancho * 3 + padding, image);
        for (int j = ancho - 1; j >= 0; j--) {
            fwrite(&fila[j * 3], sizeof(unsigned char), 3, outputImage);
        }
        fwrite(&fila[ancho * 3], sizeof(unsigned char), padding, outputImage);
    }

    free(fila);
    fclose(image);
    fclose(outputImage);
}

void mirror_vertical(const char *input_path, const char *output_path) {
    FILE *image = fopen(input_path, "rb");
    FILE *outputImage = fopen(output_path, "wb");
    if (!image || !outputImage) return;

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int ancho = *(int*)&header[18];
    int alto = *(int*)&header[22];
    int filaSize = ancho * 3;
    int padding = (4 - (filaSize % 4)) % 4;
    int filaCompleta = filaSize + padding;

    unsigned char *data = (unsigned char *)malloc(alto * filaCompleta);
    fread(data, sizeof(unsigned char), alto * filaCompleta, image);

    for (int i = alto - 1; i >= 0; i--) {
        fwrite(&data[i * filaCompleta], sizeof(unsigned char), filaCompleta, outputImage);
    }

    free(data);
    fclose(image);
    fclose(outputImage);
}

void grayscale_mirror_horizontal(const char *input_path, const char *output_path) {
    FILE *image = fopen(input_path, "rb");
    FILE *outputImage = fopen(output_path, "wb");
    if (!image || !outputImage) return;

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int ancho = *(int*)&header[18];
    int alto = *(int*)&header[22];
    int padding = (4 - (ancho * 3) % 4) % 4;

    unsigned char *fila = (unsigned char *)malloc(ancho * 3 + padding);
    unsigned char *grisFila = (unsigned char *)malloc(ancho * 3);

    for (int i = 0; i < alto; i++) {
        fread(fila, sizeof(unsigned char), ancho * 3 + padding, image);
        for (int j = 0; j < ancho; j++) {
            int idx = j * 3;
            unsigned char b = fila[idx];
            unsigned char g = fila[idx + 1];
            unsigned char r = fila[idx + 2];
            unsigned char gray = 0.21 * r + 0.72 * g + 0.07 * b;
            int revIdx = (ancho - 1 - j) * 3;
            grisFila[revIdx] = gray;
            grisFila[revIdx + 1] = gray;
            grisFila[revIdx + 2] = gray;
        }
        fwrite(grisFila, sizeof(unsigned char), ancho * 3, outputImage);
        fwrite(&fila[ancho * 3], sizeof(unsigned char), padding, outputImage);
    }

    free(fila);
    free(grisFila);
    fclose(image);
    fclose(outputImage);
}

void grayscale_mirror_vertical(const char *input_path, const char *output_path) {
    FILE *image = fopen(input_path, "rb");
    FILE *outputImage = fopen(output_path, "wb");
    if (!image || !outputImage) return;

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int ancho = *(int*)&header[18];
    int alto = *(int*)&header[22];
    int padding = (4 - (ancho * 3) % 4) % 4;
    int filaSize = ancho * 3 + padding;

    unsigned char *data = (unsigned char *)malloc(alto * filaSize);
    unsigned char *grises = (unsigned char *)malloc(alto * filaSize);

    fread(data, sizeof(unsigned char), alto * filaSize, image);

    for (int i = 0; i < alto; i++) {
        for (int j = 0; j < ancho; j++) {
            int idx = i * filaSize + j * 3;
            unsigned char b = data[idx];
            unsigned char g = data[idx + 1];
            unsigned char r = data[idx + 2];
            unsigned char gray = 0.21 * r + 0.72 * g + 0.07 * b;
            grises[idx] = gray;
            grises[idx + 1] = gray;
            grises[idx + 2] = gray;
        }
        for (int p = 0; p < padding; p++) {
            grises[i * filaSize + ancho * 3 + p] = data[i * filaSize + ancho * 3 + p];
        }
    }

    for (int i = alto - 1; i >= 0; i--) {
        fwrite(&grises[i * filaSize], sizeof(unsigned char), filaSize, outputImage);
    }

    free(data);
    free(grises);
    fclose(image);
    fclose(outputImage);
}

struct imageMetadata {
    int width;
    int height;
    int imageSize;
    char name[512];
    char destinationFolder[128];
};

void blur_image(const char* in, const char* out, const char* nombre_base, int kernel_size) {
    if (kernel_size < 55 || kernel_size > 155 || kernel_size % 2 == 0) {
        printf("Kernel inválido. Debe ser impar y entre 55 y 155.\n");
        return;
    }

    printf("\nAplicando blur separable con kernel %dx%d\n", kernel_size, kernel_size);

    FILE *image = fopen(in, "rb");
    FILE *outputImage = fopen(out, "wb");
    if (!image || !outputImage) {
        printf("Error abriendo archivos.\n");
        return;
    }

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int width = *(int*)&header[18];
    int height = *(int*)&header[22];
    int row_padded = (width * 3 + 3) & (~3);
    size_t image_size = row_padded * height;

    unsigned char* input_data = (unsigned char*)malloc(image_size);
    unsigned char* temp_data = (unsigned char*)malloc(image_size);
    unsigned char* output_data = (unsigned char*)malloc(image_size);

    if (!input_data || !temp_data || !output_data) {
        printf("Error reservando memoria.\n");
        fclose(image);
        fclose(outputImage);
        free(input_data); free(temp_data); free(output_data);
        return;
    }

    fread(input_data, sizeof(unsigned char), image_size, image);

    int k = kernel_size / 2;

    // Blur horizontal
    #pragma omp parallel for
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumB = 0, sumG = 0, sumR = 0, count = 0;

            for (int dx = -k; dx <= k; dx++) {
                int nx = x + dx;
                if (nx >= 0 && nx < width) {
                    int idx = y * row_padded + nx * 3;
                    sumB += input_data[idx + 0];
                    sumG += input_data[idx + 1];
                    sumR += input_data[idx + 2];
                    count++;
                }
            }

            int idx_out = y * row_padded + x * 3;
            temp_data[idx_out + 0] = sumB / count;
            temp_data[idx_out + 1] = sumG / count;
            temp_data[idx_out + 2] = sumR / count;
        }

        // Copia padding tal cual
        for (int p = width * 3; p < row_padded; p++) {
            temp_data[y * row_padded + p] = input_data[y * row_padded + p];
        }
    }

    // Blur vertical
    #pragma omp parallel for
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumB = 0, sumG = 0, sumR = 0, count = 0;

            for (int dy = -k; dy <= k; dy++) {
                int ny = y + dy;
                if (ny >= 0 && ny < height) {
                    int idx = ny * row_padded + x * 3;
                    sumB += temp_data[idx + 0];
                    sumG += temp_data[idx + 1];
                    sumR += temp_data[idx + 2];
                    count++;
                }
            }

            int idx_out = y * row_padded + x * 3;
            output_data[idx_out + 0] = sumB / count;
            output_data[idx_out + 1] = sumG / count;
            output_data[idx_out + 2] = sumR / count;
        }

        // Copia padding tal cual
        for (int p = width * 3; p < row_padded; p++) {
            output_data[y * row_padded + p] = temp_data[y * row_padded + p];
        }
    }

    fwrite(output_data, sizeof(unsigned char), image_size, outputImage);

    free(input_data);
    free(temp_data);
    free(output_data);
    fclose(image);
    fclose(outputImage);

    printf("Blur aplicado correctamente.\n");
}


#endif // IMAGE_PROCESSING_H

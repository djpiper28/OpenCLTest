#ifndef MAIN_
#define MAIN_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>

#include <CL/cl.h>

#include "defs.h"

#define VERSION "v1.2"
#define PROGRAM_FILE "solve_kernel.cl"
#define KERNEL_FUNC "solve_kernel"
#define RESET_FUNC "set_changed_to_zero"

#define WG_SIZE 256 // Workgroup size


struct PNG {    
    png_structp	png_ptr;
    png_infop info_ptr;
    
    png_uint_32 width;
    png_uint_32 height;
    
    png_bytepp rows;
    
    int bit_depth;
    int color_type;
    int interlace_method;
    int compression_method;
    int filter_method;
    int bytes_pp;
};

//rgb is inverted but who cares
cl_int *intArr (char **rows, long width, long height, long bytes_pp) {
    cl_int *data = (int *) malloc(sizeof(cl_int) * width * height);
    
    for (long y = 0; y < height; y++) {
        for (long x = 0; x < width; x++) {
            data[x + y * width] = 0;
            
            for (long i = 0; i < bytes_pp; i++) {
                data[x + y * width] |= rows[y][bytes_pp * x + i] << 8 * i; 
            }            
        }    
    }
    
    return data;
}

int solveMazeSegment (long width,
                      long height,
                      long startX, 
                      long startY,
                      long endX, 
                      long endY, 
                      cl_mem dbuffrows,
                      cl_mem dans,
                      cl_device_id device, 
                      cl_context context, 
                      cl_program program,
                      cl_kernel kernel, 
                      cl_kernel reset_kernel, 
                      cl_command_queue queue) {
    cl_int err;
    size_t local_size, size_2, size_3, global_size;
        
    // Number of work items in each local work group
    local_size = endX - startX;
    // Number of total work items - localSize must be devisor
    global_size = (endY - startY) * local_size;
    
    size_2 = size_3 = 1;
    
    //kernel args             
    err  = clSetKernelArg(kernel, 0, sizeof(long), &width); 
    err  = clSetKernelArg(kernel, 1, sizeof(long), &local_size); 
    err |= clSetKernelArg(kernel, 2, sizeof(long), &startX);
    err |= clSetKernelArg(kernel, 3, sizeof(long), &startY); 
    err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &dbuffrows);
    err |= clSetKernelArg(kernel, 5, sizeof(cl_mem), &dans);
    
    if (err != CL_SUCCESS)
        printf("Error code :%x while adding kernel args\n", err);
   
    err = clSetKernelArg(reset_kernel, 0, sizeof(cl_mem), &dans); 
    //While changed run the kernels    
    int changed = 1, i = 0;
    while (changed) {     
        /* Enqueue kernel */
        err = clEnqueueNDRangeKernel(queue, reset_kernel, 1, NULL, &size_2,
                                     &size_3, 0, NULL, NULL);
        
        if (err != CL_SUCCESS)
            printf("Error code :%d while resetting the changed buffer\n", err);
        //wait for reset of array
        clFinish(queue);           
        
        /* Enqueue kernel */
        err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, 
                                     &local_size, 0, NULL, NULL); 
        
        if (err != CL_SUCCESS)
            printf("Error code :%d while running the solve kernel\n", err);     
        
        /* Wait for the command queue to get serviced before reading 
         * back results */    
        clFinish(queue);
         
        int arr[1];
        /* Read the kernel's output */
        clEnqueueReadBuffer(queue, dans, CL_TRUE, 0, sizeof(int), arr, 0, 
                            NULL, NULL);
        clFinish(queue);
        
        changed = *arr;   
        i++;
    } 
    
    return i;
}

void solveMaze(struct PNG *maze) {
    // init
    /* OpenCL structures */
    cl_device_id device;
    cl_context context;
    cl_program program;
    cl_kernel kernel, reset_kernel;
    cl_command_queue queue;
    cl_int err;
    
    int segments = 0;
    
    int segSize;
    if ((maze->width - 2) * (maze->height - 2) > CL_DEVICE_MAX_WORK_GROUP_SIZE) {
        segSize = floor(sqrt(CL_DEVICE_MAX_WORK_GROUP_SIZE));
        printf("Splitting into segments of size %dx%d (workgroup size %d)\n", 
               segSize, segSize, CL_DEVICE_MAX_WORK_GROUP_SIZE);
        segments = 1;
    }        
       
    // Device input and output buffers
    cl_mem dans;
    
    /* Create device and context */
    device = create_device();
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    
    /* Build program */
    program = build_program(context, device, PROGRAM_FILE);
    
    /* Create a command queue */
    queue = clCreateCommandQueue(context, device, 0, &err);
    
    //changed buffer    
    dans = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(int), NULL, NULL);  
    int tmp[] = {0};
    err = clEnqueueWriteBuffer(queue, dans, CL_TRUE, 0,
                               sizeof(int), tmp, 0, NULL, NULL);
    if (err != CL_SUCCESS)
        printf("Error code :%x while writing changed buffer\n", err);
    
    //data buffer
    int len = sizeof(cl_int) * maze->width * maze->height;
    cl_int *data = intArr((char **) maze->rows, maze->width, maze->height, maze->bytes_pp);
    cl_mem dbuffrows = clCreateBuffer(context, CL_MEM_READ_WRITE, 
                               len, NULL, NULL);
    err = clEnqueueWriteBuffer(queue, dbuffrows, CL_TRUE, 0,
                               len, data, 0, NULL, NULL);
    
    if (err != CL_SUCCESS)
        printf("Error code :%x while writing image buffer\n", err);    
            
    //make kernel
    kernel = clCreateKernel(program, KERNEL_FUNC, &err);   
    reset_kernel = clCreateKernel(program , RESET_FUNC, &err);
    
    //create and solve segments
    if (!segments) {
        solveMazeSegment(maze->width, maze->height, 
                         1, 1, 
                         maze->width - 1, maze->height - 1,
                         dbuffrows,
                         dans,
                         device, 
                         context, 
                         program,
                         kernel, 
                         reset_kernel, 
                         queue);        
    } else {
        int cont = 1;
        
        while (cont) {
            cont = 0;
            
            for (int y = 1; y < maze->height - 1; y += segSize) {
                for (int x = 1; x < maze->width - 1; x += segSize) {
                    int endX = x + segSize, 
                        endY = y + segSize;
                        
                    //Check segment is correct size
                    //if (endX > maze->width - 1) endX = maze->width - 1;
                    //if (endY > maze->height - 1) endY = maze->height - 1;
                    
                    //branchless min
                    int a = endX > (maze->width - 1);
                    endX = a * (maze->width - 1) +
                          !a * endX;
                    
                    //branchless min
                    int b = endY > (maze->height - 1);
                    endY = b * (maze->height - 1) +
                          !b * endY;
                    
                    int iterations = solveMazeSegment(maze->width, maze->height, 
                                               x, y,
                                               endX, endY,
                                               dbuffrows,
                                               dans,
                                               device, 
                                               context, 
                                               program,
                                               kernel, 
                                               reset_kernel, 
                                               queue);
                    
                    if (iterations > 1) cont = 1; 
                }
            }
        }
    }
    
    //get output
    cl_int *rows = (int *) malloc(len);
    clEnqueueReadBuffer(queue, dbuffrows, CL_TRUE, 0, len, rows, 0, NULL, NULL);
    
    //set png rows (on cpu)
    char **pngRows = (char **) malloc(sizeof(char *) * maze->height);
    for (int y = 0; y < maze->height; y++) {
        char *pngRow = (char *) malloc(sizeof(char) * maze->width * maze->bytes_pp);
        
        for (int x = 0; x < maze->width; x++) {
            //convert one pixel to n bytes
            for (int i = 0; i < maze->bytes_pp; i++) {
                if (x == 0 || x == maze->width - 1 
                 || y == 0 || y == maze->height - 1) {
                    //copy from image
                    pngRow[maze->bytes_pp * x + i] = maze->rows[y]
                                                    [maze->bytes_pp * x + i];
                } else {
                    //read from output
                    pngRow[maze->bytes_pp * x + i] = (char) (0xFF & 
                                        (rows[x + y * maze->width] >> 8 * i));  
                    
                }
            }
        }
        
        pngRows[y] = pngRow;
    }
    
    //Free old rows
    for (int y = 0; y < maze->height; y++) {
        png_free (maze->png_ptr, maze->rows[y]);
    }
    png_free (maze->png_ptr, maze->rows);
    
    //Assign new rows
    maze->rows = (png_bytepp) pngRows;
    
    clReleaseMemObject(dbuffrows);
        
    /* Deallocate resources */
    clReleaseKernel(kernel);
    clReleaseMemObject(dans);
    clReleaseCommandQueue(queue);
    clReleaseProgram(program);
    clReleaseContext(context);   
    
    free(data);
}

struct PNG readPNG(FILE *inputFile) {
    struct PNG png;
    png.png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, 
                                          NULL, NULL, NULL);
    
    if (png.png_ptr == NULL)
        return png;
        
    png.info_ptr = png_create_info_struct (png.png_ptr);    
    
    if (png.info_ptr == NULL)
        return png;
    
    png_init_io (png.png_ptr, inputFile);
    png_read_png (png.png_ptr, png.info_ptr, 0, 0);
    png_get_IHDR (png.png_ptr, png.info_ptr, & png.width, & png.height, & png.bit_depth,
                  & png.color_type, & png.interlace_method, & png.compression_method,
                  & png.filter_method);
        
    png.rows = png_get_rows (png.png_ptr, png.info_ptr);
    png.bytes_pp = png_get_rowbytes (png.png_ptr, png.info_ptr) / png.width;
    
    printf("BYTESPP=%d\n", png.bytes_pp);
    
    return png;
}

void writePNG (FILE *outputFile, struct PNG png) {  
    
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    
    
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);    
    info_ptr = png_create_info_struct (png_ptr);
    
    png_set_IHDR (png_ptr,
                  info_ptr,
                  png.width,
                  png.height,
                  png.bit_depth,
                  png.color_type,
                  png.interlace_method,
                  png.compression_method,
                  png.filter_method);    
    
    png_init_io (png_ptr, outputFile);
    png_set_rows (png_ptr, info_ptr, png.rows);
    png_write_png (png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
    png_destroy_write_struct (&png_ptr, &info_ptr);
}

int solve (char *input, char *output) {
    int status = EXIT_SUCCESS;
    
    //Read input
    FILE *inputFile = fopen(input, "rb");
    
    if (inputFile == NULL) {
        //Unable to open inputFile
        printf("Cannot open input file\n");
        status = EXIT_FAILURE;
    } else {
        //Read png bytes
        struct PNG image = readPNG(inputFile);
        fclose(inputFile);        
        
        if (image.png_ptr == NULL || image.info_ptr == NULL) {
            //Bad png
            printf("Input file is an invalid PNG file\n");
            status = EXIT_FAILURE;
        } else {  
            //Solve
            solveMaze(&image);
            
            //Save output
            FILE *outputFile = fopen(output, "wb+");            
            
            if (outputFile == NULL) {
                printf("Cannot open output file\n");
                status = EXIT_FAILURE;
            } else {
                writePNG(outputFile, image);
                
                fclose(outputFile);
            }        
        }
    }
    
    return status;
}

int main (int argc, char **argv) {
    int statusCode = EXIT_SUCCESS;
    
    //Check arguments
    if (argc != 3) {
        printf("Maze solver %s\n", VERSION);
        printf("Usage: mazeSolver <input file> <output file>\n");
    } else {
        //Check input exists
        if (access(argv[1], F_OK) == 0) {
            //Check input can be read
            if (access(argv[1], R_OK) == 0) {
                //If output is exists check it can be written
                if (access(argv[2], F_OK) == 0) {
                    printf("Overwriting file output file\n");
                    
                    if (access(argv[2], W_OK) != 0) {
                        printf("Unable to write output file\n");
                        statusCode = EXIT_FAILURE;
                    } else {
                        //solve
                        statusCode = solve(argv[1], argv[2]);
                    }
                } else {
                    //solve
                    statusCode = solve(argv[1], argv[2]);
                }
            } else {
                printf("Input file cannot be read\n");
                statusCode = EXIT_FAILURE;
            }
        } else {
            printf("Input file does not exist\n");
            statusCode = EXIT_FAILURE;
        }
    }
    
    return statusCode;
}

#endif

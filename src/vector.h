/**
 *  @file vector.h
 *  @brief Defines our context struct.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef VECTOR_H
#define VECTOR_H

#define _GNU_SOURCE



//Store and track the stored data
typedef struct sVectorList {
    void **items;
    int capacity;
    int total;
} sVectorList;



//structure contain the function pointer
typedef struct sVector vector;
struct sVector {
    sVectorList vectorList;
//function pointers
    void (*vector_init)(vector *v);
    int (*pfVectorTotal)(vector *);
    int (*pfVectorResize)(vector *, int);
    int (*pfVectorAdd)(vector *, void *);
    int (*pfVectorSet)(vector *, int, void *);
    void *(*pfVectorGet)(vector *, int);
    int (*pfVectorDelete)(vector *, int);
    int (*pfVectorFree)(vector *);
};



void vector_init(vector *v);

#endif

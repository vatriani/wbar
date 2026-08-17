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



/**
int main(int argc, char *argv[]) {
    int i =0;
//init vector
    VECTOR_INIT(v);
//Add  data in vector
    v.pfVectorAdd(&v,"aticleworld.com\n");
    v.pfVectorAdd(&v,"amlendra\n");
    v.pfVectorAdd(&v,"Pooja\n");
//print the data and type cast it
    for (i = 0; i < v.pfVectorTotal(&v); i++)
        printf("%s", (char*)v.pfVectorGet(&v, i));
//Set the data at index 0
    v.pfVectorSet(&v,0,"Apoorv\n");
    printf("\n\n\nVector list after changes\n\n\n");
//print the data and type cast it
    for (i = 0; i < v.pfVectorTotal(&v); i++)
        printf("%s", (char*)v.pfVectorGet(&v, i));
    return 0;
}
*/



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

/*
    -- MAGMA (version 2.5.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2019

       @generated from sparse/blas/zgesellcmv.cu, normal z -> s, Fri Aug  2 17:10:12 2019

*/
#include "magmasparse_internal.h"

#define BLOCK_SIZE 512


#define PRECISION_s


// SELLC SpMV kernel
// see paper by M. KREUTZER, G. HAGER, G WELLEIN, H. FEHSKE A. BISHOP
// A UNIFIED SPARSE MATRIX DATA FORMAT 
// FOR MODERN PROCESSORS WITH WIDE SIMD UNITS
__global__ void 
sgesellcmv_kernel(   
    int num_rows, 
    int num_cols,
    int blocksize,
    float alpha, 
    float * dval, 
    magma_index_t * dcolind,
    magma_index_t * drowptr,
    float * dx,
    float beta, 
    float * dy)
{
    // threads assigned to rows
    int Idx = blockDim.x * blockIdx.x + threadIdx.x;
    int offset = drowptr[ blockIdx.x ];
    int border = (drowptr[ blockIdx.x+1 ]-offset)/blocksize;
    if(Idx < num_rows ){
        float dot = MAGMA_S_MAKE(0.0, 0.0);
        for ( int n = 0; n < border; n++){ 
            int col = dcolind [offset+ blocksize * n + threadIdx.x ];
            float val = dval[offset+ blocksize * n + threadIdx.x];
            if( val != 0){
                  dot=dot+val*dx[col];
            }
        }

        dy[ Idx ] = dot * alpha + beta * dy [ Idx ];
    }
}


/**
    Purpose
    -------
    
    This routine computes y = alpha *  A^t *  x + beta * y on the GPU.
    Input format is SELLC/SELLP.
    
    Arguments
    ---------

    @param[in]
    transA      magma_trans_t
                transposition parameter for A

    @param[in]
    m           magma_int_t
                number of rows in A

    @param[in]
    n           magma_int_t
                number of columns in A 

    @param[in]
    blocksize   magma_int_t
                number of rows in one ELL-slice

    @param[in]
    slices      magma_int_t
                number of slices in matrix

    @param[in]
    alignment   magma_int_t
                number of threads assigned to one row (=1)

    @param[in]
    alpha       float
                scalar multiplier

    @param[in]
    dval        magmaFloat_ptr
                array containing values of A in SELLC/P

    @param[in]
    dcolind     magmaIndex_ptr
                columnindices of A in SELLC/P

    @param[in]
    drowptr     magmaIndex_ptr
                rowpointer of SELLP

    @param[in]
    dx          magmaFloat_ptr
                input vector x

    @param[in]
    beta        float
                scalar multiplier

    @param[out]
    dy          magmaFloat_ptr
                input/output vector y

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_sblas
    ********************************************************************/

extern "C" magma_int_t
magma_sgesellcmv(
    magma_trans_t transA,
    magma_int_t m, magma_int_t n,
    magma_int_t blocksize,
    magma_int_t slices,
    magma_int_t alignment,
    float alpha,
    magmaFloat_ptr dval,
    magmaIndex_ptr dcolind,
    magmaIndex_ptr drowptr,
    magmaFloat_ptr dx,
    float beta,
    magmaFloat_ptr dy,
    magma_queue_t queue )
{
    // the kernel can only handle up to 65535 slices 
    // (~2M rows for blocksize 32)
    dim3 grid( slices, 1, 1);
    magma_int_t threads = blocksize;
    sgesellcmv_kernel<<< grid, threads, 0, queue->cuda_stream() >>>
    ( m, n, blocksize, alpha,
        dval, dcolind, drowptr, dx, beta, dy );

    return MAGMA_SUCCESS;
}

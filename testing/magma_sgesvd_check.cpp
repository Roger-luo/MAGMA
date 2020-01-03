/*
    -- MAGMA (version 2.5.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2019

       @author Mark Gates
       @generated from testing/magma_zgesvd_check.cpp, normal z -> s, Fri Aug  2 17:10:11 2019
*/

#include "magma_v2.h"
#include "magma_lapack.h"
#include "testings.h"

#define REAL

/**
    Check the results following the LAPACK's [zcds]drvbd routine.
    A is factored as A = U diag(S) VT and the following 4 tests computed:
    (1)    | A - U diag(S) VT | / ( |A| max(m,n) )
    (2)    | I - U^H U   | / ( m )
    (3)    | I - VT VT^H | / ( n )
    (4)    S contains min(m,n) nonnegative values in decreasing order.
           (Return 0 if true, 1 if false.)
    
    If check is false, skips (1) - (3), but always does (4).
    ********************************************************************/
extern "C"
void check_sgesvd(
    magma_int_t check,
    magma_vec_t jobu,
    magma_vec_t jobv,
    magma_int_t m, magma_int_t n,
    float *A,  magma_int_t lda,
    float *S,
    float *U,  magma_int_t ldu,
    float *VT, magma_int_t ldv,
    float result[4] )
{
    float unused[1];
    const magma_int_t izero = 0;
    float eps = lapackf77_slamch( "E" );
    
    if ( jobu == MagmaNoVec ) {
        U = NULL;
    }
    if ( jobv == MagmaNoVec ) {
        VT = NULL;
    }
    
    // -1 indicates check not done
    result[0] = -1;
    result[1] = -1;
    result[2] = -1;
    result[3] = -1;
    
    magma_int_t min_mn = min(m, n);
    magma_int_t n_u  = (jobu == MagmaAllVec ? m : min_mn);
    magma_int_t m_vt = (jobv == MagmaAllVec ? n : min_mn);
    
    assert( lda >= m );
    assert( ldu >= m );
    assert( ldv >= m_vt );
    
    if ( check ) {
        // sbdt01 needs m+n
        // sort01 prefers n*(n+1) to check U; m*(m+1) to check V
        magma_int_t lwork_err = m+n;
        if ( U != NULL ) {
            lwork_err = max( lwork_err, n_u*(n_u+1) );
        }
        if ( VT != NULL ) {
            lwork_err = max( lwork_err, m_vt*(m_vt+1) );
        }
        float *work_err;
        TESTING_CHECK( magma_smalloc_cpu( &work_err, lwork_err ));
        
        // sbdt01 and sort01 need max(m,n), depending
        float *rwork_err;
        TESTING_CHECK( magma_smalloc_cpu( &rwork_err, max(m,n) ));
        
        if ( U != NULL && VT != NULL ) {
            // since KD=0 (3rd arg), E is not referenced so pass unused (9th arg)
            lapackf77_sbdt01( &m, &n, &izero, A, &lda,
                              U, &ldu, S, unused, VT, &ldv,
                              work_err,
                              #ifdef COMPLEX
                              rwork_err,
                              #endif
                              &result[0] );
        }
        if ( U != NULL ) {
            lapackf77_sort01( "Columns", &m,  &n_u, U,  &ldu, work_err, &lwork_err,
                              #ifdef COMPLEX
                              rwork_err,
                              #endif
                              &result[1] );
        }
        if ( VT != NULL ) {
            lapackf77_sort01( "Rows",    &m_vt, &n, VT, &ldv, work_err, &lwork_err,
                              #ifdef COMPLEX
                              rwork_err,
                              #endif
                              &result[2] );
        }
        
        result[0] *= eps;
        result[1] *= eps;
        result[2] *= eps;
        
        magma_free_cpu( work_err );
        magma_free_cpu( rwork_err );
    }
    
    // check S is sorted
    result[3] = 0.;
    for (int j=0; j < min_mn-1; j++) {
        if ( S[j] < S[j+1] )
            result[3] = 1.;
        if ( S[j] < 0. )
            result[3] = 1.;
    }
    if (min_mn > 1 && S[min_mn-1] < 0.) {
        result[3] = 1.;
    }
}

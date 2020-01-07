/*
    -- MAGMA (version 2.5.2) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date November 2019

       @author Hartwig Anzt

       @generated from sparse/src/zparilut_cpu.cpp, normal z -> c, Sun Nov 24 14:37:48 2019
*/

#include "magmasparse_internal.h"
#ifdef _OPENMP
#include <omp.h>
#endif

#define PRECISION_c


/***************************************************************************//**
    Purpose
    -------

    Generates an incomplete threshold LU preconditioner via the ParILUT 
    algorithm. The strategy is to interleave a parallel fixed-point 
    iteration that approximates an incomplete factorization for a given nonzero 
    pattern with a procedure that adaptively changes the pattern. 
    Much of this algorithm has fine-grained parallelism, and can efficiently 
    exploit the compute power of shared memory architectures.

    This is the routine used in the publication by Anzt, Chow, Dongarra:
    ''ParILUT - A new parallel threshold ILU factorization''
    submitted to SIAM SISC in 2017.
    
    This version uses the default setting which adds all candidates to the
    sparsity pattern.

    This function requires OpenMP, and is only available if OpenMP is activated.
    
    The parameter list is:
    
    precond.sweeps : number of ParILUT steps
    precond.atol   : absolute fill ratio (1.0 keeps nnz count constant)


    Arguments
    ---------

    @param[in]
    A           magma_c_matrix
                input matrix A

    @param[in]
    b           magma_c_matrix
                input RHS b

    @param[in,out]
    precond     magma_c_preconditioner*
                preconditioner parameters

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_cgepr
*******************************************************************************/
extern "C"
magma_int_t
magma_cparilut_cpu(
    magma_c_matrix A,
    magma_c_matrix b,
    magma_c_preconditioner *precond,
    magma_queue_t queue)
{
    magma_int_t info = 0;
    
#ifdef _OPENMP

    real_Double_t start, end;
    real_Double_t t_rm=0.0, t_add=0.0, t_res=0.0, t_sweep1=0.0, t_sweep2=0.0, 
        t_cand=0.0, t_sort=0.0, t_transpose1=0.0, t_transpose2=0.0, t_selectrm=0.0,
        t_nrm=0.0, t_total = 0.0, accum=0.0;
                    
    float sum, sumL, sumU;

    magma_c_matrix hA={Magma_CSR}, hAT={Magma_CSR}, hL={Magma_CSR}, 
        hU={Magma_CSR}, oneL={Magma_CSR}, oneU={Magma_CSR},
        L={Magma_CSR}, U={Magma_CSR}, L_new={Magma_CSR}, U_new={Magma_CSR}, 
        UT={Magma_CSR}, L0={Magma_CSR}, U0={Magma_CSR};
    magma_int_t num_rmL, num_rmU;
    float thrsL = 0.0;
    float thrsU = 0.0;

    magma_int_t num_threads = 1, timing = 1; // print timing
    magma_int_t L0nnz, U0nnz;

    #pragma omp parallel
    {
        num_threads = omp_get_max_threads();
    }
    
    CHECK(magma_cmtransfer(A, &hA, A.memory_location, Magma_CPU, queue));
    
    // in case using fill-in
    if (precond->levels > 0) {
        CHECK(magma_csymbilu(&hA, precond->levels, &hL, &hU , queue));
        magma_cmfree(&hU, queue);
        magma_cmfree(&hL, queue);
    }
    CHECK(magma_cmatrix_tril(hA, &L0, queue));
    CHECK(magma_cmatrix_triu(hA, &U0, queue));
    magma_cmfree(&hU, queue);
    magma_cmfree(&hL, queue);
    CHECK(magma_cmatrix_tril(hA, &L, queue));
    CHECK(magma_cmtranspose(hA, &hAT, queue));
    CHECK(magma_cmatrix_tril(hAT, &U, queue));
    CHECK(magma_cmatrix_addrowindex(&L, queue)); 
    CHECK(magma_cmatrix_addrowindex(&U, queue)); 
    L0nnz=L.nnz;
    U0nnz=U.nnz;
    oneL.memory_location = Magma_CPU;
    oneU.memory_location = Magma_CPU;
        
    if (timing == 1) {
        printf("ilut_fill_ratio = %.6f;\n\n", precond->atol);  
        printf("performance_%d = [\n%%iter      L.nnz      U.nnz    ILU-Norm    transp    candidat  resid     sort    transcand    add      sweep1   selectrm    remove    sweep2     total       accum\n", 
            (int) num_threads);
    }

    //##########################################################################

    for (magma_int_t iters =0; iters<precond->sweeps; iters++) {
        t_rm=0.0; t_add=0.0; t_res=0.0; t_sweep1=0.0; t_sweep2=0.0; t_cand=0.0;
        t_transpose1=0.0; t_transpose2=0.0;  t_selectrm=0.0; t_sort = 0;
        t_sort=0.0; t_nrm=0.0; t_total = 0.0;
     
        // step 1: transpose U
        start = magma_sync_wtime(queue);
        magma_cmfree(&UT, queue);
        CHECK(magma_ccsrcoo_transpose(U, &UT, queue));
        end = magma_sync_wtime(queue); t_transpose1+=end-start;
        
        
        // step 2: find candidates
        start = magma_sync_wtime(queue);
        CHECK(magma_cparilut_candidates(L0, U0, L, UT, &hL, &hU, queue));
        end = magma_sync_wtime(queue); t_cand=+end-start;
        
        
        // step 3: compute residuals (optional when adding all candidates)
        start = magma_sync_wtime(queue);
        CHECK(magma_cparilut_residuals(hA, L, U, &hL, queue));
        CHECK(magma_cparilut_residuals(hA, L, U, &hU, queue));
        end = magma_sync_wtime(queue); t_res=+end-start;
        start = magma_sync_wtime(queue);
        CHECK(magma_cmatrix_abssum(hL, &sumL, queue));
        CHECK(magma_cmatrix_abssum(hU, &sumU, queue));
        sum = sumL + sumU;
        end = magma_sync_wtime(queue); t_nrm+=end-start;
        CHECK(magma_cmatrix_swap(&hL, &oneL, queue));
        magma_cmfree(&hL, queue);
        
        
        // step 4: sort candidates
        start = magma_sync_wtime(queue);
        CHECK(magma_ccsr_sort(&hL, queue));
        CHECK(magma_ccsr_sort(&hU, queue));
        end = magma_sync_wtime(queue); t_sort+=end-start;
        
        
        // step 5: transpose candidates
        start = magma_sync_wtime(queue);
        magma_ccsrcoo_transpose(hU, &oneU, queue);
        end = magma_sync_wtime(queue); t_transpose2+=end-start;
        
        
        // step 6: add candidates
        start = magma_sync_wtime(queue);
        CHECK(magma_cmatrix_cup(L, oneL, &L_new, queue));   
        CHECK(magma_cmatrix_cup(U, oneU, &U_new, queue));
        end = magma_sync_wtime(queue); t_add=+end-start;
        magma_cmfree(&oneL, queue);
        magma_cmfree(&oneU, queue);
       
        
        // step 7: sweep
        start = magma_sync_wtime(queue);
        CHECK(magma_cparilut_sweep_sync(&hA, &L_new, &U_new, queue));
        end = magma_sync_wtime(queue); t_sweep1+=end-start;
        
        
        // step 8: select threshold to remove elements
        start = magma_sync_wtime(queue);
        num_rmL = max((L_new.nnz-L0nnz*(1+(precond->atol-1.)
            *(iters+1)/precond->sweeps)), 0);
        num_rmU = max((U_new.nnz-U0nnz*(1+(precond->atol-1.)
            *(iters+1)/precond->sweeps)), 0);
        // pre-select: ignore the diagonal entries
        CHECK(magma_cparilut_preselect(0, &L_new, &oneL, queue));
        CHECK(magma_cparilut_preselect(0, &U_new, &oneU, queue));
        if (num_rmL>0) {
            CHECK(magma_cparilut_set_thrs_randomselect_approx(num_rmL, 
                &oneL, 0, &thrsL, queue));
        } else {
            thrsL = 0.0;
        }
        if (num_rmU>0) {
            CHECK(magma_cparilut_set_thrs_randomselect_approx(num_rmU, 
                &oneU, 0, &thrsU, queue));
        } else {
            thrsU = 0.0;
        }
        magma_cmfree(&oneL, queue);
        magma_cmfree(&oneU, queue);
        end = magma_sync_wtime(queue); t_selectrm=end-start;

        
        // step 9: remove elements
        start = magma_sync_wtime(queue);
        CHECK(magma_cparilut_thrsrm(1, &L_new, &thrsL, queue));
        CHECK(magma_cparilut_thrsrm(1, &U_new, &thrsU, queue));
        CHECK(magma_cmatrix_swap(&L_new, &L, queue));
        CHECK(magma_cmatrix_swap(&U_new, &U, queue));
        magma_cmfree(&L_new, queue);
        magma_cmfree(&U_new, queue);
        end = magma_sync_wtime(queue); t_rm=end-start;
        
        
        // step 10: sweep
        start = magma_sync_wtime(queue);
        CHECK(magma_cparilut_sweep_sync(&hA, &L, &U, queue));
        end = magma_sync_wtime(queue); t_sweep2+=end-start;
        
        if (timing == 1) {
            t_total = t_transpose1+ t_cand+ t_res+ t_sort+ t_transpose2+ t_add+ t_sweep1+ t_selectrm+ t_rm+ t_sweep2;
            accum = accum + t_total;
            printf("%5lld %10lld %10lld  %.4e   %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e      %.2e\n",
                (long long) iters, (long long) L.nnz, (long long) U.nnz, 
                (float) sum, 
                t_transpose1, t_cand, t_res, t_sort, t_transpose2, t_add, t_sweep1, t_selectrm, t_rm, t_sweep2, t_total, accum);
            fflush(stdout);
        }
    }

    if (timing == 1) {
        printf("]; \n");
        fflush(stdout);
    }
    //##########################################################################

    // for CUSPARSE
    CHECK(magma_cmtransfer(L, &precond->L, Magma_CPU, Magma_DEV , queue));
    CHECK(magma_ccsrcoo_transpose(U, &UT, queue));
    //magma_cmtranspose(U, &UT, queue);
    CHECK(magma_cmtransfer(UT, &precond->U, Magma_CPU, Magma_DEV , queue));
    
    if (precond->trisolver == 0 || precond->trisolver == Magma_CUSOLVE) {
        CHECK(magma_ccumilugeneratesolverinfo(precond, queue));
    } else {
        //prepare for iterative solves
        // extract the diagonal of L into precond->d
        CHECK(magma_cjacobisetup_diagscal(precond->L, &precond->d, queue));
        CHECK(magma_cvinit(&precond->work1, Magma_DEV, hA.num_rows, 1, 
            MAGMA_C_ZERO, queue));
        // extract the diagonal of U into precond->d2
        CHECK(magma_cjacobisetup_diagscal(precond->U, &precond->d2, queue));
        CHECK(magma_cvinit(&precond->work2, Magma_DEV, hA.num_rows, 1, 
            MAGMA_C_ZERO, queue));
    }

cleanup:
    magma_cmfree(&hA, queue);
    magma_cmfree(&hAT, queue);
    magma_cmfree(&L, queue);
    magma_cmfree(&U, queue);
    magma_cmfree(&UT, queue);
    magma_cmfree(&L0, queue);
    magma_cmfree(&U0, queue);
    magma_cmfree(&L_new, queue);
    magma_cmfree(&U_new, queue);
    magma_cmfree(&hL, queue);
    magma_cmfree(&hU, queue);
#endif
    return info;
}

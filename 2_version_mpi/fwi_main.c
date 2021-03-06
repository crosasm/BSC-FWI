/*
 * =====================================================================================
 *
 *       Filename:  fwi_main.c
 *
 *    Description:  Main file of the FWI mockup
 *
 *        Version:  1.0
 *        Created:  10/12/15 10:33:40
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (),
 *   Organization:
 *
 * =====================================================================================
 */
#include "fwi_kernel.h"


/*
 * In order to generate a source for injection,
 * /system/support/bscgeo/src/wavelet.c
 * functions can be used.
 */
void kernel( propagator_t propagator, real waveletFreq, int shotid, char* outputfolder, char* shotfolder)
{
    /* find ourselves into the MPI space */
    int mpi_rank, Subdomains;
    MPI_Comm_size( MPI_COMM_WORLD, &Subdomains);
    MPI_Comm_rank( MPI_COMM_WORLD, &mpi_rank);
    
    /* local variables */
    int stacki;
    double start_t, end_t;
    real dt,dz,dx,dy;
    integer dimmz, dimmx, dimmy, forw_steps, back_steps;

    load_shot_parameters( shotid, &stacki, &dt, &forw_steps, &back_steps, &dz, &dx, &dy, &dimmz, &dimmx, &dimmy, outputfolder, waveletFreq );

    /* number of cell for each MPI rank */
    const integer planesPerSubdomain = ((dimmy-2*HALO)/Subdomains);

    /* Compute local computing limits */
    const integer y0 = planesPerSubdomain * mpi_rank; 
  //const integer y0 = (dimmz) * (dimmx) * (planesPerSubdomain * localRank    );
    const integer yF = y0 + planesPerSubdomain + 2*HALO; 
  //const integer yF = (dimmz) * (dimmx) * (planesPerSubdomain * (localRank+1));

    const integer edimmy = (yF - y0);
    const integer numberOfCells = dimmz * dimmx * edimmy;


		fprintf(stderr, "number of cells in kernel() %d\n", numberOfCells);

    /* set GLOBAL integration limits */
    const integer nz0 = 0;
    const integer ny0 = 0;
    const integer nx0 = 0;
    const integer nzf = dimmz;
    const integer nxf = dimmx;
    const integer nyf = edimmy;

    real    *rho;
    v_t     v;
    s_t     s;
    coeff_t coeffs;

    print_debug("The length of local arrays is " I " cells", numberOfCells);

    /* allocate shot memory */
    alloc_memory_shot  ( numberOfCells, &coeffs, &s, &v, &rho);

    /* load initial model from a binary file */
    load_initial_model ( waveletFreq, dimmz, dimmx, edimmy, &coeffs, &s, &v, rho);

    /* Allocate memory for IO buffer */
    real* io_buffer = (real*) __malloc( ALIGN_REAL, numberOfCells * sizeof(real) * WRITTEN_FIELDS );

    /* inspects every array positions for leaks. Enabled when DEBUG flag is defined */
    check_memory_shot  ( numberOfCells, &coeffs, &s, &v, rho);

    print_debug( "MPI rank " I " compute from y=" I " to=" I ". #planes per subdomain " I " ", 
            mpi_rank, y0, yF, planesPerSubdomain);

    switch( propagator )
    {
    case( RTM_KERNEL ):
    {
        start_t = dtime();

        propagate_shot ( FORWARD,
                         v, s, coeffs, rho,
                         forw_steps, back_steps -1,
                         dt,dz,dx,dy,
                         nz0, nzf, nx0, nxf, ny0, nyf,
                         stacki,
                         shotfolder,
                         io_buffer,
                         numberOfCells,
                         dimmz, dimmx);

        end_t = dtime();

        print_stats("Forward propagation finished in %lf seconds", end_t - start_t );

        start_t = dtime();
        
        propagate_shot ( BACKWARD,
                         v, s, coeffs, rho,
                         forw_steps, back_steps -1,
                         dt,dz,dx,dy,
                         nz0, nzf, nx0, nxf, ny0, nyf,
                         stacki,
                         shotfolder,
                         io_buffer,
                         numberOfCells,
                         dimmz, dimmx);

        end_t = dtime();

        print_stats("Backward propagation finished in %lf seconds", end_t - start_t );

#ifdef DO_NOT_PERFORM_IO
        print_info("Warning: we are not creating gradient nor preconditioner "
                   "fields, because IO is not enabled for this execution" );
#else
        if ( mpi_rank == 0 ) 
        {
            char fnameGradient[300];
            char fnamePrecond[300];
            sprintf( fnameGradient, "%s/gradient_%05d.dat", shotfolder, shotid );
            sprintf( fnamePrecond , "%s/precond_%05d.dat" , shotfolder, shotid );

            FILE* fgradient = safe_fopen( fnameGradient, "ab", __FILE__, __LINE__ );
            FILE* fprecond  = safe_fopen( fnamePrecond , "ab", __FILE__, __LINE__ );

            print_info("Storing local gradient field in %s", fnameGradient );
            safe_fwrite( io_buffer, sizeof(real), numberOfCells * WRITTEN_FIELDS, fgradient, __FILE__, __LINE__ );

            print_info("Storing local preconditioner field in %s", fnamePrecond);
            safe_fwrite( io_buffer, sizeof(real), numberOfCells * WRITTEN_FIELDS, fprecond , __FILE__, __LINE__ );

            safe_fclose( fnameGradient, fgradient, __FILE__, __LINE__ );
            safe_fclose( fnamePrecond , fprecond , __FILE__, __LINE__ );
        }
#endif

        break;
    }
    case( FM_KERNEL  ):
    {
        start_t = dtime();

        propagate_shot ( FWMODEL,
                         v, s, coeffs, rho,
                         forw_steps, back_steps -1,
                         dt,dz,dx,dy,
                         nz0, nzf, nx0, nxf, ny0, nyf,
                         stacki,
                         shotfolder,
                         io_buffer,
                         numberOfCells,
                         dimmz, dimmx);

        end_t = dtime();

        print_stats("Forward Modelling finished in %lf seconds", end_t - start_t );
       
        break;
    }
    default:
    {
        print_error("Invalid propagation identifier");
        abort();
    }
    } /* end case */

    // liberamos la memoria alocatada en el shot
    free_memory_shot  ( &coeffs, &s, &v, &rho);
    __free( io_buffer );
};

int main(int argc, char* argv[])
{
    double tstart, tend;
    tstart = dtime();

    MPI_Init ( &argc, &argv );
    int mpi_rank, subdomains;
    MPI_Comm_size( MPI_COMM_WORLD, &subdomains);
    MPI_Comm_rank( MPI_COMM_WORLD, &mpi_rank);

    real lenz,lenx,leny,vmin,srclen,rcvlen;
    char outputfolder[200];

    read_fwi_parameters( argv[1], &lenz, &lenx, &leny, &vmin, &srclen, &rcvlen, outputfolder);

    const int nshots = 1;
    const int ngrads = 1;
    const int ntest  = 0;

    int   nfreqs;
    real *frequencies;

    load_freqlist( argv[2], &nfreqs, &frequencies );

    for(int i=0; i<nfreqs; i++)
    {
        /* Process one frequency at a time */
        real waveletFreq = frequencies[i];
        fprintf(stderr, "Freq: %2.1f ------------------------\n", waveletFreq); 

        /* Deltas of space, 16 grid point per Hz */
        real dx = vmin / (16.0 * waveletFreq);
        real dy = vmin / (16.0 * waveletFreq);
        real dz = vmin / (16.0 * waveletFreq);

        /* number of cells along axis, adding HALO planes */
        integer dimmz = roundup(ceil( lenz / dz ) + 2*HALO, HALO);
        integer dimmy = roundup(ceil( leny / dy ) + 2*HALO, HALO);
        integer dimmx = roundup(ceil( lenx / dx ) + 2*HALO, HALO);

        /* compute delta T */
        real dt = 68e-6 * dx;

        /* dynamic I/O */
        integer stacki = floor( 0.25 / (2.5 * waveletFreq * dt) );

        const integer numberOfCells = dimmz * dimmx * dimmx;
        const size_t VolumeMemory  = numberOfCells * sizeof(real) * 58;

        print_stats("Local domain size for freq %f [%d][%d][%d] is %lu bytes (%lf GB)", 
                    waveletFreq, dimmz, dimmx, dimmy, VolumeMemory, TOGB(VolumeMemory) );

        /* compute time steps */
        int forw_steps = max_int ( IT_FACTOR * (srclen/dt), 1);
        int back_steps = max_int ( IT_FACTOR * (rcvlen/dt), 1);

        for(int grad=0; grad<ngrads; grad++) /* iteracion de inversion */
        {
            fprintf(stderr, "Processing %d-th gradient iteration.\n", grad);
            print_info("Processing %d-gradient iteration", grad);

            for(int shot=0; shot<nshots; shot++)
            {
                char shotfolder[200];
                sprintf(shotfolder, "%s/shot.%2.1f.%05d", outputfolder, waveletFreq, shot);
                
                if ( mpi_rank == 0 ) 
                {
                    create_folder( shotfolder );

                    store_shot_parameters( shot, &stacki, &dt, &forw_steps, &back_steps,
                                           &dz, &dx, &dy, 
                                           &dimmz, &dimmx, &dimmy, 
                                           outputfolder, waveletFreq );
                }

                MPI_Barrier( MPI_COMM_WORLD );

                kernel( RTM_KERNEL, waveletFreq, shot, outputfolder, shotfolder);

                fprintf(stderr, "\tGradient loop processed for the %d-th shot\n", shot);
                print_info("\tGradient loop processed for %d-th shot", shot);
                
                //update_shot()
            }

            MPI_Barrier( MPI_COMM_WORLD );
            
						
						for(int test=0; test<ntest; test++)
            {
                fprintf(stderr, "\tProcessing %d-th test iteration.\n", test);
                print_info("\tProcessing %d-th test iteration", test);
                
                for(int shot=0; shot<nshots; shot++)
                {
                    char shotfolder[200];
                    sprintf(shotfolder, "%s/test.%05d.shot.%2.1f.%05d", 
                            outputfolder, test, waveletFreq, shot);

                    if ( mpi_rank == 0)
                    {
                        create_folder( shotfolder );    

                        store_shot_parameters( shot, &stacki, &dt, &forw_steps, &back_steps, 
                                               &dz, &dx, &dy, 
                                               &dimmz, &dimmx, &dimmy, 
                                               outputfolder, waveletFreq );
                    }

                    MPI_Barrier( MPI_COMM_WORLD );

                    kernel( FM_KERNEL , waveletFreq, shot, outputfolder, shotfolder);
                
                    fprintf(stderr, "\t\tTest loop processed for the %d-th shot\n", shot);
                    print_info("\t\tTest loop processed for the %d-th shot", shot);
                }
            } /* end of test loop */
        } /* end of gradient loop */
    } /* end of frequency loop */

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();

    tend = dtime() - tstart;

    fprintf(stderr, "FWI Program finished in %lf seconds\n", tend);

    return 0;
}

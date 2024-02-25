/***
 * MRHS solver interface
 * See: Håvard Raddum and Pavol Zajac
 *      MRHS Solver Based on Linear Algebra and Exhaustive Search
 */
#include <stdlib.h>
#include <memory.h>
#include <math.h>

#include "mrhs.bm.h"
#include "mrhs.bv.h"
#include "mrhs.h"
#include "mrhs.solver.h"


/// n, m, (l, k) - fixed
MRHS_system create_mrhs_fixed(int nrows, int nblocks, int blocksize, int rhscount)
{
	int *blocksizes = malloc(nblocks * sizeof(int));
	int* rhscounts = malloc(nblocks * sizeof(int));

	for (int block = 0; block < nblocks; block++)
	{
		blocksizes[block] = blocksize;
		rhscounts[block]  = rhscount;
	}

	return create_mrhs_variable(nrows, nblocks, blocksizes, rhscounts);
}

/// n, m, (l, k) as lists
MRHS_system create_mrhs_variable(int nrows, int nblocks, int blocksizes[], int rhscounts[])
{
	MRHS_system system;

	if (nblocks == 0)
	{
		system.nblocks = 0;
		system.pM      = NULL;
		system.pS      = NULL;
		return system;
	}

	//else, non-zero blocks
	system.nblocks = nblocks;
	system.pM      = calloc(nblocks, sizeof(_bm));
	system.pS      = calloc(nblocks, sizeof(_bm));
	for (int block = 0; block < system.nblocks; block++)
	{
		system.pM[block] = create_bm(nrows, blocksizes[block]);
		system.pS[block] = create_bm(rhscounts[block], blocksizes[block]);
	}
	return system;
}

/// release memory allocated for MRHS system
void clear_MRHS(MRHS_system *psystem)
{
	//if (psystem->nblocks == 0)	//ASSERT ...
	//	return;

	for (int block = 0; block < psystem->nblocks; block++)
	{
		clear_bm(&psystem->pM[block]);
		clear_bm(&psystem->pS[block]);
	}
	if (psystem->pM != NULL)
        free(psystem->pM);
	if (psystem->pS != NULL)
        free(psystem->pS);

	psystem->nblocks = 0;
	psystem->pM      = NULL;
	psystem->pS      = NULL;
}

/// ///////////////////////////////////////////////////////////////////

/// Fill MRHS system with random data
void fill_mrhs_random(MRHS_system *psystem)
{
	for (int block = 0; block < psystem->nblocks; block++)
	{
		random_bm(&psystem->pM[block]);
		random_unique_bm(&psystem->pS[block]);
	}
}

/// Fill MRHS system with random data,
///   single one in each linearly independent column
void fill_mrhs_random_sparse(MRHS_system *psystem)
{
	for (int block = 0; block < psystem->nblocks; block++)
	{
		random_sparse_cols_bm(&psystem->pM[block]);
		random_unique_bm(&psystem->pS[block]);
	}
}

/// Fill MRHS system with "AND" PRNG data,
///   block structure, AND gates + constants
///     m -> number of blocks (system->nblocks)
///     IN: k -> key variables
///     IN: l -> filter variables
///    first m - l equations are AND blocks with output variables
///    last l equations are filters
///   PRE: ncols in each block == 3, rhs in each block == 4
///   PRE: 0 <= l <= nblocks
///   PRE: nrows == k+m-l 
void fill_mrhs_and(MRHS_system *psystem, int k, int l)
{
    int m = psystem->nblocks;
    if (l > m || l < 0 || k + m - l != psystem->pM->nrows)
        return;
    
	for (int block = 0; block < m-l; block++)
	{
		random_and_cols_bm(&psystem->pM[block], k+block);
		random_and_bm(&psystem->pS[block]);
	}
	for (int block = m-l; block < m; block++)
	{
		random_bm(&psystem->pM[block]);
		random_and_bm(&psystem->pS[block]);
	}
}

/// Fill MRHS system with "AND" PRNG data,
///   block structure, AND gates + constants, sparse version
///     m -> number of blocks (system->nblocks)
///     IN: k -> key variables
///     IN: l -> filter variables
///    first m - l equations are AND blocks with output variables
///    last l equations are filters
///   PRE: ncols in each block == 3, rhs in each block == 4
///   PRE: 0 <= l <= nblocks
///   PRE: nrows == k+m-l 
void fill_mrhs_and_sparse(MRHS_system *psystem, int k, int l, int density)
{
    int m = psystem->nblocks;
    if (l > m || l < 0 || k + m - l != psystem->pM->nrows)
        return;
    
	for (int block = 0; block < psystem->nblocks; block++)
	{
		random_sparse_and_cols_bm(&psystem->pM[block], k+block, density);
		random_and_bm(&psystem->pS[block]);
	}
}


/// Fill MRHS system with random data
///  M is sparse -> one 1 in each column + density number of ones
void fill_mrhs_random_sparse_extra(MRHS_system *psystem, int density)
{
	fill_mrhs_random_sparse(psystem);
	for (int i = 0; i < density; i++)
    {
		int block = rand() % psystem->nblocks;
		int row   = rand() % psystem->pM[block].nrows;
		int col   = rand() % psystem->pM[block].ncols;
		set_one_bm(&psystem->pM[block], row, col);
	}
}

/// Change RHS to ensure system has at least one random solution
void ensure_random_solution(MRHS_system *psystem)
{
    if (psystem->nblocks < 1)
        return;

    _bv sol = create_bv(psystem->pM[0].nrows);
    random_bv(&sol);

    for (int block = 0; block < psystem->nblocks; block++)
    {
        //multiply block by sol
        _block rhs = multiply_bv_x_bm(&sol, &psystem->pM[block]);

        //replace rhs if needed
        ensure_block_in_bm(&psystem->pS[block], rhs);
    }

}

/// I/O

/// deserialize system
MRHS_system read_mrhs_variable(FILE *f)
{
	MRHS_system system;
	char c;
	int row, block, n = 0, m = 0;
	int *k = NULL, *l = NULL;

	//read header
	if ( fscanf(f, "%i", &n) > 0 && fscanf(f, "%i", &m) > 0 )
	{
        k = (int*)calloc(m, sizeof(int));
        l = (int*)calloc(m, sizeof(int));
        for (block = 0; block < m; block++)
        {
         //TODO: check sizes
         fscanf(f, "%i", &l[block]);
         fscanf(f, "%i", &k[block]);
        }
	}


	//prepare system
	system = create_mrhs_variable(n, m, l, k);

	//no longer needed
	if (l != NULL) free(l);
    if (k != NULL) free(k);

	//read rows of M
	for (row = 0; row < n; row++)
	{
		// skip until beggining of row is found
		while (fscanf(f, "%c", &c) && c != '[') { /* skip */ }

		for (block = 0; block < system.nblocks; block++)
		{
			read_block_bm(f, system.pM[block], row);
		}
		//remove terminator
		fscanf(f, "]\n");
	}

	//read blocks of S
	for (block = 0; block < system.nblocks; block++)
	{
		for (row = 0; row < system.pS[block].nrows; row++)
		{
			// skip until beggining of row is found
			while (fscanf(f, "%c", &c) && c != '[') { /* skip */ }

			read_block_bm(f, system.pS[block], row);

			//remove terminator
			fscanf(f, "]\n");
		}
	}

	return system;
}

/// serialize system
int write_mrhs_variable(FILE *f, MRHS_system system)
{
	int row, block, nrows;
	int sum = 0;

	if (system.nblocks == 0) return 0;

	//number of lines for M
	nrows = system.pM[0].nrows;

	//print header
	sum += fprintf(f, "%i %i\n", nrows, system.nblocks);
	for (block = 0; block < system.nblocks; block++)
	{
		sum += fprintf(f, "%i %i\n", system.pS[block].ncols, system.pS[block].nrows);
	}

	//print matrix
	for (row = 0; row < nrows; row++)
	{
		sum += fprintf(f, "[ ");
		for (block = 0; block < system.nblocks; block++)
		{
			sum += print_block_bm(f, system.pM[block], row);
			fprintf(f, " ");
		}
		sum += fprintf(f, "]\n");
	}

	// print RHS part
	for (block = 0; block < system.nblocks; block++)
	{
		sum += fprintf(f, "\n");
		for (row = 0; row < system.pS[block].nrows; row++)
		{
			sum += fprintf(f, "[");
			sum += print_block_bm(f, system.pS[block], row);
			sum += fprintf(f, "]\n");
		}
	}
	return sum;
}

///print block matrix in user readable form
int print_mrhs(FILE *f, MRHS_system system)
{
	int row, block, nrows, maxrhs;
	int sum = 0;

	if (system.nblocks == 0) return 0;

	//number of lines for M
	nrows = system.pM[0].nrows;

	//max number of lines for S
	maxrhs = system.pS[0].nrows;
	for (block = 1; block < system.nblocks; block++)
	{
		if (system.pS[block].nrows > maxrhs)
		{
			maxrhs = system.pS[block].nrows;
		}
	}

	//print matrix
	for (row = 0; row < nrows; row++)
	{
		for (block = 0; block < system.nblocks; block++)
		{
			sum += print_block_bm(f, system.pM[block], row);
			sum += fprintf(f, " ");
		}
		sum += fprintf(f, "\n");
	}

	//separator
	for (block = 0; block < system.nblocks; block++)
	{
		for (int bit = 0; bit < system.pM[block].ncols; bit++)
		{
			sum += fprintf(f, "-");
		}
		sum += fprintf(f, " ");
	}
	sum += fprintf(f, "\n");

	// print RHS part
	for (row = 0; row < maxrhs; row++)
	{
		for (block = 0; block < system.nblocks; block++)
		{
			// no more rhs in given S
			if (row >= system.pS[block].nrows)
			{
				sum += fprintf(f, "%*c", system.pS[block].ncols+1, ' ');
				continue;
			}

			sum += print_block_bm(f, system.pS[block], row);
			fprintf(f, " ");
		}
		sum += fprintf(f, "\n");
	}
	return sum;
}

int print_bbm(FILE* f, _bbm* system, char rhs)
{
	int row, block, nrows, maxrhs;
	int sum = 0;

	if (system->nblocks == 0) return 0;

	if (rhs == 0)
	{

		//number of lines for M
		nrows = system->nrows;

		//print matrix
		for (row = 0; row < nrows; row++)
		{
			for (block = 0; block < system->nblocks; block++)
			{
				sum += print_block_bbm(f, system, row, block);
				sum += fprintf(f, " ");
			}
			sum += fprintf(f, "\n");
		}

		//separator
		for (block = 0; block < system->nblocks; block++)
		{
			for (int bit = 0; bit < system->ncols; bit++)
			{
				sum += fprintf(f, "-");
			}
			sum += fprintf(f, " ");
		}
		sum += fprintf(f, "\n");
	}
	else {
		maxrhs = system[0].nrows;
		for (block = 1; block < system[0].nblocks; block++)
		{
			if (system[block].nrows > maxrhs)
			{
				maxrhs = system[block].nrows;
			}
		}

		// print RHS part
		for (row = 0; row < maxrhs; row++)
		{
			for (block = 0; block < system[0].nblocks; block++)
			{
				// no more rhs in given S
				if (row >= system[block].nrows)
				{
					sum += fprintf(f, "%*c", system[block].ncols + 1, ' ');
					continue;
				}

				sum += print_block_bbm(f, system, row, block);
				fprintf(f, " ");
			}
			sum += fprintf(f, "\n");
		}
	}
	return sum;
}


/// Linear algebra

///substitute given linear equation into system
int linear_substitution(MRHS_system *system, _bv *column, _block rhs)
{
    int count = 0;
    int pivot = find_nonzero(column, 0);
    if (pivot < 0)
        return count;

    for (int block = 0; block < system->nblocks; block++)
    {
        for (int col = 0; col < system->pM[block].ncols; col++)
        {
            if (get_bit_bm(&system->pM[block],pivot,col) == ONE)
            {
                //add column to bm
                add_column_bm(&system->pM[block], column, col);

                //add constant to
                add_constant_bm(&system->pS[block], rhs, col);
                count++;
            }
        }
    }
    return count;
}

///remove linear equations from the system
int remove_linear(MRHS_system *system)
{
   int count = 0;
   for (int block = 0; block < system->nblocks; block++)
   {
        if (system->pS[block].nrows == 1)
        {
            for (int col = 0; col < system->pM[block].ncols; col++)
            {
                _bv column = get_column_bm(&system->pM[block], col);
                _block rhs = get_bit_bm(&system->pS[block], 0, col);
                count += linear_substitution(system, &column, rhs);
                clear_bv(&column);
            }
        }
   }
   return count;
}
///remove linear equations from the system
int remove_empty(MRHS_system *system)
{
    int numblocks = system->nblocks;
   _bv active_rows = create_bv(system->pM->nrows);

   for (int block = 0; block < system->nblocks; block++)
   {
        _bv active = get_active_rows_bm(&system->pM[block]);
        if (is_non_zero_bv(&active))
        {
            or_bv(&active_rows, &active);
        }
        else
        {
            //remove whole block
            clear_bm(&system->pM[block]);
            clear_bm(&system->pS[block]);
            for (int nb = block+1; nb < system->nblocks; nb++)
            {
                system->pM[nb-1] = system->pM[nb];
                system->pS[nb-1] = system->pS[nb];
            }
            block--;
            system->nblocks--;
        }
        clear_bv(&active);
   }
   for (int block = 0; block < system->nblocks; block++)
   {
        remove_rows_bm(&system->pM[block], &active_rows);
   }
   clear_bv(&active_rows);
   return numblocks - system->nblocks;
}



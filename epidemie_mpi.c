#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef N
#define N 300
#endif

#ifndef ITER
#define ITER 200
#endif

#define IDX(i,j) ((i)*N + (j))

int main(int argc, char *argv[])
{
    int rang;
    int nbproc;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rang);
    MPI_Comm_size(MPI_COMM_WORLD, &nbproc);


    if (N % nbproc != 0) {
        if (rang == 0) {
            printf("Erreur : N doit etre divisible par le nombre de processus\n");
        }
        MPI_Finalize();
        return 1;
    }

    int bloc = N / nbproc;
    int debut = rang * bloc;

    /*
     * Les lignes 1 a bloc sont les lignes traitees par le processus.
     * Les lignes 0 et bloc + 1 servent a recevoir les frontieres.
     */
    int *A = malloc((size_t)(bloc + 2) * N * sizeof(int));
    int *B = malloc((size_t)(bloc + 2) * N * sizeof(int));

    if (A == NULL || B == NULL) {
        printf("Erreur d'allocation sur le processus %d\n", rang);
        free(A);
        free(B);
        MPI_Finalize();
        return 1;
    }

    for (int i = 0; i < bloc + 2; i++) {
        for (int j = 0; j < N; j++) {
            A[IDX(i,j)] = 0;
            B[IDX(i,j)] = 0;
        }
    }

    /* On place les trois personnes infectees dans le bon bloc. */
    int lignes_infectees[3] = {N / 2, N / 2, N / 2 + 1};
    int colonnes_infectees[3] = {N / 2, N / 2 + 1, N / 2};

    for (int k = 0; k < 3; k++) {
        if (lignes_infectees[k] >= debut &&
            lignes_infectees[k] < debut + bloc) {
            int ligne_locale = lignes_infectees[k] - debut + 1;
            A[IDX(ligne_locale, colonnes_infectees[k])] = 1;
        }
    }

    clock_t t0 = clock();

    for (int it = 0; it < ITER; it++) {
        /*
         * Premier echange : processus 0 avec 1, processus 2 avec 3, etc.
         * L'ordre pair/impair evite que deux processus attendent en meme temps.
         */
        if (rang % 2 == 0) {
            if (rang < nbproc - 1) {
                MPI_Send(&A[IDX(bloc,0)], N, MPI_INT, rang + 1, 0,
                         MPI_COMM_WORLD);
                MPI_Recv(&A[IDX(bloc + 1,0)], N, MPI_INT, rang + 1, 1,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        } else {
            MPI_Recv(&A[IDX(0,0)], N, MPI_INT, rang - 1, 0,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send(&A[IDX(1,0)], N, MPI_INT, rang - 1, 1,
                     MPI_COMM_WORLD);
        }

        /* Deuxieme echange : processus 1 avec 2, processus 3 avec 4, etc. */
        if (rang % 2 == 1) {
            if (rang < nbproc - 1) {
                MPI_Send(&A[IDX(bloc,0)], N, MPI_INT, rang + 1, 2,
                         MPI_COMM_WORLD);
                MPI_Recv(&A[IDX(bloc + 1,0)], N, MPI_INT, rang + 1, 3,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        } else if (rang > 0) {
            MPI_Recv(&A[IDX(0,0)], N, MPI_INT, rang - 1, 2,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send(&A[IDX(1,0)], N, MPI_INT, rang - 1, 3,
                     MPI_COMM_WORLD);
        }

        for (int i = 1; i <= bloc; i++) {
            int ligne_globale = debut + i - 1;

            if (ligne_globale != 0 && ligne_globale != N - 1) {
                for (int j = 1; j < N - 1; j++) {
                    int v = A[IDX(i,j)];

                    if (v == 0) {
                        int voisins_inf = 0;
                        voisins_inf += (A[IDX(i-1,j)] == 1);
                        voisins_inf += (A[IDX(i+1,j)] == 1);
                        voisins_inf += (A[IDX(i,j-1)] == 1);
                        voisins_inf += (A[IDX(i,j+1)] == 1);
                        B[IDX(i,j)] = (voisins_inf >= 2) ? 1 : 0;
                    } else if (v == 1) {
                        B[IDX(i,j)] = 2;
                    } else {
                        B[IDX(i,j)] = 2;
                    }
                }
            }
        }

        int *tmp = A;
        A = B;
        B = tmp;
    }

    clock_t t1 = clock();
    double temps_local = (double)(t1 - t0) / CLOCKS_PER_SEC;
    double somme_temps = 0.0;

    MPI_Reduce(&temps_local, &somme_temps, 1, MPI_DOUBLE, MPI_SUM, 0,
               MPI_COMM_WORLD);

    int compteurs_locaux[3] = {0, 0, 0};
    int compteurs_globaux[3] = {0, 0, 0};

    for (int i = 1; i <= bloc; i++) {
        for (int j = 0; j < N; j++) {
            if (A[IDX(i,j)] == 0) {
                compteurs_locaux[0]++;
            } else if (A[IDX(i,j)] == 1) {
                compteurs_locaux[1]++;
            } else {
                compteurs_locaux[2]++;
            }
        }
    }

    MPI_Reduce(compteurs_locaux, compteurs_globaux, 3, MPI_INT, MPI_SUM, 0,
               MPI_COMM_WORLD);

    if (rang == 0) {
        printf("Propagation simplifiee d'une epidemie avec MPI\n");
        printf("N=%d ITER=%d processus=%d\n", N, ITER, nbproc);
        printf("Temps moyen = %.6f s\n", somme_temps / nbproc);
        printf("sains=%d infectes=%d gueris=%d\n",
               compteurs_globaux[0], compteurs_globaux[1],
               compteurs_globaux[2]);
    }

    free(A);
    free(B);
    MPI_Finalize();
    return 0;
}

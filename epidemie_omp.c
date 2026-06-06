#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>

#ifndef N
#define N 8312 
/* #define N 8312 pour avoir des chiffres semblables à la population française.
 * Pour avoir des temps plus pratiques à tester, on prendre N = 2000
 */
#endif

#ifndef ITER
#define ITER 500
#endif

#define IDX(i,j) ((i)*N + (j))

static double wtime(void) {
    return omp_get_wtime();
}

/* Données du programme :
 * On travaille avec un tableau 1D qui fonctionne comme un tableau 2D grâce à IDX(a,b).
 * Le tableau A est celui qui est lu, le tableau B est celui qui est rempli
 * N --> Dimensions de la grille ie racine carrée du nombre d'individus
 * ITER --> Nombre d'itérations ie le nombre de fois que les valeurs du tableau sont recalculées
 * IDX --> Index de l'individu dans un tableau 1D 
 */

int main(void) {
    // Nombre de threads à utiliser
    omp_set_num_threads(8);

    // Allocation des tableaux
    int *A = malloc((size_t)N*N*sizeof(int));
    int *B = malloc((size_t)N*N*sizeof(int));
    if(!A || !B) {
        fprintf(stderr, "Erreur allocation\n");
        free(A); free(B);
        return 1;
    }

    // Boucle de remplissage des tableaux
    #pragma omp parallel for
    for(int i=0;i<N;i++) {
        for(int j=0;j<N;j++) {
            A[IDX(i,j)] = 0;
            B[IDX(i,j)] = 0;
        }
    }

    // On met 3 individus infectés aux indices 45150 (150,150), 45151 (150,151), 45450 (151,150)
    A[IDX(N/2,N/2)] = 1;
    A[IDX(N/2,N/2+1)] = 1;
    A[IDX(N/2+1,N/2)] = 1;

    // Début de la prise de temps
    double t0 = wtime();

    // Boucle principale de traitement
    // On boucle ITER fois
    for(int it=0; it<ITER; it++) {
        // On boucle sur une dimension du tableau ie N fois
        #pragma omp parallel for
        for(int i=1;i<N-1;i++) {
            // On boucle sur l'autre dimension du tableau ie N fois
            for(int j=1;j<N-1;j++) {
                int v = A[IDX(i,j)];
                // Si la personne courante est saine
                if(v == 0) {
                    int voisins_inf = 0;
                    voisins_inf += (A[IDX(i-1,j)] == 1);
                    voisins_inf += (A[IDX(i+1,j)] == 1);
                    voisins_inf += (A[IDX(i,j-1)] == 1);
                    voisins_inf += (A[IDX(i,j+1)] == 1);
                    B[IDX(i,j)] = (voisins_inf >= 2) ? 1 : 0;
                
                // Si la personne courante est infectée
                } else if(v == 1) {
                    B[IDX(i,j)] = 2;
                
                // Si la personne courante est guérie
                } else {
                    B[IDX(i,j)] = 2;
                }
            }
        }

        // On inverse A et B pour que le tableau A soit le tableau final
        int *tmp = A;
        A = B;
        B = tmp;
    }

    // Fin de la prise de temps
    double t1 = wtime();

    int c0=0, c1=0, c2=0;

    // Première dimension du comptage des individus
    #pragma omp parallel for reduction(+:c0,c1,c2)
    for(int i=0;i<N;i++) {

        // Deuxième dimension du parcours
        /* #pragma omp parallel for
         * En conservant la directive ci-dessus, on garde des temps d'exécution équivalents
         * On décide quand même de la retirer car à priori, la parallélisation
         * supplémentaire devrait rajouter du temps d'exécution
         */
        for(int j=0;j<N;j++) {
            if(A[IDX(i,j)] == 0) c0++;
            else if(A[IDX(i,j)] == 1) c1++;
            else c2++;
        }
    }

    // Résultats
    printf("Propagation simplifiée d'une épidémie\n");
    printf("N=%d ITER=%d\n", N, ITER);
    printf("Temps = %.6f s\n", t1-t0);
    printf("sains=%d infectes=%d gueris=%d\n", c0, c1, c2);

    free(A);
    free(B);
    return 0;
}

/* 
Résultats moyens à mettre dans le rapport (pour N = 2000 et ITER = 500):
Avec 1 thread : 9,333s
Avec 2 threads : 4,998s
Avec 4 threads : 6,076s
Avec 8 threads : 3,003s

s(1) = 1
s(2) = 1,867
s(4) = 1,536
s(8) = 3,108

Pour 69 089 340 personnes et 500 itérations (environ la population française) : 
Avec 1 thread : 2min 33s
Avec 2 threads : 1min 30s
Avec 4 threads : 1min 17s
Avec 8 threads : 48s

s(1) = 1
s(2) = 1,7
s(4) = 1,99
s(8) = 3,19
*/

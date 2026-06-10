# Parallélisation de la simulation d'épidémie avec MPI

## 1. Principe de la version séquentielle

Dans `epidemie_seq.c`, un seul processus possède toute la grille de taille
`N x N`.

À chaque itération, il parcourt toutes les cellules internes de la grille :

```c
for (int i = 1; i < N - 1; i++) {
    for (int j = 1; j < N - 1; j++) {
        /* Calcul du nouvel état de la cellule (i, j). */
    }
}
```

Le tableau `A` contient l'état actuel de la population et le tableau `B`
reçoit l'état suivant. À la fin d'une itération, les deux pointeurs sont
échangés :

```c
int *tmp = A;
A = B;
B = tmp;
```

Ainsi, toutes les lignes sont calculées les unes après les autres par le même
processus.

## 2. Découpage de la grille dans la version MPI

Dans `epidemie_mpi.c`, les lignes de la grille sont réparties entre plusieurs
processus MPI.

```c
int bloc = N / nbproc;
int debut = rang * bloc;
```

- `nbproc` est le nombre total de processus ;
- `rang` est le numéro du processus courant ;
- `bloc` est le nombre de lignes gérées par chaque processus ;
- `debut` est l'indice global de la première ligne gérée par le processus.

Par exemple, pour `N = 300` et quatre processus :

| Processus | Lignes globales traitées |
|---|---:|
| rang 0 | 0 à 74 |
| rang 1 | 75 à 149 |
| rang 2 | 150 à 224 |
| rang 3 | 225 à 299 |

Cette version impose que `N` soit divisible par le nombre de processus :

```c
if (N % nbproc != 0) {
    /* Arrêt du programme. */
}
```

Il n'y a pas de `MPI_Scatter` au début du programme : chaque processus alloue
et initialise directement son propre morceau de grille.

## 3. Lignes fantômes

Pour calculer l'état d'une cellule, il faut connaître ses quatre voisins :
haut, bas, gauche et droite. Les voisins horizontaux appartiennent toujours au
même processus, mais un voisin vertical peut appartenir au processus précédent
ou suivant.

Chaque processus alloue donc `bloc + 2` lignes :

```c
int *A = malloc((size_t)(bloc + 2) * N * sizeof(int));
int *B = malloc((size_t)(bloc + 2) * N * sizeof(int));
```

L'organisation locale est la suivante :

```text
ligne locale 0          : ligne fantôme reçue du processus précédent
lignes locales 1..bloc : lignes réellement traitées
ligne locale bloc + 1   : ligne fantôme reçue du processus suivant
```

Les lignes fantômes sont aussi appelées *halos*. Elles permettent de conserver
la même formule de calcul que dans la version séquentielle :

```c
A[IDX(i-1,j)]  /* voisin du haut */
A[IDX(i+1,j)]  /* voisin du bas */
A[IDX(i,j-1)]  /* voisin de gauche */
A[IDX(i,j+1)]  /* voisin de droite */
```

## 4. Échange des frontières

Avant chaque étape de calcul, les processus voisins s'échangent leurs lignes
de bord.

Un processus envoie :

- sa première ligne réelle au processus précédent ;
- sa dernière ligne réelle au processus suivant.

Il reçoit ensuite les lignes correspondantes dans ses deux lignes fantômes.

Dans le programme, les communications sont réalisées avec `MPI_Send` et
`MPI_Recv`. Elles sont séparées en deux phases :

1. échanges entre les processus `0-1`, `2-3`, etc. ;
2. échanges entre les processus `1-2`, `3-4`, etc.

L'alternance entre rangs pairs et impairs évite que deux processus exécutent
simultanément un `MPI_Send` bloquant en attendant l'autre. Les étiquettes
MPI `0`, `1`, `2` et `3` permettent de distinguer les différents messages.

Exemple avec quatre processus :

```text
Phase 1 : [P0 <-> P1]   [P2 <-> P3]
Phase 2 :       [P1 <-> P2]
```

Ces échanges sont nécessaires à chaque itération, car l'état des cellules
change au cours de la simulation.

## 5. Calcul effectué en parallèle

Une fois les lignes fantômes reçues, chaque processus calcule uniquement ses
propres lignes :

```c
for (int i = 1; i <= bloc; i++) {
    int ligne_globale = debut + i - 1;

    if (ligne_globale != 0 && ligne_globale != N - 1) {
        for (int j = 1; j < N - 1; j++) {
            /* Calcul du nouvel état. */
        }
    }
}
```

Tous les processus exécutent cette boucle au même moment sur des parties
différentes de la grille. C'est cette répartition du travail qui constitue la
parallélisation principale.

La variable `ligne_globale` permet de reconnaître les véritables bords de la
grille. Les lignes globales `0` et `N - 1` ne sont pas modifiées, comme dans la
version séquentielle.

Après le calcul, chaque processus échange localement les pointeurs `A` et `B`.
Il n'est pas nécessaire de rassembler toute la grille entre deux itérations.

## 6. Placement des personnes infectées

Dans la version séquentielle, les trois personnes infectées sont directement
placées dans la grille complète.

Dans la version MPI, chaque processus vérifie si leurs coordonnées globales
appartiennent à son bloc :

```c
if (ligne_globale >= debut && ligne_globale < debut + bloc) {
    int ligne_locale = ligne_globale - debut + 1;
    A[IDX(ligne_locale, colonne)] = 1;
}
```

Le `+ 1` vient de la ligne fantôme située à l'indice local `0`.

## 7. Regroupement des résultats

À la fin, chaque processus compte les personnes saines, infectées et guéries
dans son bloc. `MPI_Reduce` additionne ces compteurs sur le processus de rang
zéro :

```c
MPI_Reduce(compteurs_locaux, compteurs_globaux, 3, MPI_INT,
           MPI_SUM, 0, MPI_COMM_WORLD);
```

Le même principe est utilisé pour additionner les temps locaux. Seul le
processus de rang zéro affiche le résultat final afin d'éviter un affichage
identique par tous les processus.

## 8. Comparaison des deux versions

| Élément | Version séquentielle | Version MPI |
|---|---|---|
| Nombre de processus | 1 | Plusieurs |
| Grille détenue | Grille entière | Bloc de lignes |
| Taille locale | `N x N` | `(bloc + 2) x N` |
| Calcul | Toutes les cellules | Seulement les cellules du bloc |
| Communication | Aucune | Échange des lignes frontières |
| Mémoire | Toute la grille sur un processus | Grille répartie entre les processus |
| Initialisation | Directe dans la grille complète | Directe dans le processus propriétaire |
| Résultat final | Comptage direct | Somme avec `MPI_Reduce` |

## 9. Coût et limites de la parallélisation

La version MPI peut réduire le temps de calcul, car chaque processus traite
environ `N / nbproc` lignes. Cependant, le gain n'est pas parfaitement
proportionnel au nombre de processus :

- les échanges de frontières ont un coût ;
- les processus doivent attendre les communications bloquantes ;
- pour une petite grille, le coût des communications peut dépasser le gain de
  calcul ;
- le découpage actuel exige que `N` soit divisible par `nbproc`.

Le temps est actuellement mesuré avec `clock()`, qui mesure principalement le
temps processeur consommé par chaque processus. Pour mesurer le temps réel
d'exécution d'un programme MPI, `MPI_Wtime()` serait plus adapté. Le temps
global pertinent est généralement le temps maximal parmi les processus, car
l'exécution se termine lorsque le processus le plus lent a terminé.

## 10. Résumé

La version MPI parallélise la simulation en découpant horizontalement la grille.
Chaque processus calcule un groupe de lignes et communique seulement avec ses
voisins directs. Les lignes fantômes fournissent les données situées de l'autre
côté des frontières. Enfin, `MPI_Reduce` rassemble les compteurs locaux pour
produire le résultat global.

La règle d'évolution de l'épidémie reste donc identique à celle de la version
séquentielle ; seuls la répartition des données, les communications et le
regroupement des résultats sont ajoutés.

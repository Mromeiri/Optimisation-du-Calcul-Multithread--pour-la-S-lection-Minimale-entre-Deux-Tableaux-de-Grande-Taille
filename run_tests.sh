#!/bin/bash
# run_tests.sh
# Ce script lance le programme avec différentes configurations.

# Nom du binaire compilé
BINARY=./min_array
OUTPUT=results.csv

# En-tête du fichier CSV
echo "method,nb_threads,migration,average_time,min_blocks,max_blocks" > $OUTPUT

# Méthodes à tester
methods=("cyclic" "block" "farming")
# Nombre de threads à tester
threads=(1 2 4 8 16 32 64 128 256 512 1024)
# Migration : 0 = non autorisée, 1 = autorisée
migrations=(0 1)

for m in "${methods[@]}"; do
  for mig in "${migrations[@]}"; do
    for t in "${threads[@]}"; do
      echo "Running method=$m, threads=$t, migration=$mig"
      # Exécution du programme et récupération de la sortie CSV
      result=$($BINARY $m $t $mig)
      
      # Pour les méthodes cyclic et block qui ne fournissent pas min_blocks et max_blocks,
      # ajouter des champs vides à la fin de la ligne
      if [[ "$m" == "cyclic" || "$m" == "block" ]]; then
        echo "$result,," >> $OUTPUT
      else
        echo "$result" >> $OUTPUT
      fi
    done
  done
done

echo "Les résultats ont été sauvegardés dans $OUTPUT"
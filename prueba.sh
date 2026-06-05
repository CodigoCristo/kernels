#!/bin/bash
# sudo pacman -S stress-ng sysbench hyperfine 
CORES=$(nproc)
KERNEL=$(uname -r)
FECHA=$(date "+%Y-%m-%d %H:%M")
CARPETA="$HOME/resultado_kernels"
mkdir -p "$CARPETA"

sep() { echo "─────────────────────────────────────────"; }

# La linea de resultados de stress-ng 0.21 tiene este formato:
# stressor  bogo-ops  real-time  usr  sys  bogo-ops/s(real)  bogo-ops/s(usr+sys)
# Nos interesa la penultima columna (bogo-ops/s real time)
sacar_ops() {
  echo "$1" | grep -E '^\S.*: \[.*\] (cpu|vm|hdd|pipe|cache)' | \
    awk '{print $(NF-1)}' | tail -1
}

# alternativa mas directa: buscar la linea con el stressor y sacar col NF-1
sacar2() {
  local raw="$1"
  local stressor="$2"
  echo "$raw" | grep "rc:.*$stressor " | awk '{print $(NF-1)}' | tail -1
}

echo ""
sep
echo "  kernel : $KERNEL"
echo "  cores  : $CORES"
echo "  fecha  : $FECHA"
sep
echo ""
sleep 1

echo "[1/5] CPU puro ($CORES cores, 15s)..."
R1=$(stress-ng --cpu $CORES --timeout 15 --metrics-brief 2>&1)
OPS1=$(echo "$R1" | awk '/cpu[[:space:]]+[0-9]+/ {print $(NF-1)}' | tail -1)
[ -z "$OPS1" ] && OPS1="error"
echo "    → ${OPS1} ops/s"
echo ""

echo "[2/5] Memoria RAM (512MB, 15s)..."
R2=$(stress-ng --vm 2 --vm-bytes 512M --timeout 15 --metrics-brief 2>&1)
OPS2=$(echo "$R2" | awk '/vm[[:space:]]+[0-9]+/ {print $(NF-1)}' | tail -1)
[ -z "$OPS2" ] && OPS2="error"
echo "    → ${OPS2} ops/s"
echo ""

echo "[3/5] I/O disco (15s)..."
R3=$(stress-ng --hdd $CORES --hdd-bytes 128M --timeout 15 --metrics-brief 2>&1)
OPS3=$(echo "$R3" | awk '/hdd[[:space:]]+[0-9]+/ {print $(NF-1)}' | tail -1)
[ -z "$OPS3" ] && OPS3="error"
echo "    → ${OPS3} ops/s"
echo ""

echo "[4/5] Scheduler - pipes entre procesos (15s)..."
R4=$(stress-ng --pipe $CORES --timeout 15 --metrics-brief 2>&1)
OPS4=$(echo "$R4" | awk '/pipe[[:space:]]+[0-9]+/ {print $(NF-1)}' | tail -1)
[ -z "$OPS4" ] && OPS4="error"
echo "    → ${OPS4} ops/s"
echo ""

echo "[5/5] Cache de CPU (15s)..."
R5=$(stress-ng --cache $CORES --timeout 15 --metrics-brief 2>&1)
OPS5=$(echo "$R5" | awk '/cache[[:space:]]+[0-9]+/ {print $(NF-1)}' | tail -1)
[ -z "$OPS5" ] && OPS5="error"
echo "    → ${OPS5} ops/s"
echo ""

sep
echo "  RESULTADO FINAL  (mas alto = mejor)"
sep
echo "  kernel : $KERNEL"
echo "  cores  : $CORES"
echo "  fecha  : $FECHA"
echo ""
printf "  %-25s %s\n" "cpu_puro"       "${OPS1} ops/s"
printf "  %-25s %s\n" "memoria_ram"    "${OPS2} ops/s"
printf "  %-25s %s\n" "io_disco"       "${OPS3} ops/s"
printf "  %-25s %s\n" "latencia_sched" "${OPS4} ops/s"
printf "  %-25s %s\n" "cache_cpu"      "${OPS5} ops/s"
sep
echo "  ops/s = mas alto es mejor"

LOG="$CARPETA/resultado_${KERNEL}.txt"
{
  echo "kernel : $KERNEL"
  echo "cores  : $CORES"
  echo "fecha  : $FECHA"
  echo ""
  printf "%-25s %s\n" "cpu_puro"       "${OPS1} ops/s"
  printf "%-25s %s\n" "memoria_ram"    "${OPS2} ops/s"
  printf "%-25s %s\n" "io_disco"       "${OPS3} ops/s"
  printf "%-25s %s\n" "latencia_sched" "${OPS4} ops/s"
  printf "%-25s %s\n" "cache_cpu"      "${OPS5} ops/s"
  echo ""
  echo "ops/s = mas alto es mejor"
} > "$LOG"
echo ""
echo "  Guardado en: $LOG"
echo ""

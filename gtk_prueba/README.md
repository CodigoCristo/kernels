# 🚀 Sprite Bench — x86-64-v4 con GTK4 + Adwaita + Meson

Stress test visual: cientos de sprites rebotando con FPS y CPU en pantalla.
Demuestra el impacto real de `-march=x86-64-v4` (AVX-512) sobre el hot path.

---

## Requisitos

```bash
# Ubuntu / Debian
sudo apt install gcc meson ninja-build libgtk-4-dev libadwaita-1-dev

# Fedora
sudo dnf install gcc meson ninja-build gtk4-devel libadwaita-devel

# Arch
sudo pacman -S gcc meson ninja gtk4 libadwaita
```

---

## Compilar con Meson

### Versión base (-O2, sin SIMD extendido)
```bash
meson setup build-base -Dvector_level=base
meson compile -C build-base
./build-base/sprite-bench
```

### Versión v3 (-O3 + AVX2 + FMA)
```bash
meson setup build-v3 -Dvector_level=v3
meson compile -C build-v3
./build-v3/sprite-bench
```

### Versión v4 (-O3 + AVX-512)
```bash
meson setup build-v4 -Dvector_level=v4
meson compile -C build-v4
./build-v4/sprite-bench
```

### Ver resumen de configuración
```bash
meson setup build-v4 -Dvector_level=v4
# Imprime la tabla:
# Build type    : release
# Vector level  : v4
# Arch flags    : -march=x86-64-v4 -O3 -ffast-math ...
# GTK4 version  : 4.x.x
```

---

## Detectar qué nivel soporta tu CPU

### 1. Ver niveles x86-64 soportados por el sistema

```bash
/lib/ld-linux-x86-64.so.2 --help | grep supported
```

Salida típica en Zen 4 / Ice Lake+:
```
Subdirectories of glibc-hwcaps directories, in priority order:
  x86-64-v4  (supported, searched)
  x86-64-v3  (supported, searched)
  x86-64-v2  (supported, searched)
```

Si `x86-64-v4` no aparece como `(supported)`, el binario compilado con
`-march=x86-64-v4` dará **Illegal instruction** al ejecutarse.
Usa `-Dvector_level=v3` en ese caso.

### 2. Ver el `-march` exacto que corresponde a tu CPU

```bash
gcc -march=native -Q --help=target 2>&1 | grep -Po "^\s+-march=\s+\K(\w+)$"
```

Ejemplos de salida:

| CPU                        | Salida         | Nivel recomendado |
|----------------------------|----------------|-------------------|
| AMD Ryzen 9 7950X (Zen 4)  | `znver4`       | v4 ✅ AVX-512     |
| AMD Ryzen 9 9950X (Zen 5)  | `znver5`       | v4 ✅ AVX-512     |
| AMD Ryzen 5 5600X (Zen 3)  | `znver3`       | v3 ⚡ AVX2+FMA   |
| Intel Core i9-13900K       | `raptorlake`   | v3 ⚡ AVX2+FMA   |
| Intel Core Ultra 9 285K    | `arrowlake`    | v4 ✅ AVX-512     |
| Intel Xeon Ice Lake        | `icelake-server` | v4 ✅ AVX-512   |

`znver4` / `znver5` y las generaciones Ice Lake+ de Intel tienen AVX-512 completo →
usa `-Dvector_level=v4`. `znver3` y `raptorlake` solo llegan a AVX2 → `-Dvector_level=v3`.

### 3. Alternativa: compilar con `-march=native`

Añade `'native'` a los `choices` en `meson_options.txt` y el bloque en `meson.build`:

```meson
elif opt_level == 'native'
  arch_flags = ['-march=native', '-O3', '-ffast-math', '-funroll-loops', '-ftree-vectorize']
  message('▶ Vector level: native (máximo para este CPU específico)')
```

Luego:
```bash
meson setup build-native -Dvector_level=native
meson compile -C build-native
```

---

## Cómo comparar rendimiento

1. Lanza `./build-base/sprite-bench` → mueve el slider a **2000 sprites**
2. Anota FPS y CPU%
3. Cierra y lanza `./build-v4/sprite-bench` con los mismos 2000 sprites
4. Compara las barras de progreso en el panel lateral

---

## El hot path vectorizado

`update_sprites()` en `main.c` — este bucle es el candidato ideal:

```c
// Con -march=x86-64-v4 el compilador genera instrucciones ZMM (512-bit):
// VMOVAPS zmm0        → carga 16 floats en 1 registro
// VFMADD231PS zmm     → multiply-add fusionado de 16 floats
// VCMPPS + VBLENDMPS  → máscara condicional para rebotes

for (int i = 0; i < n; i++) {
    s[i].x += s[i].vx * dt;    // 16 floats/ciclo con ZMM
    s[i].y += s[i].vy * dt;
    s[i].angle += s[i].spin * dt;
    // ... rebotes
}
```

---

## Estructura del proyecto

```
sprite-bench/
├── meson.build          ← build system principal
├── meson_options.txt    ← opción vector_level: base | v3 | v4
├── main.c               ← fuente completa GTK4 + Adwaita
└── README.md
```

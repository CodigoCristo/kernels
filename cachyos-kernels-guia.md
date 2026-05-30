# Guía Completa de Kernels en CachyOS

> CachyOS es una distribución basada en Arch Linux orientada al rendimiento. Una de sus características principales es ofrecer múltiples kernels altamente optimizados para diferentes casos de uso.

> 🧪 **Próximamente:** Realizaremos benchmarks y pruebas comparativas entre los distintos kernels para ver cuál rinde mejor en gaming, escritorio, compilación y latencia de audio. ¡Mantente atento!

---

## Kernels Oficiales de Arch Linux

Antes de entrar en los kernels de CachyOS, conviene conocer los que Arch Linux soporta oficialmente. Tienen soporte de la comunidad en el [foro oficial](https://bbs.archlinux.org/viewforum.php?id=22) y sistema de [reporte de bugs](https://wiki.archlinux.org/title/Bug_reporting_guidelines).

### `linux` — Stable
- **Descripción:** El kernel Linux estándar con módulos, y algunos parches mínimos aplicados. Es la base de todo.
- **Paquete:** [linux](https://archlinux.org/packages/?name=linux)
- **Web:** https://www.kernel.org/

### `linux-hardened` — Hardened
- **Descripción:** Kernel enfocado en seguridad. Aplica un conjunto de parches de hardening para mitigar exploits tanto en el kernel como en el espacio de usuario. Habilita más opciones de seguridad upstream que el kernel estable base.
- **Paquete:** [linux-hardened](https://archlinux.org/packages/?name=linux-hardened)
- **Web:** https://github.com/anthraxx/linux-hardened

### `linux-lts` — Longterm
- **Descripción:** Kernel LTS (Long Term Support) con soporte extendido. Útil cuando se usan módulos externos que pueden no ser compatibles de forma inmediata con el último kernel estable.
- **Paquete:** [linux-lts](https://archlinux.org/packages/?name=linux-lts)
- **Web:** https://www.kernel.org/

### `linux-rt` / `linux-rt-lts` — Realtime
- **Descripción:** Mantenido por un pequeño grupo de desarrolladores liderado por Ingo Molnár. Permite que casi todo el kernel sea apropiado (*preempted*), reemplazando la mayoría de spinlocks del kernel con mutexes que soportan herencia de prioridad, y moviendo todas las interrupciones a hilos del kernel. Esencial para audio profesional con latencias extremadamente bajas.
- **Paquetes:** [linux-rt](https://archlinux.org/packages/?name=linux-rt), [linux-rt-lts](https://archlinux.org/packages/?name=linux-rt-lts)
- **Web:** https://wiki.linuxfoundation.org/realtime/start

### `linux-zen` — Zen Kernel
- **Descripción:** Resultado del esfuerzo colaborativo de varios hackers del kernel para ofrecer el mejor kernel Linux posible para sistemas de uso diario. Incluye mejoras de interactividad y rendimiento para escritorio.
- **Paquete:** [linux-zen](https://archlinux.org/packages/?name=linux-zen)
- **Web:** https://github.com/zen-kernel/zen-kernel

---

## Kernels de CachyOS

Los kernels de CachyOS van un paso más allá de los oficiales de Arch, aplicando optimizaciones agresivas de compilador, schedulers alternativos y parches experimentales de rendimiento. A continuación, todo lo que necesitas saber.

---

## ¿Qué es Linux Upstream?

Es el **kernel Linux oficial**, el que mantiene Linus Torvalds y su equipo en [kernel.org](https://kernel.org). Es la fuente original de la que parten todas las distribuciones. Cuando CachyOS añade sus propios parches, está trabajando "downstream" (aguas abajo). Si algo se dice que está "en upstream", significa que ya fue aceptado en el kernel oficial.

---

## El CachyOS Base Patchset

Todos los kernels de CachyOS comparten un conjunto base de mejoras respecto al kernel oficial:

| Parche | Descripción |
|---|---|
| `aes-crypto` | Mejoras masivas de cifrado con soporte dinámico de AVX2, AVX512 y AVX10.1 |
| `amd-pstate` | Mejoras al driver de gestión de energía de CPUs AMD |
| `intel-pstate` | Mejoras al driver de gestión de energía de CPUs Intel |
| `bbr3` | Reemplaza BBRv2 con BBRv3 de Google (mejor control de congestión de red) |
| `block` | Mejoras a los schedulers de I/O bfq y mq-deadline |
| `ntsync` | Driver NTSync para mejor compatibilidad con aplicaciones Windows via Wine/Proton |
| `le9uo` | Previene thrashing y alta latencia en situaciones de poca RAM disponible |
| `ksm` | Identifica páginas de memoria idénticas y las fusiona, ahorrando RAM |
| `zstd` | Actualiza la API de compresión ZSTD a la versión más reciente (mejora BTRFS, ZRAM, ZSWAP) |
| `cachy` | Cambios de configuración para interactividad, OpenRGB, ACS Override, parches de Clear Linux y HDR |
| `t2` | Compatibilidad con MacBooks con chip T2 |

---

## Los Schedulers — ¿Qué son y cuál es la diferencia?

El **scheduler de CPU** (planificador) es un componente de software o sistema encargado de gestionar, programar y distribuir tareas en el tiempo. Su objetivo principal es automatizar procesos y optimizar recursos (como la memoria o el tiempo de CPU). Es uno de los factores más importantes para la sensación de fluidez del sistema.

### EEVDF — *Earliest Eligible Virtual Deadline First*
- **Origen:** Kernel Linux oficial desde la versión 6.6 (2023). Reemplazó al histórico CFS.
- **Cómo funciona:** Cada proceso recibe una "fecha límite virtual" para ser ejecutado. El scheduler siempre elige al proceso cuya fecha límite vence más pronto.
- **Para qué sirve:** Comportamiento predecible y justo. Es el scheduler "neutral" y estable, sin tuning especial para gaming ni escritorio.
- **Ideal para:** Usuarios que prefieren seguir el estándar oficial sin modificaciones.

### BORE — *Burst-Oriented Response Enhancer*
- **Origen:** Creado por `firelzrd`, parche externo aplicado sobre EEVDF.
- **Cómo funciona:** Añade un concepto de "burst score" que rastrea si un proceso tiende a usar mucho CPU de golpe. Los procesos interactivos (que duermen y despiertan frecuentemente, como la UI) reciben prioridad sobre procesos que consumen CPU de forma continua.
- **Para qué sirve:** Mejor respuesta del escritorio y menor latencia percibida, especialmente cuando hay procesos pesados en segundo plano.
- **Ideal para:** Gaming, uso de escritorio general, cualquier cosa donde la fluidez visual importa.
- **Es el scheduler por defecto de CachyOS.**

### BMQ — *BitMap Queue*
- **Origen:** Parte de **Project C** por Alfred Chen.
- **Cómo funciona:** Usa colas organizadas como mapas de bits (estructuras muy eficientes en memoria) para gestionar la prioridad de procesos. Tiene un diseño más simple y determinista que BORE o EEVDF.
- **Para qué sirve:** Interactividad del escritorio con un enfoque diferente al de BORE. Algunos usuarios reportan mejor respuesta en sistemas con pocos núcleos.
- **Limitación importante:** **No soporta sched-ext**, así que no puedes cambiar de scheduler en caliente.
- **Ideal para:** Usuarios que quieren experimentar con una alternativa a BORE.

### sched-ext — *Scheduler Extensions Framework*
- **Origen:** Fusionado en el kernel Linux oficial en la versión 6.12.
- **Cómo funciona:** No es un scheduler en sí, sino un **framework** que permite cargar schedulers escritos en **eBPF** desde el espacio de usuario, sin recompilar el kernel. Puedes cambiar de scheduler con un solo comando, en caliente, sin reiniciar.
- **Para qué sirve:** Experimentar y cambiar entre schedulers fácilmente.
- **Schedulers disponibles via sched-ext** (paquete `scx-scheds`):

| Scheduler | Enfoque |
|---|---|
| `scx_rusty` | Uso general, escrito en Rust |
| `scx_lavd` | Gaming y baja latencia |
| `scx_bpfland` | Interactividad de escritorio |
| `scx_flash` | Alto rendimiento experimental |

> **Nota:** El kernel BMQ no soporta sched-ext porque reemplaza el scheduler a nivel de código del kernel directamente, lo que lo hace incompatible con el framework.

---

## Los Kernels Base

Todos los kernels incluyen el CachyOS Base Patchset. La diferencia está en el scheduler y el propósito.

### `linux-cachyos` — ⭐ El Recomendado
- **Scheduler:** BORE + framework sched-ext incluido
- **Para qué sirve:** Kernel principal de CachyOS. Ofrece lo mejor de BORE para interactividad más la flexibilidad de sched-ext para cambiar schedulers sin reiniciar.
- **Ideal para:** Uso diario, gaming, escritorio. La elección por defecto para la mayoría de usuarios.

---

### `linux-cachyos-bore`
- **Scheduler:** BORE (sin sched-ext)
- **Para qué sirve:** Idéntico al default pero sin el framework sched-ext, lo que lo hace marginalmente más ligero.
- **Ideal para:** Quien quiere BORE sin la posibilidad de cambiar schedulers en caliente.

---

### `linux-cachyos-bmq`
- **Scheduler:** BMQ de Project C (Alfred Chen)
- **Para qué sirve:** Alternativa al scheduler BORE con un enfoque diferente. No soporta sched-ext.
- **Ideal para:** Experimentar con un scheduler alternativo, especialmente en equipos con pocos núcleos.

---

### `linux-cachyos-eevdf`
- **Scheduler:** EEVDF puro (el scheduler estándar de Linux upstream)
- **Para qué sirve:** Kernel con los parches de CachyOS pero usando el scheduler oficial sin modificaciones adicionales.
- **Ideal para:** Usuarios que quieren las optimizaciones de CachyOS pero prefieren el scheduler estándar.

---

### `linux-cachyos-hardened`
- **Scheduler:** BORE
- **Para qué sirve:** Incluye parches de `linux-hardened` con configuración de seguridad muy agresiva. Mitiga vulnerabilidades y vectores de ataque a costa de rendimiento notable.
- **Ideal para:** Máquinas donde la seguridad es la prioridad absoluta sobre el rendimiento.
- ⚠️ **Advertencia:** La configuración de hardening reduce significativamente el rendimiento.

---

### `linux-cachyos-lts`
- **Scheduler:** BORE
- **Versión de kernel:** Rama LTS (Long Term Support) — actualmente 6.x
- **Para qué sirve:** Menos parches que las variantes más nuevas para garantizar máxima estabilidad. Recibe soporte durante años desde upstream.
- **Ideal para:** Quien prioriza estabilidad absoluta sobre tener lo último. Buen fallback si el kernel estable da problemas.

---

### `linux-cachyos-rc`
- **Scheduler:** BORE + sched-ext
- **Para qué sirve:** Basado en el último **Release Candidate** del kernel Linux. Tiene las últimas novedades pero puede ser inestable.
- **Ideal para:** Desarrolladores y usuarios avanzados que quieren probar lo más nuevo.
- ⚠️ **Advertencia:** Puede romper cosas. No recomendado para uso de producción.

---

### `linux-cachyos-rt-bore`
- **Scheduler:** BORE con parches **Real-Time (RT)**
- **Para qué sirve:** El parche RT transforma el kernel en uno de tiempo real, con preempción completa habilitada. Reduce la latencia al mínimo posible.
- **Ideal para:** Producción musical, audio profesional con herramientas como JACK o PipeWire en modo RT, o cualquier aplicación que necesite latencia ultra baja garantizada.

---

### `linux-cachyos-server`
- **Scheduler:** EEVDF estándar (sin CONFIG_CACHY, sin tuning de interactividad)
- **Para qué sirve:** Optimizado para **throughput** (máximo trabajo procesado) en lugar de interactividad. Tickrate a 300Hz, sin preempción.
- **Ideal para:** Servidores, máquinas de compilación, workloads de cómputo intensivo.
- ⚠️ **No recomendado para escritorio.** El sistema se sentirá menos responsivo.

---

### `linux-cachyos-deckify`
- **Scheduler:** BORE
- **Para qué sirve:** Parches específicos para la **Steam Deck** y otros handhelds. Mejora la compatibilidad con hardware portátil.
- **Ideal para:** Steam Deck y dispositivos similares (ROG Ally, Legion Go, etc.).
- ⚠️ **No se recomienda en PCs de escritorio o laptops convencionales.**

---

## Los Sufijos — Variantes de Compilación y Módulos

Cada kernel base se multiplica por estas opciones. No son kernels distintos, son el mismo kernel con diferente compilador o módulos extras incluidos.

### Sufijos de Compilador

| Sufijo | Compilador | Cuándo usarlo |
|---|---|---|
| *(sin sufijo)* | Clang + ThinLTO | ✅ Por defecto. Recomendado para la mayoría |
| `-gcc` | GCC | Si tienes problemas de compatibilidad con Clang |
| `-lto` | Clang + Full LTO | Máxima optimización. Mismas funciones, compilación más lenta |

> **¿Qué es LTO?** *Link Time Optimization* — normalmente el compilador optimiza cada archivo por separado. Con LTO optimiza todo el código junto al final, encontrando optimizaciones cruzadas entre archivos. Resultado: binario más eficiente. El `-lto` usa **Full LTO** mientras que el default usa **ThinLTO** (más rápido de compilar, casi el mismo rendimiento).

### Sufijos de Módulos

| Sufijo | Qué incluye | Cuándo instalarlo |
|---|---|---|
| `-headers` | Cabeceras del kernel | Si compilas módulos externos (VirtualBox, DKMS, etc.) |
| `-nvidia-open` | Driver open-source de NVIDIA precompilado | Si tienes GPU NVIDIA |
| `-zfs` | Módulo ZFS precompilado | Si usas ZFS como filesystem |
| `-r8125` | Driver Realtek RTL8125 (2.5Gbps) | Si tienes esta tarjeta de red y tienes problemas con r8169 |
| `-dbg` | Símbolos de debug sin comprimir | Solo para perfilado y desarrollo del kernel |

---

## Los Repositorios — `cachyos/` vs `cachyos-v4/`

| Prefijo | Arquitectura objetivo | Cuándo usarlo |
|---|---|---|
| `cachyos/` | x86-64 genérico | CPUs desde ~2008 en adelante |
| `cachyos-v4/` | x86-64-v4 (AVX-512) | Intel Skylake-X, Ice Lake, Sapphire Rapids / AMD Zen 4+ |

La versión `-v4` usa instrucciones más avanzadas del procesador para mayor rendimiento. Si tu CPU lo soporta (puedes verificarlo con `lscpu | grep avx512`), es la versión que deberías usar.

---

## Resumen: ¿Qué instalar según tu caso?

### Usuario normal con NVIDIA
```
linux-cachyos
linux-cachyos-headers
linux-cachyos-nvidia-open
```

### Usuario sin NVIDIA (AMD/Intel)
```
linux-cachyos
linux-cachyos-headers
```

### Máxima estabilidad (fallback)
```
linux-cachyos-lts
linux-cachyos-lts-headers
```

### Producción musical / audio profesional
```
linux-cachyos-rt-bore
linux-cachyos-rt-bore-headers
```

### Quiero experimentar con schedulers
```
linux-cachyos          ← incluye sched-ext
scx-scheds             ← paquete con los schedulers alternativos
```

### Seguridad ante todo
```
linux-cachyos-hardened
linux-cachyos-hardened-headers
```

---

## Tabla de Referencia Rápida

| Kernel | Scheduler | sched-ext | Uso |
|---|---|---|---|
| `linux-cachyos` | BORE | ✅ Sí | ⭐ Recomendado general |
| `linux-cachyos-bore` | BORE | ❌ No | Gaming/escritorio sin sched-ext |
| `linux-cachyos-bmq` | BMQ | ❌ No | Alternativa experimental |
| `linux-cachyos-eevdf` | EEVDF | ❌ No | Scheduler estándar upstream |
| `linux-cachyos-hardened` | BORE | ❌ No | Seguridad máxima |
| `linux-cachyos-lts` | BORE | ❌ No | Estabilidad máxima |
| `linux-cachyos-rc` | BORE | ✅ Sí | Bleeding edge / testing |
| `linux-cachyos-rt-bore` | BORE + RT | ❌ No | Audio profesional / tiempo real |
| `linux-cachyos-server` | EEVDF | ❌ No | Servidores / throughput |
| `linux-cachyos-deckify` | BORE | ✅ Sí | Steam Deck / handhelds |

---

*Documentación basada en CachyOS con Linux 7.0. Para información actualizada visita [wiki.cachyos.org](https://wiki.cachyos.org/features/kernel/)*

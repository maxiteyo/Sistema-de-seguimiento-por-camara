# Real-Time Object Tracking System (Hybrid C/Python)

Sistema de seguimiento de objetos por color en tiempo real. Python (OpenCV) procesa la imagen, detecta objetos rojos/verdes/azules y envía las coordenadas a un proceso en C mediante memoria compartida + semáforos POSIX. C calcula el error de centrado y mueve un servomotor MG996R (o lo simula en PC).

## Requisitos de Hardware

- **Raspberry Pi** (3, 4 o 5) o una **PC con Linux/WSL2**
- **Cámara Web USB**
- **Servomotor MG996R** (solo en Raspberry Pi)
- **Fuente externa 5V/2A** para el servo (no alimentarlo desde la Pi)

## Conexión del Servo MG996R

> ⚠️ **El MG996R consume mucha corriente. Conectarlo directo a los 5V de la Pi puede quemarla.**
> Usá siempre una fuente externa. El GND compartido es obligatorio.

### Opción A — Cable USB de cargador (sin comprar nada)

Cortás un cable USB de un cargador viejo, pelás los cables rojo (+5V) y negro (GND):

```
Cargador USB ────┬── Cable rojo ──── VCC (rojo) del servo
                 └── Cable negro ──┬── GND (negro) del servo
                                   └── Pin GND de la Raspberry Pi

Raspberry Pi GPIO 18 ──── Cable señal (blanco/naranja) del servo
```

### Opción B — Módulo MB102 (más prolijo, ~$1)

```
Cargador USB ── MB102 ──┬── 5V  ──── VCC del servo
                         ├── GND ──┬── GND del servo
                         │         └── GND Raspberry Pi
Raspberry GPIO 18 ──────── Señal del servo
```

### Resumen de conexiones

| Servo MG996R | Conectar a |
|-------------|------------|
| Rojo (VCC) | **Fuente externa 5V/2A** (NO a la Pi) |
| Negro (GND) | GND de la fuente **Y** GND de la Pi |
| Blanco/Naranja (Señal) | GPIO 18 de la Pi |

## Instalación

> **En Raspberry Pi:** podés usar el script automático `setup_raspi.sh` que hace todo (repos, librerías, Python, compilación):
> ```bash
> chmod +x setup_raspi.sh
> ./setup_raspi.sh
> ```

### 1. Dependencias del Sistema

```bash
sudo apt-get update
sudo apt-get install -y build-essential python3 python3-pip python3-venv
```

### 2. Python (OpenCV + numpy + posix-ipc)

Usar Python 3. Verificar con `python3 --version`.

**En PC (x86_64):**

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

**En Raspberry Pi (ARM) — por apt (OpenCV 3.2, más estable):**

```bash
sudo apt-get install -y python3-opencv python3-numpy libatlas3-base
pip install posix-ipc
```

**En Raspberry Pi — por pip con piwheels (OpenCV 4.5, más moderno):**

Si tu Python 3 es <= 3.7 (ej: Raspbian Buster), primero arreglar los repositorios:

```bash
sudo sed -i 's|raspbian.raspberrypi.org|archive.raspbian.org|g' /etc/apt/sources.list
sudo apt-get update
sudo apt-get install -y libatlas3-base
```

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install --index-url https://www.piwheels.org/simple opencv-python==4.5.5.64 numpy posix-ipc
```

### 3. Librería GPIO y Servo (solo Raspberry Pi)

```bash
sudo apt-get install -y pigpio
sudo systemctl enable pigpiod
sudo systemctl start pigpiod
```

No ejecutar `./controller` con el daemon activo (chocan). Detenerlo antes:

```bash
sudo systemctl stop pigpiod
```

### 4. Compilar el Controlador C

**En PC / Notebook (simulación del servo por terminal):**

```bash
gcc controller.c -o controller -lpthread -lrt -lm
```

**En Raspberry Pi (control real del servo por GPIO 18):**

```bash
gcc controller.c -o controller -lpthread -lrt -lm -lpigpio
```

## Cómo Ejecutar

Siempre inicia **primero el programa en C** porque crea la memoria compartida y el semáforo.

### Paso 1: Ejecutar el controlador C

**En PC:**

```bash
./controller
```

**En Raspberry Pi (requiere root para acceder a GPIO):**

Detener el daemon pigpiod antes (si está corriendo):

```bash
sudo systemctl stop pigpiod
```

Iniciar el controlador:

```bash
sudo ./controller
```

Deberías ver:
```
Iniciando sistema de control en C...
[Seguimiento] Inicializando Memoria Compartida...
[Seguimiento] Listo. Esperando datos de Python...
```

### Paso 2: Ejecutar el sistema de visión

**En PC (misma terminal o una nueva):**

```bash
source .venv/bin/activate
python3 colorv4.py
```

**En Raspberry Pi — necesario desde VNC o con monitor HDMI:**

> ❌ **No funciona por SSH.** OpenCV necesita una pantalla para mostrar la ventana de la cámara.
> Conectate primero por VNC (ver sección [VNC con TigerVNC](#vnc-con-tigervnc--ver-la-cámara-desde-tu-pc-sin-monitor)).

Una vez conectado por VNC, abrí una terminal en el escritorio de la Pi y ejecutá:

```bash
cd ~/grupo3
sudo .venv/bin/python3 colorv4.py
```

> En la Pi se necesita `sudo` porque la memoria compartida fue creada por `root` (el C arranca con `sudo`).
> El C y Python pueden correr en la misma terminal de VNC pero en distinto orden: primero C, después Python.

Deberías ver:
```
Conectando con el controlador en C...
Conexión IPC establecida exitosamente.
```

Se abrirá una ventana con la cámara (320x240 en la Pi para mejor rendimiento, 640x480 en PC).

### Controles

| Tecla | Acción |
|-------|--------|
| R | Seguir objeto rojo |
| G | Seguir objeto verde |
| B | Seguir objeto azul |
| Q | Cerrar ambos programas |

## VNC con TigerVNC — Ver la cámara desde tu PC sin monitor

Si la Pi no tiene monitor, conectate por VNC para ver la ventana de OpenCV.

### En la Raspberry Pi

```bash
# Habilitar VNC server
sudo raspi-config nonint do_vnc 0
sudo systemctl start vncserver-x11-serviced

# Establecer contraseña (si no te la pide bien, probá las variantes)
sudo vncpasswd -service
```

Verificá que está corriendo:
```bash
vncserver -list
```

### En tu PC (cliente TigerVNC)

Instalar TigerVNC:

```bash
# Ubuntu / Debian
sudo apt-get install -y tigervnc-viewer

# Windows — descargar de https://tigervnc.org
```

Conectar:

```bash
vncviewer 192.168.1.65:5900
```

> Si la ventana se ve muy chica, apretá **F8** → **Scale to window size** o ejecutá:
> ```bash
> vncviewer -FullScreen 192.168.1.65
> ```

**Alternativa — RealVNC Viewer (Windows/macOS):** descargar de [realvnc.com](https://www.realvnc.com/en/connect/download/viewer/) y conectar a la IP de la Pi.

## Solución de Problemas

**"No se encontró la memoria compartida" / "ExistentialError"**
→ Ejecutá `./controller` primero.

**ImportError: libcblas.so.3: cannot open shared object file**
→ Falta la librería BLAS. Descargar e instalar manualmente:
```bash
wget http://archive.debian.org/debian/pool/main/a/atlas/libatlas3-base_3.10.3-8_armhf.deb
sudo dpkg -i libatlas3-base_3.10.3-8_armhf.deb
```

**ModuleNotFoundError: No module named 'cv2'**
→ Usaste `sudo python3` sin el venv. Ejecutá con:
```bash
sudo .venv/bin/python3 colorv4.py
```

**PermissionsError: No permission to access this segment**
→ La memoria compartida fue creada por root (C con sudo). Ejecutar Python también con sudo.

**pigpio uninitialised / Can't lock /var/run/pigpio.pid**
→ El daemon `pigpiod` está corriendo y bloquea el acceso directo. Ejecutá:
```bash
sudo systemctl stop pigpiod
sudo ./controller
```

**El servo no se mueve (Raspberry Pi)**
1. Verificá que el cable de señal esté en **GPIO 18**
2. Usá una **fuente externa de 5V/2A** para el servo
3. Conectá el GND de la fuente externa a un GND de la Raspberry Pi

**Error de recursos ocupados al iniciar**
```bash
rm /dev/shm/sem.sem_vision /dev/shm/shm_vision
```

**La cámara no abre**
- `ls /dev/video*` para ver si está detectada
- Si no es `/dev/video0`, cambiá `cv2.VideoCapture(0)` por el índice correcto
- En WSL2 no funcionan cámaras USB directamente (necesitás `usbipd-win`)

**apt-get update da errores 404 (Raspbian Buster)**
→ Buster quedó sin soporte. Corregir repositorios:
```bash
sudo sed -i 's|raspbian.raspberrypi.org|archive.raspbian.org|g' /etc/apt/sources.list
sudo apt-get update || true
# Si sigue fallando, descargar paquetes .deb manualmente desde archive.debian.org
```

**Error: "You don't have permission to run this program"**
→ Ejecutar con `sudo`, o compilar sin `-lpigpio` para usar el mock.

**Unable to init server / Can't initialize GTK backend**
→ No hay pantalla disponible. Conectate por VNC o usá un monitor HDMI.

## Estructura del Proyecto

| Archivo | Rol |
|---------|------|
| `colorv4.py` | Captura video, filtrado HSV, detección de color, IPC hacia C |
| `controller.c` | IPC desde Python, control de servo MG996R (o simulación) |
| `defines.h` | Constantes compartidas (deadband, servo limits) |
| `requirements.txt` | opencv-python, numpy, posix-ipc |

## Conceptos de Sistemas de Tiempo Real Aplicados

Este proyecto implementa varios conceptos fundamentales de sistemas de tiempo real
vistos en la materia del IUA. A continuación se detalla cada uno y cómo se aplica.

### Memoria Compartida (`shm_open` + `mmap`)

**Archivo:** `controller.c:150-156`, `colorv4.py:11-13`

Se usa para transferir coordenadas del objeto detectado desde el proceso Python
(visión) al proceso C (control del servo). La memoria compartida POSIX permite
que ambos procesos accedan a la misma región de RAM sin copiar datos.

**Sincronización:** el patrón es estrictamente productor-consumidor:
1. Python escribe las coordenadas en la shared memory
2. Python hace `sem_post` (libera al consumidor)
3. C hace `sem_timedwait` (consume) y lee

Como hay un solo escritor y un solo lector coordinados por semáforo, no se
necesita mutex adicional sobre la shared memory. La operación de escritura
(`struct.pack` + `mmap.write`) es suficientemente rápida como para que la
exclusión por semáforo garantice consistencia.

**Estructura de datos intercambiada** (`DatosVision` en `controller.c:28-33`):
```c
typedef struct {
    int x;           // centro X del objeto (píxeles)
    int y;           // centro Y del objeto (píxeles)
    int flags;       // bit 0: señal de quit (1 = cerrar)
    int frame_width; // ancho real del frame (para calcular FOV)
} DatosVision;
```

### Semáforo POSIX (`sem_open`, `sem_timedwait`)

**Archivo:** `controller.c` y `colorv4.py`

Semáforo contador inicializado en 0. Funciona como **mecanismo de notificación**
entre procesos: Python incrementa (produce) y C decrementa (consume).

**Uso de `sem_timedwait` con timeout de 100ms:**
- Evita que C se bloquee indefinidamente si Python falla (cuelga o crash)
- Timeout de 100ms permite re-adquirir el objeto rápidamente tras oclusión momentánea

### Mutex (`pthread_mutex_t`)

Protege la estructura `DatosServo` compartida entre los dos hilos de C:
- **Hilo de tracking**: escribe `angulo_deseado` y señaliza la condvar
- **Hilo de servo**: lee `angulo_deseado` y mueve el motor

**Propiedades de la implementación:**
- Secciones críticas muy cortas (< 10 instrucciones)
- Sin anidamiento de locks (deadlock imposible)
- Lock time mínimo: solo lo necesario para copiar el ángulo
- Inicialización estática con `PTHREAD_MUTEX_INITIALIZER`

### Variable de Condición (`pthread_cond_wait` + `pthread_cond_signal`)

Mecanismo para dormir el hilo del servo hasta que haya un nuevo ángulo disponible.
- El hilo de tracking calcula el nuevo ángulo y hace `pthread_cond_signal`
- El hilo de servo despierta con `pthread_cond_wait`, lee el ángulo, mueve el servo
- Inicialización estática con `PTHREAD_COND_INITIALIZER`

### Signal Handler Async-Signal-Safe

El manejador de `SIGINT`/`SIGTERM` es **async-signal-safe**:
- Usa `write()` en lugar de `printf()` (que no es segura en contexto de señal)
- Llama a `_exit(0)` en lugar de `exit(0)` (versión segura que no ejecuta handlers de atexit ni limpia buffers de stdio)
- No usa variables globales compartidas

### Slew Rate / Rate Limiter

**Archivo:** `controller.c` y `defines.h`

`MAX_DELTA_ANGLE = 5` limita el cambio máximo de ángulo por frame, con
velocidad proporcional al error. Esto:
- Reduce la velocidad angular máxima a ~75°/s a 15 FPS (más suave que antes)
- Evita oscilaciones por sobrepaso (overshoot) al no mover más de lo necesario
- El redondeo garantiza al menos 1°/frame incluso para errores pequeños

### Deadband (Zona Muerta)

**Archivo:** `controller.c` y `defines.h`

`DEADBAND_PX = 15` crea una zona alrededor del centro donde el servo no se mueve.
Esto evita micro-correcciones constantes cuando el objeto está prácticamente centrado.
Técnica clásica de control para eliminar el chatter del actuador.

### Control Proporcional (Velocidad)

El control calcula un delta de ángulo proporcional al error de píxeles:
```
error_px = x_objeto - centro
error_ratio = error_px / (ancho_frame / 2)
delta = error_ratio * MAX_DELTA_ANGLE
nuevo_angulo = actual + delta
```

El delta usa **redondeo matemático** (no truncado) y garantiza al menos 1°/frame
para cualquier error fuera de la deadband. Esto evita el "pegado" por truncado a 0.

La velocidad es proporcional al error:
| Error en px | Error ratio | Delta (°) | Comportamiento |
|:-----------:|:-----------:|:---------:|----------------|
| 16 | 0.10 | 1 | Corrección suave, centrado fino |
| 80 | 0.50 | 3 | Respuesta media |
| 160 | 1.00 | 5 | Velocidad máxima |

El rango completo de 0° a 180° se alcanza naturalmente: como el delta siempre apunta
en la dirección del error, el servo sigue moviéndose hasta que el objeto entra en la
deadband.

No hay término integral (I) porque:
- El sistema mecánico no tiene error de estado estacionario apreciable
- Un integrador podría causar windup

No hay término derivativo (D) porque:
- El EMA filter en Python ya suaviza la entrada
- La derivada amplificaría ruido de detección

### Resumen de Conceptos RTOS Utilizados

| Concepto | Descripción | Implementado |
|----------|-------------|:------------:|
| Memoria Compartida | IPC C↔Python vía `shm_open` + `mmap` sin copia de datos | ✅ |
| Semáforo POSIX | Notificación productor-consumidor entre procesos | ✅ |
| Mutex | Exclusión mutua entre hilos C sobre `DatosServo` | ✅ |
| Condvar | Despertar selectivo del hilo servo cuando hay nuevo ángulo | ✅ |
| Signal Handler | Manejador async-signal-safe (`write()` + `_exit()`) | ✅ |
| Slew Rate | Limitador de velocidad angular del servo (5°/frame máx) | ✅ |
| Deadband | Zona muerta de ±15px para evitar micro-oscilaciones | ✅ |
| Control P (Velocidad) | Control proporcional con redondeo y 1°/frame mínimo | ✅ |
| EMA Filter | Suavizado exponencial 70/30 sobre posición X | ✅ |
| SCHED_FIFO | [Documentado] Planificación tiempo real prioritaria | ❌ No activo |
| Memory Locking | [Documentado] `mlockall` para evitar page faults | ❌ No activo |
| Watchdog | [Documentado] Timeout de seguridad para centrar servo | ❌ No activo |

## Detección de Objetos — Algoritmo de Visión

### Pipeline de Procesamiento de Imagen (`colorv4.py`)

```
Frame raw (320x240)
  → BGR → HSV (separar color de iluminación)
  → Threshold HSV por color seleccionado
    ─ Rojo: dos rangos [0-10] ∪ [160-180] combinados con bitwise_or
    ─ Verde/azul: un único rango
  → MORPH_OPEN (erode + dilate: elimina ruido blanco aislado)
  → MORPH_CLOSE (dilate + erode: rellena huecos dentro del objeto)
  → findContours con RETR_EXTERNAL (solo contornos exteriores)
  → Filtro por área (> 300 px²)
  → Selección del contorno de mayor área
  → Filtro EMA (70% anterior + 30% nuevo) para estabilidad del servo
  → Envío por shared memory + sem_post al controlador C
```

### Rangos HSV Configurados

| Color | Lower H | Upper H | Lower S | Upper S | Lower V | Upper V |
|-------|---------|---------|---------|---------|---------|---------|
| Rojo 1 | 0 | 10 | 120 | 255 | 120 | 255 |
| Rojo 2 | 160 | 180 | 120 | 255 | 120 | 255 |
| Verde | 35 | 85 | 80 | 255 | 80 | 255 |
| Azul | 94 | 120 | 80 | 255 | 80 | 255 |

Nota: El rojo en OpenCV HSV tiene H en rango 0-179. El color rojo real se
encuentra alrededor de H=0 y H=170-180 (envuelve). Por eso se necesitan dos
rangos combinados.

## Historial de Cambios

### v2.1 — Correcciones de tracking

**colorv4.py (mejoras de detección):**
- Cambiado a `MORPH_OPEN` + `MORPH_CLOSE` (antes solo `MORPH_CLOSE`, Open rellena ruido y Close rellena huecos)
- Cambiado a `RETR_EXTERNAL` (antes `RETR_TREE` — detectaba contornos anidados innecesarios)
- Rojo: doble rango HSV combinado con `bitwise_or` (H<10 ∪ H>160), con S/V≥120
- Verde: rango HSV con S/V≥80
- Azul: rango HSV con S/V≥80 (corregido V inferior de 2 a 80 que dejaba pasar cualquier pixel)
- Estructura general idéntica al original (namedWindow, try, while loop)

**controller.c (solo mejoras de seguridad mínimas):**
- `sem_timedwait` con timeout de 1s (original usaba `sem_wait` que bloqueaba infinitamente)
- Manejador de señal async-signal-safe (`write()` + `_exit()`, original usaba `printf()` + `exit()`)
- Verificación de errores en `pthread_create`
- Header `defines.h` para constantes compartidas
- Todo lo demás: idéntico al original (sin SCHED_FIFO, sin mlockall, sin watchdog, sin sigaction, sin quit_flag, con `pthread_cond_wait` y `exit(0)` originales)

**defines.h:**
- Creado como header compartido de constantes del sistema

### v2.3 — Control por velocidad con redondeo y suavizado (ACTUAL)

**controller.c:**
- Vuelta a control por velocidad (`delta = error_ratio * MAX_DELTA`) con dos mejoras clave:
  - **Redondeo matemático** (`(int)(delta_f + 0.5f)`) en vez de truncado — evita delta=0 para errores pequeños
  - **Garantía de 1°/frame mínimo** para cualquier error fuera de deadband — elimina el "pegado"
- El rango completo 0-180° se alcanza naturalmente: el servo se mueve en la dirección del error hasta entrar en deadband

**defines.h:**
- `MAX_DELTA_ANGLE` reducido de 8 a 5 para movimientos más suaves (75°/s máx)

**README.md:**
- Secciones de control, slew rate y tabla actualizadas

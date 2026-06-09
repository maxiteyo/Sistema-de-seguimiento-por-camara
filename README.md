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

**En Raspberry Pi (desde VNC o con monitor conectado):**

```bash
sudo .venv/bin/python3 colorv4.py
```

> En la Pi se necesita `sudo` porque la memoria compartida fue creada por `root` (el C se ejecuta con `sudo`).

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
| `requirements.txt` | opencv-python, numpy, posix-ipc |

/* ================================================================
   controller.c — Controlador del servomotor MG996R
   IUA — Proyecto final de Sistemas de Tiempo Real
   
   Recibe coordenadas (x,y) del objeto desde colorv4.py a través de
   memoria compartida POSIX + semáforo. Calcula el error de centrado
   y mueve el servo proporcionalmente.
   
   Compilar en PC (simulación): gcc controller.c -o controller -lpthread -lrt -lm
   Compilar en Pi (GPIO real):  gcc controller.c -o controller -lpthread -lrt -lm -lpigpio
   ================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>

#include "defines.h"

/* ================================================================
   SIMULACIÓN vs GPIO REAL
   En PC (sin __arm__) se reemplazan las funciones de pigpio por
   versiones mock que solo imprimen por terminal. En Raspberry Pi
   se incluye la librería real para controlar el GPIO 18.
   ================================================================ */
#ifdef __arm__
    #include <pigpio.h>
#else
    int gpioInitialise(void) { return 0; }
    void gpioTerminate(void) { }
    void gpioServo(unsigned gpio, unsigned pulsewidth) {
        printf("[MOCK-GPIO] Pin %d: PulseWidth %d\n", gpio, pulsewidth);
    }
#endif

/* ================================================================
   ESTRUCTURAS DE DATOS COMPARTIDAS
   ================================================================ */

/* DatosVision — Memoria compartida entre Python y C
   Python escribe aquí las coordenadas del objeto detectado.
   C lee estos datos para calcular el movimiento del servo. */
typedef struct {
    int x;           /* Centro X del objeto en píxeles (0..frame_width) */
    int y;           /* Centro Y del objeto en píxeles */
    int flags;       /* Bit 0: señal de terminación (1 = cerrar) */
    int frame_width; /* Ancho del frame procesado (para calcular centro) */
} DatosVision;

/* DatosServo — Comunicación entre hilos de C (mutex + condvar)
   El hilo de tracking escribe angulo_deseado.
   El hilo de servo lee angulo_deseado y mueve el motor. */
typedef struct {
    int angulo_deseado;          /* Ángulo objetivo (0-180°) */
    int nuevo_dato;              /* Flag: 1 si hay nuevo ángulo pendiente */
    pthread_mutex_t mutex;       /* Exclusión mutua sobre esta estructura */
    pthread_cond_t cond;         /* Condvar: notifica al hilo servo */
} DatosServo;

/* Instancia global de DatosServo. Inicialización estática (no dinámica). */
DatosServo datos_servo = {
    .angulo_deseado = 90,        /* Comienza apuntando al centro */
    .nuevo_dato = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

/* ================================================================
   MANEJADOR DE SEÑALES (Async-Signal-Safe)
   Se ejecuta cuando el usuario presiona Ctrl+C (SIGINT) o se
   recibe SIGTERM. Limpia recursos y termina el proceso.
   
   Es async-signal-safe porque:
   - Usa write() en vez de printf() (seguro en contexto de señal)
   - Usa _exit() en vez de exit() (no ejecuta handlers ni limpia buffers)
   - No toca variables globales compartidas con los hilos
   ================================================================ */
void cleanup(int signum) {
    const char msg[] = "\n[Sistema] Cerrando y limpiando recursos...\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    gpioServo(SERVO_PIN, 0);     /* Detener servo */
    gpioTerminate();             /* Liberar GPIO */
    shm_unlink("/shm_vision");   /* Eliminar memoria compartida */
    sem_unlink("/sem_vision");   /* Eliminar semáforo */
    _exit(0);
}

/* ================================================================
   HILO 1: hilo_rotacion_servo
   Espera en pthread_cond_wait hasta que el hilo de tracking
   señalice un nuevo ángulo. Luego mueve el servo físicamente
   mediante gpioServo().
   
   Nota: el servo MG996R usa modulación por ancho de pulso (PWM).
   500us = 0°, 1500us = 90°, 2500us = 180°. La fórmula convierte
   0-180° al rango 500-2500 microsegundos.
   ================================================================ */
void* hilo_rotacion_servo(void* arg) {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Error iniciando pigpio\n");
        pthread_exit(NULL);
    }

    gpioServo(SERVO_PIN, 1500);  /* Posición inicial: 90° */

    while (1) {
        pthread_mutex_lock(&datos_servo.mutex);

        /* Espera hasta que haya un nuevo dato disponible */
        while (datos_servo.nuevo_dato == 0) {
            pthread_cond_wait(&datos_servo.cond, &datos_servo.mutex);
        }

        int angulo_a_mover = datos_servo.angulo_deseado;
        datos_servo.nuevo_dato = 0;  /* Consumir el dato */

        pthread_mutex_unlock(&datos_servo.mutex);

        /* Saturación por si el tracking pidió fuera de rango */
        if (angulo_a_mover < 0) angulo_a_mover = 0;
        if (angulo_a_mover > 180) angulo_a_mover = 180;

        /* Convertir grados a ancho de pulso (500-2500 us) */
        int pulse_width = 500 + (angulo_a_mover * 2000) / 180;
        gpioServo(SERVO_PIN, pulse_width);
        printf("[Hilo Servo] Motor movido a %d grados (%d us)\n", angulo_a_mover, pulse_width);
    }

    gpioTerminate();
    pthread_exit(NULL);
}

/* ================================================================
   CONTROL PROPORCIONAL (Velocidad)
   
   Recibe la posición X del objeto, calcula el error respecto al
   centro del frame y determina cuántos grados mover el servo.
   
   Fórmula:
     error_px = x_objeto - centro
     error_ratio = error_px / (ancho_frame / 2)    → rango [-1.0, 1.0]
     delta = error_ratio * MAX_DELTA_ANGLE          → rango [-5, 5]
   
   Características:
   - Redondeo matemático (no truncado): evita delta=0 para errores chicos
   - Garantía de 1°/frame mínimo: cualquier error fuera de deadband produce movimiento
   - Rango completo 0-180°: el servo sigue en dirección del error hasta centrar
   - Deadband: si el error está dentro de DEADBAND_PX, no se mueve (evita chatter)
   ================================================================ */
void enviar_rotacion_servo(int x_objeto, int ancho_frame, int angulo_actual) {
    int centro = ancho_frame / 2;
    int error_px = x_objeto - centro;

    /* INVERTIR_SENTIDO=1: invierte la dirección del servo
       (útil si la cámara apunta opuesto al brazo del servo) */
    if (INVERTIR_SENTIDO) error_px = -error_px;

    /* Deadband: no mover si el error es muy pequeño */
    if (abs(error_px) <= DEADBAND_PX) {
        return;
    }

    /* Normalizar error a rango [-1.0, 1.0] */
    float error_ratio = (float)error_px / (float)(ancho_frame / 2);
    if (error_ratio > 1.0f) error_ratio = 1.0f;
    if (error_ratio < -1.0f) error_ratio = -1.0f;

    /* Calcular delta con redondeo matemático */
    float delta_f = error_ratio * MAX_DELTA_ANGLE;
    int delta_angulo;
    if (delta_f > 0)
        delta_angulo = (int)(delta_f + 0.5f);
    else
        delta_angulo = (int)(delta_f - 0.5f);

    /* Garantizar al menos 1°/frame para errores no nulos */
    if (delta_angulo == 0) {
        delta_angulo = (error_px > 0) ? 1 : -1;
    }

    int nuevo_angulo = angulo_actual + delta_angulo;

    /* Saturar al rango físico del servo (0-180°) */
    if (nuevo_angulo < 0) nuevo_angulo = 0;
    if (nuevo_angulo > 180) nuevo_angulo = 180;

    /* Enviar el nuevo ángulo al hilo del servo */
    pthread_mutex_lock(&datos_servo.mutex);
    datos_servo.angulo_deseado = nuevo_angulo;
    datos_servo.nuevo_dato = 1;
    pthread_cond_signal(&datos_servo.cond);
    pthread_mutex_unlock(&datos_servo.mutex);

    printf("[Seguimiento] X=%d | error=%d px | delta=%.1f->%d deg | ang=%d deg\n",
           x_objeto, error_px, delta_f, delta_angulo, nuevo_angulo);
}

/* ================================================================
   HILO 2: hilo_sistema_seguimiento
   
   1. Crea memoria compartida POSIX (/shm_vision)
   2. Crea semáforo POSIX (/sem_vision)
   3. Espera datos de Python (sem_timedwait con 100ms de timeout)
   4. Lee coordenadas del objeto desde la shared memory
   5. Llama a enviar_rotacion_servo() para mover el servo
   
   El timeout de 100ms evita que C se bloquee para siempre si
   Python falla. Si no hay datos nuevos, reintenta.
   ================================================================ */
void* hilo_sistema_seguimiento(void* arg) {
    int ancho_frame = 640;       /* Valor por defecto */
    int angulo_actual = ANGULO_CENTRAL;

    printf("[Seguimiento] Inicializando Memoria Compartida...\n");

    /* Crear memoria compartida */
    int shm_fd = shm_open("/shm_vision", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error en shm_open");
        pthread_exit(NULL);
    }
    ftruncate(shm_fd, sizeof(DatosVision));
    DatosVision* datos_compartidos = mmap(0, sizeof(DatosVision), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    /* Crear semáforo (inicializado en 0 = bloqueado) */
    sem_t *sem_vision = sem_open("/sem_vision", O_CREAT, 0666, 0);
    if (sem_vision == SEM_FAILED) {
        perror("Error creando el semaforo");
        pthread_exit(NULL);
    }

    printf("[Seguimiento] Listo. Esperando datos de Python...\n");

    /* Bucle principal: espera datos y mueve el servo */
    while (1) {
        /* Calcular tiempo de timeout (100ms desde ahora) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec += 1;
        }

        /* Esperar datos de Python (sem_post) o timeout */
        if (sem_timedwait(sem_vision, &ts) == -1) {
            if (errno == ETIMEDOUT) continue;
            break;  /* Error en semáforo, salir */
        }

        /* Actualizar ancho de frame si Python lo envió */
        if (datos_compartidos->frame_width > 0) {
            ancho_frame = datos_compartidos->frame_width;
        }

        /* Bit 0 en flags = señal de terminación de Python */
        if (datos_compartidos->flags & 1) {
            printf("[Seguimiento] Senal de salida recibida de Python. Cerrando...\n");
            /* Asegurar que el servo vaya al centro antes de cerrar */
            pthread_mutex_lock(&datos_servo.mutex);
            datos_servo.angulo_deseado = 90;
            datos_servo.nuevo_dato = 1;
            pthread_cond_signal(&datos_servo.cond);
            pthread_mutex_unlock(&datos_servo.mutex);
            usleep(50000);  /* Esperar a que el servo procese */
            break;
        }

        /* Leer coordenada X del objeto */
        int objeto_x = datos_compartidos->x;

        /* Leer ángulo actual (último comando enviado al servo) */
        pthread_mutex_lock(&datos_servo.mutex);
        angulo_actual = datos_servo.angulo_deseado;
        pthread_mutex_unlock(&datos_servo.mutex);

        /* Calcular y enviar corrección */
        enviar_rotacion_servo(objeto_x, ancho_frame, angulo_actual);
    }

    /* Limpieza al salir */
    gpioServo(SERVO_PIN, 0);
    gpioTerminate();
    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");
    exit(0);
}

/* ================================================================
   MAIN — Punto de entrada
   
   1. Registra manejadores SIGINT/SIGTERM
   2. Limpia recursos viejos (por si quedaron de ejecuciones anteriores)
   3. Crea los dos hilos:
      - hilo_rotacion_servo: mueve el motor físicamente
      - hilo_sistema_seguimiento: recibe datos de Python y calcula
   4. Espera a que ambos hilos terminen (pthread_join)
   ================================================================ */
int main() {
    pthread_t thread_servo;
    pthread_t thread_seguimiento;

    /* Manejo de Ctrl+C y kill */
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("Iniciando sistema de control en C...\n");

    /* Limpiar recursos de ejecuciones anteriores */
    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");

    /* Crear hilo del servo */
    int ret;
    ret = pthread_create(&thread_servo, NULL, hilo_rotacion_servo, NULL);
    if (ret != 0) {
        fprintf(stderr, "Error creando hilo de servo: %s\n", strerror(ret));
        sem_unlink("/sem_vision");
        shm_unlink("/shm_vision");
        return 1;
    }

    /* Crear hilo de tracking */
    ret = pthread_create(&thread_seguimiento, NULL, hilo_sistema_seguimiento, NULL);
    if (ret != 0) {
        fprintf(stderr, "Error creando hilo de seguimiento: %s\n", strerror(ret));
        sem_unlink("/sem_vision");
        shm_unlink("/shm_vision");
        return 1;
    }

    /* Esperar a que ambos hilos finalicen */
    pthread_join(thread_servo, NULL);
    pthread_join(thread_seguimiento, NULL);

    return 0;
}

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

#ifdef __arm__
    #include <pigpio.h>
#else
    int gpioInitialise(void) { return 0; }
    void gpioTerminate(void) { }
    void gpioServo(unsigned gpio, unsigned pulsewidth) {
        printf("[MOCK-GPIO] Pin %d: PulseWidth %d\n", gpio, pulsewidth);
    }
#endif

typedef struct {
    int x;
    int y;
    int flags;
    int frame_width;
} DatosVision;

typedef struct {
    int angulo_deseado;
    int nuevo_dato;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} DatosServo;

DatosServo datos_servo = {
    .angulo_deseado = 90,
    .nuevo_dato = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

void cleanup(int signum) {
    const char msg[] = "\n[Sistema] Cerrando y limpiando recursos...\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    gpioServo(SERVO_PIN, 0);
    gpioTerminate();
    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");
    _exit(0);
}

void* hilo_rotacion_servo(void* arg) {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Error iniciando pigpio\n");
        pthread_exit(NULL);
    }

    gpioServo(SERVO_PIN, 1500);

    while (1) {
        pthread_mutex_lock(&datos_servo.mutex);

        while (datos_servo.nuevo_dato == 0) {
            pthread_cond_wait(&datos_servo.cond, &datos_servo.mutex);
        }

        int angulo_a_mover = datos_servo.angulo_deseado;
        datos_servo.nuevo_dato = 0;

        pthread_mutex_unlock(&datos_servo.mutex);

        if (angulo_a_mover < 0) angulo_a_mover = 0;
        if (angulo_a_mover > 180) angulo_a_mover = 180;

        int pulse_width = 500 + (angulo_a_mover * 2000) / 180;
        gpioServo(SERVO_PIN, pulse_width);
        printf("[Hilo Servo] Motor movido a %d grados (%d us)\n", angulo_a_mover, pulse_width);
    }

    gpioTerminate();
    pthread_exit(NULL);
}

void enviar_rotacion_servo(int x_objeto, int ancho_frame, int angulo_actual) {
    int centro = ancho_frame / 2;
    int error_px = x_objeto - centro;

    if (INVERTIR_SENTIDO) error_px = -error_px;

    if (abs(error_px) <= DEADBAND_PX) {
        return;
    }

    int angulo_target = ANGULO_CENTRAL + (int)((float)error_px * GAIN);

    if (angulo_target < 0)   angulo_target = 0;
    if (angulo_target > 180) angulo_target = 180;

    int delta_angulo = angulo_target - angulo_actual;
    if (delta_angulo > MAX_DELTA_ANGLE)
        delta_angulo = MAX_DELTA_ANGLE;
    if (delta_angulo < -MAX_DELTA_ANGLE)
        delta_angulo = -MAX_DELTA_ANGLE;

    int nuevo_angulo = angulo_actual + delta_angulo;

    pthread_mutex_lock(&datos_servo.mutex);
    datos_servo.angulo_deseado = nuevo_angulo;
    datos_servo.nuevo_dato = 1;
    pthread_cond_signal(&datos_servo.cond);
    pthread_mutex_unlock(&datos_servo.mutex);

    printf("[Seguimiento] X=%d | error=%d px | target=%d deg | ang=%d deg\n",
           x_objeto, error_px, angulo_target, nuevo_angulo);
}

void* hilo_sistema_seguimiento(void* arg) {
    int ancho_frame = 640;
    int angulo_actual = ANGULO_CENTRAL;

    printf("[Seguimiento] Inicializando Memoria Compartida...\n");

    int shm_fd = shm_open("/shm_vision", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error en shm_open");
        pthread_exit(NULL);
    }
    ftruncate(shm_fd, sizeof(DatosVision));
    DatosVision* datos_compartidos = mmap(0, sizeof(DatosVision), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    sem_t *sem_vision = sem_open("/sem_vision", O_CREAT, 0666, 0);
    if (sem_vision == SEM_FAILED) {
        perror("Error creando el semaforo");
        pthread_exit(NULL);
    }

    printf("[Seguimiento] Listo. Esperando datos de Python...\n");

    while (1) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000;  // 100ms timeout
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec += 1;
        }

        if (sem_timedwait(sem_vision, &ts) == -1) {
            if (errno == ETIMEDOUT) continue;
            break;
        }

        if (datos_compartidos->frame_width > 0) {
            ancho_frame = datos_compartidos->frame_width;
        }

        if (datos_compartidos->flags & 1) {
            printf("[Seguimiento] Senal de salida recibida de Python. Cerrando...\n");
            pthread_mutex_lock(&datos_servo.mutex);
            datos_servo.angulo_deseado = 90;
            datos_servo.nuevo_dato = 1;
            pthread_cond_signal(&datos_servo.cond);
            pthread_mutex_unlock(&datos_servo.mutex);
            usleep(50000);
            break;
        }

        int objeto_x = datos_compartidos->x;

        pthread_mutex_lock(&datos_servo.mutex);
        angulo_actual = datos_servo.angulo_deseado;
        pthread_mutex_unlock(&datos_servo.mutex);

        enviar_rotacion_servo(objeto_x, ancho_frame, angulo_actual);
    }

    gpioServo(SERVO_PIN, 0);
    gpioTerminate();
    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");
    exit(0);
}

int main() {
    pthread_t thread_servo;
    pthread_t thread_seguimiento;

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("Iniciando sistema de control en C...\n");

    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");

    int ret;
    ret = pthread_create(&thread_servo, NULL, hilo_rotacion_servo, NULL);
    if (ret != 0) {
        fprintf(stderr, "Error creando hilo de servo: %s\n", strerror(ret));
        sem_unlink("/sem_vision");
        shm_unlink("/shm_vision");
        return 1;
    }

    ret = pthread_create(&thread_seguimiento, NULL, hilo_sistema_seguimiento, NULL);
    if (ret != 0) {
        fprintf(stderr, "Error creando hilo de seguimiento: %s\n", strerror(ret));
        sem_unlink("/sem_vision");
        shm_unlink("/shm_vision");
        return 1;
    }

    pthread_join(thread_servo, NULL);
    pthread_join(thread_seguimiento, NULL);

    return 0;
}

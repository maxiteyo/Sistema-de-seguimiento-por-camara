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

// Mock de pigpio para poder compilar en PC sin Raspberry
#ifdef __arm__
    #include <pigpio.h>
#else
    // Simulamos las funciones para que compile en Laptop
    int gpioInitialise(void) { return 0; }
    void gpioTerminate(void) { }
    void gpioServo(unsigned gpio, unsigned pulsewidth) { 
        printf("[MOCK-GPIO] Pin %d: PulseWidth %d\n", gpio, pulsewidth); 
    }
#endif

#define SERVO_PIN 18

// --- Parámetros del sistema de seguimiento ---
#define FOV_HORIZONTAL    60.0f   // Campo de visión horizontal de la cámara (°)
#define DEADBAND_PX       15      // Zona muerta (± píxeles alrededor del centro)
#define MAX_DELTA_ANGLE   5       // Máximo cambio angular por frame (°)
#define ANGULO_CENTRAL    90      // Posición central del servo (°)

// Estructura para la Memoria Compartida con Python
typedef struct {
    int x;
    int y;
    int flags;       // bit 0: quit signal (1 = salir)
    int frame_width; // ancho real del frame desde Python
} DatosVision;

// Estructura compartida entre los hilos de C
typedef struct {
    int angulo_deseado;
    int nuevo_dato; // Bandera para saber si hay un ángulo nuevo
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} DatosServo;

// Variable global instanciada para compartir entre el sistema de seguimiento y el servo
DatosServo datos_servo = {
    .angulo_deseado = 90, // Posición central inicial
    .nuevo_dato = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Manejador para cerrar el programa limpiamente
void cleanup(int signum) {
    printf("\n[Sistema] Cerrando y limpiando recursos...\n");
    gpioServo(SERVO_PIN, 0); // Detener el servo
    gpioTerminate();
    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");
    exit(0);
}

// --- HILO 1: Sistema de rotación del servomotor ---
void* hilo_rotacion_servo(void* arg) {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Error iniciando pigpio\n");
        pthread_exit(NULL);
    }

    // Inicializa el servo en el medio (90 grados -> 1500 us)
    gpioServo(SERVO_PIN, 1500);

    while (1) {
        pthread_mutex_lock(&datos_servo.mutex);
        
        // Esperamos dormidos hasta que el otro hilo nos avise que hay un nuevo dato
        while (datos_servo.nuevo_dato == 0) {
            pthread_cond_wait(&datos_servo.cond, &datos_servo.mutex);
        }

        // Leemos el dato compartido
        int angulo_a_mover = datos_servo.angulo_deseado;
        datos_servo.nuevo_dato = 0; // Bajamos la bandera
        
        pthread_mutex_unlock(&datos_servo.mutex);

        // Limitamos para no forzar o romper el servo
        if (angulo_a_mover < 0) angulo_a_mover = 0;
        if (angulo_a_mover > 180) angulo_a_mover = 180;

        // Mapeo: 0° -> 500 us | 180° -> 2500 us
        int pulse_width = 500 + (angulo_a_mover * 2000) / 180;

        // Movemos el motor
        gpioServo(SERVO_PIN, pulse_width);
        printf("[Hilo Servo] Motor movido a %d grados (%d us)\n", angulo_a_mover, pulse_width);
    }

    gpioTerminate();
    pthread_exit(NULL);
}

// -------------------------------------------------------------------
// Función: enviar_rotacion_servo
// Calcula el nuevo ángulo y lo envía al hilo del servomotor.
// -------------------------------------------------------------------
void enviar_rotacion_servo(int x_objeto, int ancho_frame, int angulo_actual) {
    int centro = ancho_frame / 2;
    int error_px = x_objeto - centro;

    // Zona muerta: ignorar errores muy pequeños
    if (abs(error_px) <= DEADBAND_PX) {
        return;
    }

    // Calcular ángulo objetivo directamente desde el centro (proporcional)
    float grados_por_pixel = FOV_HORIZONTAL / (float)ancho_frame;
    int angulo_target = ANGULO_CENTRAL + (int)((float)error_px * grados_por_pixel);

    // Saturación a los límites físicos del servo
    if (angulo_target < 0)   angulo_target = 0;
    if (angulo_target > 180) angulo_target = 180;

    // Limitar cambio angular máximo por frame (slew rate)
    int delta_angulo = angulo_target - angulo_actual;
    if (delta_angulo > MAX_DELTA_ANGLE)
        delta_angulo = MAX_DELTA_ANGLE;
    if (delta_angulo < -MAX_DELTA_ANGLE)
        delta_angulo = -MAX_DELTA_ANGLE;

    int nuevo_angulo = angulo_actual + delta_angulo;

    // Envío al hilo servomotor con exclusión mutua
    pthread_mutex_lock(&datos_servo.mutex);
    datos_servo.angulo_deseado = nuevo_angulo;
    datos_servo.nuevo_dato = 1;
    pthread_cond_signal(&datos_servo.cond);
    pthread_mutex_unlock(&datos_servo.mutex);

    printf("[Seguimiento] X=%d | error=%d px | target=%d° | ang=%d°\n",
           x_objeto, error_px, angulo_target, nuevo_angulo);
}

// --- HILO 2: Sistema de seguimiento (procesamiento de imagen IPC) ---
void* hilo_sistema_seguimiento(void* arg) {
    int ancho_frame = 640;
    int angulo_actual = ANGULO_CENTRAL;

    printf("[Seguimiento] Inicializando Memoria Compartida...\n");

    // 1. Crear Memoria Compartida
    int shm_fd = shm_open("/shm_vision", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error en shm_open");
        pthread_exit(NULL);
    }
    ftruncate(shm_fd, sizeof(DatosVision));
    DatosVision* datos_compartidos = mmap(0, sizeof(DatosVision), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // 2. Crear Semáforo POSIX
    sem_t *sem_vision = sem_open("/sem_vision", O_CREAT, 0666, 0);
    if (sem_vision == SEM_FAILED) {
        perror("Error creando el semáforo");
        pthread_exit(NULL);
    }

    printf("[Seguimiento] Listo. Esperando datos de Python...\n");

    while (1) {
        sem_wait(sem_vision);

        // Actualizar ancho del frame desde Python
        if (datos_compartidos->frame_width > 0) {
            ancho_frame = datos_compartidos->frame_width;
        }

        // Señal de salida desde Python (tecla Q)
        if (datos_compartidos->flags & 1) {
            printf("[Seguimiento] Señal de salida recibida de Python. Cerrando...\n");
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

    // Registrar el manejador de señales (Ctrl+C)
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("Iniciando sistema de control en C...\n");

    // Limpieza inicial por si acaso
    shm_unlink("/shm_vision");
    sem_unlink("/sem_vision");

    pthread_create(&thread_servo, NULL, hilo_rotacion_servo, NULL);
    pthread_create(&thread_seguimiento, NULL, hilo_sistema_seguimiento, NULL);

    pthread_join(thread_servo, NULL);
    pthread_join(thread_seguimiento, NULL);

    return 0;
}
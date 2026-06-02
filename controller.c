#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <pigpio.h>
#include <unistd.h>
#include <math.h>

#define SERVO_PIN 18

// --- Parámetros del sistema de seguimiento ---
#define FOV_HORIZONTAL    60.0f   // Campo de visión horizontal de la cámara (°)
#define DEADBAND_PX       15      // Zona muerta (± píxeles alrededor del centro)
#define MAX_DELTA_ANGLE   5       // Máximo cambio angular por frame (°)
#define ANGULO_CENTRAL    90      // Posición central del servo (°)

// Estructura compartida entre los hilos
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
// Parámetros:
//   x_objeto    - Coordenada X del centro del objeto detectado (px)
//   ancho_frame - Ancho del frame capturado por la cámara (px)
//   angulo_actual - Ángulo actual del servo (grados, 0-180)
// -------------------------------------------------------------------
void enviar_rotacion_servo(int x_objeto, int ancho_frame, int angulo_actual) {
    int centro = ancho_frame / 2;
    int error_px = x_objeto - centro;

    // Zona muerta: ignorar errores muy pequeños
    if (abs(error_px) <= DEADBAND_PX) {
        return;
    }

    // Convertir error en píxeles a grados usando el FOV horizontal
    float grados_por_pixel = FOV_HORIZONTAL / (float)ancho_frame;
    float delta_angulo = (float)error_px * grados_por_pixel;

    // Limitar cambio angular máximo por frame (slew rate)
    if (delta_angulo > MAX_DELTA_ANGLE)
        delta_angulo = MAX_DELTA_ANGLE;
    if (delta_angulo < -MAX_DELTA_ANGLE)
        delta_angulo = -MAX_DELTA_ANGLE;

    int nuevo_angulo = angulo_actual + (int)delta_angulo;

    // Saturación a los límites físicos del servo
    if (nuevo_angulo < 0)   nuevo_angulo = 0;
    if (nuevo_angulo > 180) nuevo_angulo = 180;

    // Envío al hilo servomotor con exclusión mutua
    pthread_mutex_lock(&datos_servo.mutex);
    datos_servo.angulo_deseado = nuevo_angulo;
    datos_servo.nuevo_dato = 1;
    pthread_cond_signal(&datos_servo.cond);
    pthread_mutex_unlock(&datos_servo.mutex);

    printf("[Seguimiento] X=%d | error=%d px | delta=%.1f° | ang=%d°\n",
           x_objeto, error_px, delta_angulo, nuevo_angulo);
}

// --- HILO 2: Sistema de seguimiento (procesamiento de imagen) ---
void* hilo_sistema_seguimiento(void* arg) {
    int ancho_frame = 640;
    int angulo_actual = ANGULO_CENTRAL;

    printf("[Seguimiento] Iniciando captura (%d px de ancho)...\n", ancho_frame);

    while (1) {
        // ---------------------------------------------------------------
        // BLOQUE DE ADQUISICIÓN
        // El hilo de captura deposita el frame en un buffer compartido.


        // ---------------------------------------------------------------
        // BLOQUE DE PROCESAMIENTO
        // Detección del objeto por color usando OpenCV.

        // Actualizar ángulo local desde la variable compartida
        pthread_mutex_lock(&datos_servo.mutex);
        angulo_actual = datos_servo.angulo_deseado;
        pthread_mutex_unlock(&datos_servo.mutex);

        usleep(33000); // ~33 ms → ~30 FPS
    }

    pthread_exit(NULL);
}

int main() {
    pthread_t thread_servo;
    pthread_t thread_seguimiento;

    printf("Iniciando sistema de seguimiento por cámara...\n");

    // 1. Iniciar el hilo del Servomotor
    pthread_create(&thread_servo, NULL, hilo_rotacion_servo, NULL);

    // 2. Iniciar el hilo de Seguimiento (procesamiento de imagen)
    pthread_create(&thread_seguimiento, NULL, hilo_sistema_seguimiento, NULL);

    // Esperamos a que los hilos terminen (loops infinitos)
    pthread_join(thread_servo, NULL);
    pthread_join(thread_seguimiento, NULL);

    return 0;
}

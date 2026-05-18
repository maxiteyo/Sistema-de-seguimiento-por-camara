#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <pigpio.h>
#include <unistd.h>

#define SERVO_PIN 18

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

// PENDIENTE: Hilo de Seguimiento real
// void* hilo_sistema_seguimiento(void* arg) { ... }

int main() {
    pthread_t thread_servo;
    // pthread_t thread_seguimiento;

    printf("Iniciando sistema de servo...\n");

    // 1. Iniciar el hilo del Servomotor (Tu parte)
    pthread_create(&thread_servo, NULL, hilo_rotacion_servo, NULL);

    // 2. Iniciar el hilo de Seguimiento (Parte de tu compañero)
    // pthread_create(&thread_seguimiento, NULL, hilo_sistema_seguimiento, NULL);

    // Esperamos a que los hilos terminen (En este caso thread_servo es un loop infinito)
    pthread_join(thread_servo, NULL);
    
    return 0;
}

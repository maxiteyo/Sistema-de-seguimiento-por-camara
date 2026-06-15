/* ================================================================
   defines.h — Constantes compartidas entre controller.c y colorv4.py
   IUA — Proyecto final de Sistemas de Tiempo Real
   
   Todas las constantes de configuración del sistema están centralizadas
   aquí. Modificar este archivo afecta tanto al controlador C como al
   pipeline de visión Python.
   ================================================================ */

#ifndef DEFINES_H
#define DEFINES_H

/* Pin GPIO donde está conectada la señal del servo MG996R */
#define SERVO_PIN           18

/* Deadband (zona muerta) en píxeles.
   Si el objeto está dentro de ±DEADBAND_PX del centro del frame,
   el servo no se mueve. Evita micro-correcciones por ruido. */
#define DEADBAND_PX         15

/* Máximo cambio de ángulo por frame (en grados).
   Controla la velocidad del servo: 5°/frame a ~15 FPS = ~75°/s.
   Actúa como slew rate limiter: el servo no puede girar más de
   esta cantidad en un solo paso. */
#define MAX_DELTA_ANGLE     5

/* Invertir sentido de giro del servo.
   0 = seguimiento normal (objeto a derecha → servo gira a derecha)
   1 = invertido (útil si la cámara apunta en dirección opuesta al servo) */
#define INVERTIR_SENTIDO    1

/* Ángulo central del servo (posición de reposo, en grados).
   Cuando el objeto está centrado, el servo apunta aquí. */
#define ANGULO_CENTRAL      90

#endif

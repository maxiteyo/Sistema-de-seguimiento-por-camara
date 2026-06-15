"""
colorv4.py — Pipeline de visión por color y comunicación IPC
IUA — Proyecto final de Sistemas de Tiempo Real

Captura video de la cámara web, aplica filtrado HSV para detectar
objetos rojos/verdes/azules, y envía las coordenadas del objeto
más grande al controlador en C mediante memoria compartida POSIX.

Requiere: opencv-python, numpy, posix-ipc
Ejecutar DESPUÉS de ./controller (que crea la shared memory).
"""

import cv2
import numpy as np
import posix_ipc
import mmap
import struct
import sys
import time

# ================================================================
# CONEXIÓN IPC CON EL CONTROLADOR EN C
# Se conecta a la memoria compartida /shm_vision y al semáforo
# /sem_vision creados por controller.c.
# Si el C no está corriendo, se muestra un error y se sale.
# ================================================================
print("Conectando con el controlador en C...")
try:
    shm = posix_ipc.SharedMemory("/shm_vision")
    memoria_mapeada = mmap.mmap(shm.fd, shm.size)
    shm.close_fd()

    semaforo = posix_ipc.Semaphore("/sem_vision")
    print("Conexion IPC establecida exitosamente.")
except posix_ipc.ExistentialError:
    print("ERROR: No se encontro la memoria compartida.")
    print("Asegurate de ejecutar primero el programa en C ('./controller')")
    sys.exit(1)

# ================================================================
# INICIALIZACIÓN DE LA CÁMARA
# La cámara devuelve 640x360 nativamente (formato de la webcam USB).
# Se procesa a 320x240 en software para mejor rendimiento.
# Nota: 320x240 no es soportado por hardware, se hace resize.
# ================================================================
webcam = cv2.VideoCapture(0)
webcam.set(cv2.CAP_PROP_FRAME_WIDTH, 320)
webcam.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)
actual_ancho = int(webcam.get(cv2.CAP_PROP_FRAME_WIDTH))
print(f"Camara iniciada: {actual_ancho}x{int(webcam.get(cv2.CAP_PROP_FRAME_HEIGHT))}")

# ================================================================
# BUCLE PRINCIPAL DE VISIÓN
# ================================================================
try:
    # Color activo al iniciar (rojo por defecto)
    current_color = "red"

    # Kernel morfológico 5x5 para operaciones de limpieza de máscara
    kernel = np.ones((5, 5), np.uint8)

    # Ventana de visualización (redimensionable)
    cv2.namedWindow("Color Tracker", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Color Tracker", 960, 720)

    # Filtro EMA (Exponential Moving Average) para suavizar la posición X
    # Inicia en 0, se inicializa con el primer valor detectado
    filtro_cx = 0

    # ================================================================
    # CONFIGURACIÓN DE COLORES HSV
    #
    # Cada color tiene:
    #   - lower/upper: rango HSV para cv2.inRange()
    #   - box: color del rectángulo en BGR (para la visualización)
    #   - text: etiqueta del color
    #
    # El rojo en OpenCV HSV envuelve alrededor de H=0, por lo que
    # necesita DOS rangos combinados con bitwise_or.
    #
    # Valores de Saturation y Value se ajustaron experimentalmente:
    #   - Rojo: S/V >= 120 (más selectivo, evita falsos positivos)
    #   - Verde/Azul: S/V >= 80
    # ================================================================
    colors = {
        "red": {
            "lower1": np.array([0, 120, 120], np.uint8),
            "upper1": np.array([10, 255, 255], np.uint8),
            "lower2": np.array([160, 120, 120], np.uint8),
            "upper2": np.array([180, 255, 255], np.uint8),
            "box": (0, 0, 255),
            "text": "RED"
        },
        "green": {
            "lower": np.array([35, 80, 80], np.uint8),
            "upper": np.array([85, 255, 255], np.uint8),
            "box": (0, 255, 0),
            "text": "GREEN"
        },
        "blue": {
            "lower": np.array([94, 80, 80], np.uint8),
            "upper": np.array([120, 255, 255], np.uint8),
            "box": (255, 0, 0),
            "text": "BLUE"
        }
    }

    # Dimensiones de procesamiento (320x240)
    PROC_W = 320
    PROC_H = 240

    while True:

        # --- Capturar frame ---
        ret, frame0 = webcam.read()
        if not ret:
            break

        # Redimensionar a resolución de proceso
        frame = cv2.resize(frame0, (PROC_W, PROC_H))
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        cfg = colors[current_color]

        # --- Aplicar máscara HSV según el color activo ---
        if current_color == "red":
            # Rojo: dos rangos combinados (H envuelve alrededor de 0)
            mask1 = cv2.inRange(hsv, cfg["lower1"], cfg["upper1"])
            mask2 = cv2.inRange(hsv, cfg["lower2"], cfg["upper2"])
            mask = cv2.bitwise_or(mask1, mask2)
        else:
            # Verde/Azul: un solo rango
            mask = cv2.inRange(hsv, cfg["lower"], cfg["upper"])

        # --- Limpieza morfológica de la máscara ---
        # MORPH_OPEN = erode + dilate: elimina ruido blanco aislado
        # MORPH_CLOSE = dilate + erode: rellena huecos dentro del objeto
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

        # --- Encontrar contornos ---
        # RETR_EXTERNAL: solo contornos exteriores (ignora contornos anidados)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        # --- Seleccionar el contorno más grande (área > 300 px²) ---
        largest = None
        largest_area = 0

        for contour in contours:
            area = cv2.contourArea(contour)
            if area > 300 and area > largest_area:
                largest_area = area
                largest = contour

        # --- Referencias visuales: líneas de zona izquierda/derecha ---
        altura, ancho = frame.shape[:2]
        limite_izq = int(ancho * 0.15)
        limite_der = int(ancho * 0.85)
        estado = "BUSCANDO..."

        # ================================================================
        # SI SE DETECTÓ UN OBJETO
        # ================================================================
        if largest is not None:
            # Obtener centro del objeto (bounding box)
            x, y, w, h = cv2.boundingRect(largest)
            cx = x + w // 2
            cy = y + h // 2

            # ================================================================
            # FILTRO EMA (Exponential Moving Average)
            # Suaviza la coordenada X para evitar que el servo tiembla por
            # cambios bruscos en el contorno entre frames consecutivos.
            #
            # Fórmula: filtro = 0.7 * anterior + 0.3 * nuevo
            # El 70/30 da buena estabilidad sin agregar mucha latencia.
            # ================================================================
            if filtro_cx == 0:
                filtro_cx = cx  # Inicializar con el primer valor detectado
            filtro_cx = int(filtro_cx * 0.7 + cx * 0.3)
            cx = filtro_cx

            # --- Enviar coordenadas al controlador C ---
            # Estructura: (x, y, flags=0, frame_width)
            # flags=0 significa "datos normales" (sin señal de salida)
            memoria_mapeada.seek(0)
            memoria_mapeada.write(struct.pack('iiii', cx, cy, 0, PROC_W))

            # Notificar al proceso C que hay nuevos datos
            semaforo.release()

            # --- Dibujar rectángulo y centro en la visualización ---
            cv2.rectangle(frame, (x, y), (x + w, y + h), cfg["box"], 3)
            cv2.circle(frame, (cx, cy), 6, (255, 255, 255), -1)

            # Determinar estado: IZQUIERDA / DERECHA / CENTRO
            if cx < limite_izq:
                estado = "IZQUIERDA"
            elif cx > limite_der:
                estado = "DERECHA"
            else:
                estado = "CENTRO"

        # ================================================================
        # VISUALIZACIÓN EN PANTALLA
        # ================================================================
        try:
            # Líneas verticales que marcan las zonas
            cv2.line(frame, (limite_izq, 0), (limite_izq, altura), (0, 255, 255), 2)
            cv2.line(frame, (limite_der, 0), (limite_der, altura), (0, 255, 255), 2)

            # Texto informativo
            cv2.putText(frame, f"Tracking: {cfg['text']}", (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 1, cfg["box"], 2)
            cv2.putText(frame, f"Estado: {estado}", (10, 80), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
            if largest is not None:
                cv2.putText(frame, f"X:{cx}  Y:{cy}", (10, 120), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)

            cv2.putText(frame, "R=Rojo G=Verde B=Azul Q=Salir", (10, frame.shape[0] - 15), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        except Exception as e:
            print(f"Error en visualizacion: {e}")

        # Mostrar frame en la ventana
        cv2.imshow("Color Tracker", frame)

        # ================================================================
        # TECLADO
        # R = Rojo, G = Verde, B = Azul, Q = Salir
        # Al presionar Q, se envía flags=1 al C para que cierre ordenadamente
        # ================================================================
        key = cv2.waitKey(1) & 0xFF
        if key == ord('r'):
            current_color = "red"
        elif key == ord('g'):
            current_color = "green"
        elif key == ord('b'):
            current_color = "blue"
        elif key == ord('q'):
            # Enviar señal de salida (flags=1) al controlador C
            memoria_mapeada.seek(0)
            memoria_mapeada.write(struct.pack('iiii', 0, 0, 1, PROC_W))
            semaforo.release()
            break

except KeyboardInterrupt:
    print("\nDeteniendo sistema...")

# ================================================================
# LIMPIEZA DE RECURSOS
# Se liberan: cámara, ventana OpenCV, memoria compartida, semáforo
# ================================================================
finally:
    webcam.release()
    cv2.destroyAllWindows()
    if 'memoria_mapeada' in locals():
        memoria_mapeada.close()
    if 'semaforo' in locals():
        semaforo.close()
    print("Recursos liberados correctamente.")

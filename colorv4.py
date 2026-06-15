import cv2
import numpy as np
import posix_ipc
import mmap
import struct
import sys
import time

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

webcam = cv2.VideoCapture(0)
webcam.set(cv2.CAP_PROP_FRAME_WIDTH, 320)
webcam.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)
actual_ancho = int(webcam.get(cv2.CAP_PROP_FRAME_WIDTH))
print(f"Camara iniciada: {actual_ancho}x{int(webcam.get(cv2.CAP_PROP_FRAME_HEIGHT))}")

try:
    current_color = "red"
    kernel = np.ones((5, 5), np.uint8)
    cv2.namedWindow("Color Tracker", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Color Tracker", 960, 720)
    filtro_cx = 0

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

    PROC_W = 320
    PROC_H = 240

    while True:

        ret, frame0 = webcam.read()
        if not ret:
            break

        frame = cv2.resize(frame0, (PROC_W, PROC_H))
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        cfg = colors[current_color]

        if current_color == "red":
            mask1 = cv2.inRange(hsv, cfg["lower1"], cfg["upper1"])
            mask2 = cv2.inRange(hsv, cfg["lower2"], cfg["upper2"])
            mask = cv2.bitwise_or(mask1, mask2)
        else:
            mask = cv2.inRange(hsv, cfg["lower"], cfg["upper"])

        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        largest = None
        largest_area = 0

        for contour in contours:
            area = cv2.contourArea(contour)
            if area > 300 and area > largest_area:
                largest_area = area
                largest = contour

        altura, ancho = frame.shape[:2]
        limite_izq = int(ancho * 0.15)
        limite_der = int(ancho * 0.85)
        estado = "BUSCANDO..."

        if largest is not None:
            x, y, w, h = cv2.boundingRect(largest)
            cx = x + w // 2
            cy = y + h // 2
            if filtro_cx == 0:
                filtro_cx = cx
            filtro_cx = (filtro_cx + cx) // 2
            cx = filtro_cx

            memoria_mapeada.seek(0)
            memoria_mapeada.write(struct.pack('iiii', cx, cy, 0, PROC_W))

            semaforo.release()

            cv2.rectangle(frame, (x, y), (x + w, y + h), cfg["box"], 3)
            cv2.circle(frame, (cx, cy), 6, (255, 255, 255), -1)

            if cx < limite_izq:
                estado = "IZQUIERDA"
            elif cx > limite_der:
                estado = "DERECHA"
            else:
                estado = "CENTRO"

        try:
            cv2.line(frame, (limite_izq, 0), (limite_izq, altura), (0, 255, 255), 2)
            cv2.line(frame, (limite_der, 0), (limite_der, altura), (0, 255, 255), 2)

            cv2.putText(frame, f"Tracking: {cfg['text']}", (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 1, cfg["box"], 2)
            cv2.putText(frame, f"Estado: {estado}", (10, 80), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
            if largest is not None:
                cv2.putText(frame, f"X:{cx}  Y:{cy}", (10, 120), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)

            cv2.putText(frame, "R=Rojo G=Verde B=Azul Q=Salir", (10, frame.shape[0] - 15), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        except Exception as e:
            print(f"Error en visualizacion: {e}")

        cv2.imshow("Color Tracker", frame)

        key = cv2.waitKey(1) & 0xFF
        if key == ord('r'):
            current_color = "red"
        elif key == ord('g'):
            current_color = "green"
        elif key == ord('b'):
            current_color = "blue"
        elif key == ord('q'):
            memoria_mapeada.seek(0)
            memoria_mapeada.write(struct.pack('iiii', 0, 0, 1, PROC_W))
            semaforo.release()
            break
except KeyboardInterrupt:
    print("\nDeteniendo sistema...")
finally:
    webcam.release()
    cv2.destroyAllWindows()
    if 'memoria_mapeada' in locals():
        memoria_mapeada.close()
    if 'semaforo' in locals():
        semaforo.close()
    print("Recursos liberados correctamente.")

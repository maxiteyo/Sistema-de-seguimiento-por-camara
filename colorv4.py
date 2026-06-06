import cv2
import numpy as np

# CONFIGURACION
webcam = cv2.VideoCapture(0)

current_color = "red"
ultimo_estado = ""

kernel = np.ones((5, 5), np.uint8)

# BUCLE PRINCIPAL
while True:

    ret, frame = webcam.read()

    if not ret:
        break

    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # COLORES DISPONIBLES
    colors = {
        "red": {
            "lower": np.array([136, 87, 111], np.uint8),
            "upper": np.array([180, 255, 255], np.uint8),
            "box": (0, 0, 255),
            "text": "RED"
        },
        "green": {
            "lower": np.array([25, 52, 72], np.uint8),
            "upper": np.array([102, 255, 255], np.uint8),
            "box": (0, 255, 0),
            "text": "GREEN"
        },
        "blue": {
            "lower": np.array([94, 80, 2], np.uint8),
            "upper": np.array([120, 255, 255], np.uint8),
            "box": (255, 0, 0),
            "text": "BLUE"
        }
    }

    cfg = colors[current_color]

    # DETECCION DEL COLOR
    mask = cv2.inRange(
        hsv,
        cfg["lower"],
        cfg["upper"]
    )

    mask = cv2.dilate(mask, kernel)

    contours, _ = cv2.findContours(
        mask,
        cv2.RETR_TREE,
        cv2.CHAIN_APPROX_SIMPLE
    )

    # BUSCAR EL OBJETO MAS GRANDE
    largest = None
    largest_area = 0

    for contour in contours:

        area = cv2.contourArea(contour)

        if area > 300 and area > largest_area:
            largest_area = area
            largest = contour

    # SI ENCUENTRA OBJETO
    if largest is not None:

        x, y, w, h = cv2.boundingRect(largest)

        cx = x + w // 2
        cy = y + h // 2

        # Actualizar posicion en tiempo real
        with open("posicion.txt", "w") as archivo:
            archivo.write(f"{cx},{cy}")

        cv2.rectangle(
            frame,
            (x, y),
            (x + w, y + h),
            cfg["box"],
            3
        )

        cv2.circle(
            frame,
            (cx, cy),
            6,
            (255, 255, 255),
            -1
        )

        # ZONAS DE LA PANTALLA
        altura, ancho = frame.shape[:2]

        limite_izq = int(ancho * 0.15)
        limite_der = int(ancho * 0.85)

        cv2.line(
            frame,
            (limite_izq, 0),
            (limite_izq, altura),
            (0, 255, 255),
            2
        )

        cv2.line(
            frame,
            (limite_der, 0),
            (limite_der, altura),
            (0, 255, 255),
            2
        )

        # DETERMINAR POSICION
        if cx < limite_izq:
            estado = "IZQUIERDA"

        elif cx > limite_der:
            estado = "DERECHA"

        else:
            estado = "CENTRO"

        # GUARDAR ARCHIVO
        if estado != ultimo_estado:

            with open("alerta.txt", "w") as archivo:
                archivo.write(estado)

        ultimo_estado = estado

        # MOSTRAR INFORMACION
        cv2.putText(
            frame,
            f"Tracking: {cfg['text']}",
            (10, 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            1,
            cfg["box"],
            2
        )

        cv2.putText(
            frame,
            f"Estado: {estado}",
            (10, 80),
            cv2.FONT_HERSHEY_SIMPLEX,
            1,
            (255, 255, 255),
            2
        )

        cv2.putText(
            frame,
            f"X:{cx}  Y:{cy}",
            (10, 120),
            cv2.FONT_HERSHEY_SIMPLEX,
            1,
            (255, 255, 255),
            2
        )

    # AYUDA EN PANTALLA
    cv2.putText(
        frame,
        "R=Rojo  G=Verde  B=Azul  Q=Salir",
        (10, frame.shape[0] - 15),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (255, 255, 255),
        2
    )

    cv2.imshow(
        "Color Tracker",
        frame
    )

    # TECLAS
    key = cv2.waitKey(1) & 0xFF

    if key == ord('r'):
        current_color = "red"

    elif key == ord('g'):
        current_color = "green"

    elif key == ord('b'):
        current_color = "blue"

    elif key == ord('q'):
        break


webcam.release()
cv2.destroyAllWindows()
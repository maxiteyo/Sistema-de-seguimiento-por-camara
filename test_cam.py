import cv2, os

if not os.environ.get('DISPLAY', ''):
    os.environ['DISPLAY'] = ':0'

print("1. abriendo camara...")
cap = cv2.VideoCapture(0)
print("2. abierta:", cap.isOpened())
print("3. creando ventana...")
cv2.namedWindow("test", cv2.WINDOW_NORMAL)
print("4. ventana creada")
ret, frame = cap.read()
print("5. frame:", ret)
if ret:
    cv2.imshow("test", frame)
    print("6. mostrando. Apreta cualquier tecla...")
    cv2.waitKey(0)
cv2.destroyAllWindows()
cap.release()
print("7. ok")

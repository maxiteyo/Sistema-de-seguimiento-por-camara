# Sistema-de-seguimiento-por-camara

## Descripción General

El presente proyecto integrador consiste en el desarrollo de un sistema embebido cuyo objetivo es la detección y el seguimiento automático de objetos mediante una cámara manteniendo el objetivo dentro del campo visual del dispositivo de vigilancia, para ello debe ajustar dinámicamente su orientación para mantener el objeto seleccionado centrado en la imagen.

Para lograr este comportamiento, se realiza un procesamiento continuo de las imágenes capturadas por la cámara, detectando la posición del objeto en tiempo real. Cuando el objeto se desplaza fuera de la región central del encuadre, el sistema calcula la corrección necesaria y acciona un servomotor encargado de modificar la orientación de la cámara, permitiendo un seguimiento automático y continuo.

## Objetivos

* Detectar objetos de colores rojo, verde y azul dentro de la imagen capturada por la cámara.
* Determinar la posición del objeto detectado respecto del centro del encuadre.
* Controlar un servomotor para corregir la orientación de la cámara cuando el objeto se desplaza.
* Mantener el objeto seleccionado centrado en el campo visual durante su movimiento.
* Implementar una arquitectura concurrente que permita ejecutar las tareas de captura, procesamiento y control de forma eficiente.

## Arquitectura del Sistema

La solución fue desarrollada siguiendo una arquitectura concurrente basada en múltiples hilos de ejecución. Cada componente principal del sistema opera de manera independiente y coordinada, permitiendo la ejecución simultánea de tareas de captura de imágenes, procesamiento de video, detección de objetos y control del servomotor.

Este enfoque mejora la capacidad de respuesta del sistema y permite satisfacer los requisitos de procesamiento en tiempo real, fundamentales para aplicaciones de seguimiento automático.

## Tecnologías Utilizadas

* Lenguaje de programación C.
* Biblioteca OpenCV para procesamiento de imágenes y visión por computadora.
* Biblioteca POSIX Threads (Pthreads) para la implementación de concurrencia.
* Cámara de video para adquisición de imágenes.
* Servomotor para el control de orientación.
* Sistema operativo Linux.

## Aplicaciones

Este tipo de sistemas tiene aplicación en áreas como vigilancia automatizada, robótica móvil, sistemas de monitoreo inteligente, seguimiento de objetivos y plataformas de visión artificial en tiempo real.


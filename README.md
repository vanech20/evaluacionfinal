# Procesamiento de imágenes en Paralelo

El presente proyecto despliega un sistema distribuido para el procesamiento de imágenes, basado en un clúster de tres máquinas virtuales interconectadas mediante NFS y SSH. Empleando MPI para repartir tareas entre nodos y OpenMP para paralelizar el trabajo dentro de cada equipo, el sistema procesa archivos BMP aplicando las siguientes transformaciones:

Conversión a escala de grises
Desenfoque (blur)
Reflejo horizontal y vertical (en color y en blanco y negro)

Además, se incluye una interfaz gráfica desarrollada en PyQT5 para que usuarios sin conocimientos técnicos puedan interactuar con el sistema de manera sencilla.


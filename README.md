# Pulse Counter

Mod de Windows para Geometry Dash 2.2081 y 2.2091.

1. Completa el nivel en modo normal desde el comienzo.
2. Pulsa **Replay** en la pantalla de resultados.
3. El mod comprueba la repetición original y luego prueba los ticks anteriores y posteriores de cada salto. Marca como válido un desplazamiento si la repetición completa todavía termina el nivel. Conserva la forma grabada de cada pulsación y el resto de los controles.
4. La última repetición enseña los resultados. Las filas agrupan las posiciones válidas consecutivas incluyendo la original. El círculo queda en el punto original del clic, se omite en 7+ y hay un ding para una sola posición válida.

Los resultados dependen de la ruta grabada. Los escaneos pueden durar varios minutos, pues cada prueba empieza desde el principio. Si la repetición original se desincroniza, se cancela la medición. Los paquetes para ambas versiones están disponibles en los artefactos de GitHub Actions.

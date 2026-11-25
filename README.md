# Pulse Counter

Prototipo de mod para Geometry Dash 2.2091 y Geode 6.0.0 en Windows.

Muestra un c�rculo verde al pulsar salto y un c�rculo azul al soltarlo. El c�rculo de suelta cambia a naranja para pulsaciones cortas y puede reproducir un ding original. Los milisegundos indican la duraci�n observada seg�n el reloj del nivel. El mod observa los eventos que recibe el juego y deja que Geometry Dash procese cada entrada.

Este prototipo todav�a carece del an�lisis de ventanas de �xito. Para contar cu�ntos fotogramas permiten un clic, hace falta reproducir la partida con variaciones de cada entrada y comprobar sus resultados. La duraci�n mostrada tampoco demuestra precisi�n subframe con Click Between Steps.

## Compilar

En Windows, instala el SDK Geode v6.0.0 y su CLI, Visual Studio con C++ y CMake. Configura GEODE_SDK con la ruta del SDK. Desde esta carpeta ejecuta:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Copia el paquete .geode generado a la carpeta de mods. Comprueba en un nivel normal que aparezcan c�rculos al pulsar y soltar, que el sonido se pueda desactivar y que el juego responda igual con Click Between Steps activado y desactivado.

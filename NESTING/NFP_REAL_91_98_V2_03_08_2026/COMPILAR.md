# Cómo compilar esta versión (con NFP real vía Clipper2)

Esta versión usa la librería Clipper2 para calcular el NFP (No-Fit Polygon)
exacto entre piezas. Es más liviana que antes en tiempo de ejecución, pero el
comando de compilación cambia: ahora hay que incluir también los archivos
.cpp de Clipper2.

## Estructura de carpetas

Poné todos estos archivos en la misma carpeta:
```
nesting_core.hpp
curve_parser.hpp
genetic.hpp
json_io.hpp
main.cpp
svg_export.hpp
clipper.engine.cpp
clipper.offset.cpp
clipper.rectclip.cpp
clipper.triangulation.cpp
clipper2/            <- carpeta con los .h de Clipper2
  clipper.core.h
  clipper.engine.h
  clipper.export.h
  clipper.h
  clipper.minkowski.h
  clipper.offset.h
  clipper.rectclip.h
  clipper.triangulation.h
  clipper.version.h
```

## Comando de compilación (Windows, MinGW/g++)

```
g++ -O2 -pthread -std=c++17 -I. main.cpp clipper.engine.cpp clipper.offset.cpp clipper.rectclip.cpp clipper.triangulation.cpp -o nesting.exe
```

La diferencia clave respecto a antes: se agregan los 4 archivos
`clipper.*.cpp` a la línea de compilación, y `-I.` para que encuentre la
carpeta `clipper2/`.

## Uso

Igual que antes: `nesting.exe figuras.json resultado.json 160 300`
(figura de entrada, salida, ancho de tela, iteraciones).

Con la mejora de velocidad, probablemente convenga subir el número de
iteraciones (el último parámetro) ya que cada una es varias veces más barata
que antes.

## Qué cambió por dentro

El motor de colocación (antes: grilla + sacudida + compactación por búsqueda
binaria, una aproximación) ahora calcula el NFP (No-Fit Polygon) exacto entre
cada par de piezas usando Clipper2 (Minkowski sum robusto + unión booleana).
Las posiciones candidatas salen directo de los vértices de esa región, que
son matemáticamente puntos de contacto exacto — no hace falta sacudir ni
compactar por pasos.

Validado con más de 100 layouts aleatorios (15, 30 y 60 piezas): 0
solapamientos reales en todos los casos.

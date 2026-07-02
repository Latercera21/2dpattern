C++ Nesting Engine
├── Lee tu JSON de piezas
├── Construye polígonos (igual que ahora)
├── Grid espacial para colisiones rápidas
├── Prueba miles de posiciones por pieza
├── Algoritmo genético paralelo (OpenMP)
└── Escribe JSON con posiciones finales
Estamos intentando har un programa de nesting como audaces u otros que hacen tizadas digitales, 
qué usan? Nfp, no fit polygon?? No dogo que lo has como el texto que te mandé yo no sé cómo será ....///CONSTANTES
MARGEN=0.5
STEP=1.0
ELITE_SIZE=10

FASES GA
progress<0.08: N_MUT=12 macroProb=0.50 acceptTemp=5.0
progress<0.30: N_MUT=8 macroProb=0.35 acceptTemp=3.0
progress<0.70: N_MUT=5 macroProb=0.15 acceptTemp=1.5
else: N_MUT=3 macroProb=0.05 acceptTemp=0.5

SEMILLAS INICIALES
1. sort desc area (wh)
2. sort desc width
3. sort desc height
4. sort desc perimeter (w+h)
5. sort asc area
6. mix: big-small-big-small from semilla 1

MUTACIONES LOCALES (11)
mutSwap
mutSwapAdj
mutMove
mutMoveNear
mutSmallFront
mutBigMid
mutShuffleSeg
mutReverseSeg
mutRotateSeg
mutFitWidth
mutSimilarSwap

MUTACIONES MACRO (6)
mutBlockReinsert
mutBlockShuffle
mutSplitMerge
mutInvertHalf
mutScramble
mutRelocateBig

BUSCARPOS
xSet = {0, maxX}
- bordes de piezas colocadas: tx, tx+bb.w, tx-vbb.w, tx+bb.w-vbb.w
- 4 posiciones fijas: round(maxXr/5) para r=1..4
Para cada sx en xSet ordenado:
  ty = maxAltura + STEP
  drop = clamp(floor(maxAltura/5), 5, 20)
  while ty-drop>=0 && canPlace -> ty -= drop
  while ty-STEP>=0 && canPlace -> ty -= STEP
  slide: intentar dx=-STEP,+STEP, si puede, bajar de nuevo
  score = ty100000 + tx
  elegir menor score

EVALUACION
Para cada pieza:
vars = [original, rotate180]
para cada var: buscarPos -> score
elegir var con menor score
colocar con MARGEN
maxAltura = max(maxAltura, ty + bb.h)

SHAKE
Para cada pieza colocada:
while canPlace(poly, 0, -STEP):
subir 1 STEP

REINICIO
stuckCounter>8: macroProb+=0.15, N_MUT=max(N_MUT,10), acceptTemp=max(3.5)
stuckCounter>20: nueva semilla aleatoria

CRUCE
Para cada posicion i:
si o1[i].id == o2[i].id y no usado -> result[i]=o1[i]
else: alternar o1/o2, buscar primer no usado

BUSQUEDA LOCAL FINAL
limit = min(n-1, 20)
start = random(0, n-limit)
for i in [start, start+limit): swap(i,i+1), evaluar, si mejora -> guardar

ESTRUCTURA PIEZA
id: int
poly: vector
bb: {minX, minY, maxX, maxY, w, h}
area: float

ESTRUCTURA LAYOUT ITEM
id: int
tx, ty: float
rotada: bool

COLISIONES
1. AABB overlap check
2. pointInPoly: ray casting (even-odd rule)
3. segmentsIntersect: cross product sign test
4. A->B puntos dentro, B->A puntos dentro, aristas intersectan

ROTACION 180
x = bb.w - (p.x - bb.minX)
y = bb.h - (p.y - bb.minY)

DETERMINISMO
Sin random en buscarPos
Posiciones fijas en vez de aleatorias
Mismo orden -> mismo resultado garantizado......./// 
Yo no sé que es lo que realmente sirva pero paso esto solo para dar contexto.
 Las figuras las importaremos las que queremos hacer nesting serán figuras con curvas, cóncavas etc. 
 Como los nesting de ropa obviamente lo desarrollaremos en PC y en celular. Ahora estoy con el celular. 
 Que hacer primero. Tengo un coding c , parece un editor de c, funcionará ahí c++? Tengo termux también. 
 Al final queremos verlo en web y hasta en app. Lo usaremos tanto en celular o PC. O recomiendas otro lenguaje???
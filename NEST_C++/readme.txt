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


 ¿¿¿
 const lerp = (a,b,t) => a+(b-a)*t;
const ptLerp = (a,b,t) => ({x:lerp(a.x,b.x,t), y:lerp(a.y,b.y,t)});
const ptQuad = (p0,cp,p1,t) => ptLerp(ptLerp(p0,cp,t), ptLerp(cp,p1,t), t);

function construirPoligono(segs) {
  const poly = [];
  const PUNTOS_POR_CURVA = 8; 
  segs.forEach(seg => {
    if (seg.type === 'line') {
      poly.push({ ...seg.pts[0] }); 
    } else {
      for (let i = 0; i < PUNTOS_POR_CURVA; i++) {
        poly.push(ptQuad(seg.pts[0], seg.pts[1], seg.pts[2], i / PUNTOS_POR_CURVA));
      }
    }
  });
  return poly;
}

function polygonArea(poly) {
  let area = 0;
  for (let i = 0; i < poly.length; i++) {
    const j = (i + 1) % poly.length;
    area += poly[i].x * poly[j].y;
    area -= poly[j].x * poly[i].y;
  }
  return Math.abs(area / 2);
}

const Geo = {
  bbox(poly) {
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const p of poly) {
      if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
      if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y;
    }
    return { minX, minY, maxX, maxY, w: maxX - minX, h: maxY - minY };
  },
  translate(poly, dx, dy) { return poly.map(p => ({ x: p.x + dx, y: p.y + dy })); },
  rotate180(poly) {
    const bb = this.bbox(poly);
    return poly.map(p => ({ x: bb.w - (p.x - bb.minX), y: bb.h - (p.y - bb.minY) }));
  },
  pointInPoly(pt, poly) {
    let inside = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      const intersect = ((poly[i].y > pt.y) !== (poly[j].y > pt.y)) &&
        (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x);
      if (intersect) inside = !inside;
    }
    return inside;
  },
  segmentsIntersect(p1, p2, p3, p4) {
    const d1 = (p4.x - p3.x) * (p1.y - p3.y) - (p4.y - p3.y) * (p1.x - p3.x);
    const d2 = (p4.x - p3.x) * (p2.y - p3.y) - (p4.y - p3.y) * (p2.x - p3.x);
    const d3 = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
    const d4 = (p2.x - p1.x) * (p4.y - p1.y) - (p2.y - p1.y) * (p4.x - p1.x);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
  },
  collide(polyA, polyB) {
    const bbA = this.bbox(polyA); const bbB = this.bbox(polyB);
    if (bbA.maxX <= bbB.minX || bbA.minX >= bbB.maxX || bbA.maxY <= bbB.minY || bbA.minY >= bbB.maxY) return false;
    
    for (let i = 0; i < polyA.length; i++) if (this.pointInPoly(polyA[i], polyB)) return true;
    for (let i = 0; i < polyB.length; i++) if (this.pointInPoly(polyB[i], polyA)) return true;
    
    for (let i = 0; i < polyA.length; i++) {
      const a1 = polyA[i], a2 = polyA[(i + 1) % polyA.length];
      for (let j = 0; j < polyB.length; j++) {
        const b1 = polyB[j], b2 = polyB[(j + 1) % polyB.length];
        if (this.segmentsIntersect(a1, a2, b1, b2)) return true;
      }
    }
    return false;
  }
};

// ======================================================================
// 2. WEB WORKER (Núcleo del Algoritmo)
// ======================================================================
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d');
let piezas = [];
let TELA_W = 160;
let worker;

const workerCode = `
  const Geo = {
    bbox(poly) {
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
      for (const p of poly) {
        if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y;
      }
      return { minX, minY, maxX, maxY, w: maxX - minX, h: maxY - minY };
    },
    translate(poly, dx, dy) { return poly.map(p => ({ x: p.x + dx, y: p.y + dy })); },
    rotate180(poly) {
      const bb = this.bbox(poly);
      return poly.map(p => ({ x: bb.w - (p.x - bb.minX), y: bb.h - (p.y - bb.minY) }));
    },
    pointInPoly(pt, poly) {
      let inside = false;
      for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
        const intersect = ((poly[i].y > pt.y) !== (poly[j].y > pt.y)) &&
          (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x);
        if (intersect) inside = !inside;
      }
      return inside;
    },
    segmentsIntersect(p1, p2, p3, p4) {
      const d1 = (p4.x - p3.x) * (p1.y - p3.y) - (p4.y - p3.y) * (p1.x - p3.x);
      const d2 = (p4.x - p3.x) * (p2.y - p3.y) - (p4.y - p3.y) * (p2.x - p3.x);
      const d3 = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
      const d4 = (p2.x - p1.x) * (p4.y - p1.y) - (p2.y - p1.y) * (p4.x - p1.x);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    },
    collide(polyA, polyB) {
      const bbA = this.bbox(polyA); const bbB = this.bbox(polyB);
      if (bbA.maxX <= bbB.minX || bbA.minX >= bbB.maxX || bbA.maxY <= bbB.minY || bbA.minY >= bbB.maxY) return false;
      for (let i = 0; i < polyA.length; i++) if (this.pointInPoly(polyA[i], polyB)) return true;
      for (let i = 0; i < polyB.length; i++) if (this.pointInPoly(polyB[i], polyA)) return true;
      for (let i = 0; i < polyA.length; i++) {
        const a1 = polyA[i], a2 = polyA[(i + 1) % polyA.length];
        for (let j = 0; j < polyB.length; j++) {
          const b1 = polyB[j], b2 = polyB[(j + 1) % polyB.length];
          if (this.segmentsIntersect(a1, a2, b1, b2)) return true;
        }
      }
      return false;
    }
  };

  function canPlace(poly, x, y, colocadas, telaW, margen) {
    const testPoly = Geo.translate(poly, x + margen, y + margen);
    const bb = Geo.bbox(testPoly);
    if (bb.minX < 0 || bb.maxX > telaW || bb.minY < 0) return false;
    for (const col of colocadas) {
      const cbb_minX = col.bb.minX + col.tx + margen;
      const cbb_maxX = col.bb.maxX + col.tx + margen;
      const cbb_minY = col.bb.minY + col.ty + margen;
      const cbb_maxY = col.bb.maxY + col.ty + margen;
      if (bb.maxX <= cbb_minX || bb.minX >= cbb_maxX || bb.maxY <= cbb_minY || bb.minY >= cbb_maxY) continue;
      if (Geo.collide(testPoly, col.poly)) return false;
    }
    return true;
  }

  function tryShift(current, dx, dy, others, telaW, margen) {
    const testPoly = Geo.translate(current.poly, dx, dy);
    const bbTest = Geo.bbox(testPoly);
    if (bbTest.minX < 0 || bbTest.maxX > telaW || bbTest.minY < 0) return null;
    for (const o of others) {
      const o_minX = o.bb.minX + o.tx + margen;
      const o_maxX = o.bb.maxX + o.tx + margen;
      const o_minY = o.bb.minY + o.ty + margen;
      const o_maxY = o.bb.maxY + o.ty + margen;
      if (bbTest.maxX <= o_minX || bbTest.minX >= o_maxX || bbTest.maxY <= o_minY || bbTest.minY >= o_maxY) continue;
      if (Geo.collide(testPoly, o.poly)) return null;
    }
    return { poly: testPoly, tx: current.tx + dx, ty: current.ty + dy };
  }

  function calcularLayout(ordenPiezas, telaW) {
    const colocadas = [];
    let maxAltura = 0;
    const margen = 0.5;
    const step = 0.5; 

    for (let i = 0; i < ordenPiezas.length; i++) {
      const p = ordenPiezas[i];
      let mejorPos = { tx: 0, ty: maxAltura };
      let mejorRot = false;
      let mejorY = Infinity;

      const polyRot = Geo.rotate180(p.poly);
      const variantes = [
        { poly: p.poly, bb: p.bb, rot: false },
        { poly: polyRot, bb: Geo.bbox(polyRot), rot: true }
      ];

      let xTests = [0, telaW - p.bb.w];
      for (const col of colocadas) {
        xTests.push(col.tx); 
        xTests.push(col.tx + col.bb.w); 
        xTests.push(col.tx - p.bb.w); 
        xTests.push(col.tx + col.bb.w - p.bb.w); 
        xTests.push(col.tx + (col.bb.w - p.bb.w) / 2); 
        xTests.push(col.tx + 5); 
        xTests.push(col.tx - 5);
      }
      for(let r=0; r<6; r++) xTests.push(Math.random() * (telaW - p.bb.w));
      
      xTests = xTests.filter(x => x >= 0 && x + p.bb.w <= telaW);
      xTests = [...new Set(xTests)].sort(() => 0.5 - Math.random()); 

      for (const v of variantes) {
        for (let startX of xTests) {
          let tempX = startX;
          let tempY = maxAltura + 50;
          
          let drop = 10;
          while (tempY - drop > 0 && canPlace(v.poly, tempX, tempY - drop, colocadas, telaW, margen)) tempY -= drop;
          while (tempY > 0 && canPlace(v.poly, tempX, tempY - step, colocadas, telaW, margen)) tempY -= step;

          let encontreMejor = true;
          while (encontreMejor) {
            encontreMejor = false;
            if (canPlace(v.poly, tempX - step, tempY, colocadas, telaW, margen)) {
              let nX = tempX - step, nY = tempY;
              while (nY > 0 && canPlace(v.poly, nX, nY - step, colocadas, telaW, margen)) nY -= step;
              if (nY < tempY) { tempX = nX; tempY = nY; encontreMejor = true; }
            }
            if (canPlace(v.poly, tempX + step, tempY, colocadas, telaW, margen)) {
              let nX = tempX + step, nY = tempY;
              while (nY > 0 && canPlace(v.poly, nX, nY - step, colocadas, telaW, margen)) nY -= step;
              if (nY < tempY) { tempX = nX; tempY = nY; encontreMejor = true; }
            }
          }

          if (tempY < mejorY) {
            mejorY = tempY;
            mejorPos = { tx: tempX, ty: tempY };
            mejorRot = v.rot;
          }
        }
      }

      const v = mejorRot ? variantes[1] : variantes[0];
      const polyFinal = Geo.translate(v.poly, mejorPos.tx + margen, mejorPos.ty + margen);
      colocadas.push({ id: p.id, poly: polyFinal, tx: mejorPos.tx, ty: mejorPos.ty, bb: v.bb, rotada: mejorRot }); 
      
      if (mejorPos.ty + v.bb.h > maxAltura) maxAltura = mejorPos.ty + v.bb.h;
    }

    // SACUDIDA MEJORADA (Sin bucles infinitos)
    for (let c = 0; c < 4; c++) {
      let improved = false;
      for (let i = 0; i < colocadas.length; i++) {
        const current = colocadas[i];
        const others = colocadas.filter((_, idx) => idx !== i);
        
        let currentScore = current.ty * 10000 + current.tx;
        let bestScore = currentScore;
        let bestRes = null;
        
        const dirs = [[0, -step], [-step, 0], [step, 0]];
        for (const [dx, dy] of dirs) {
            let res = tryShift(current, dx, dy, others, telaW, margen);
            if (res) {
                let score = res.ty * 10000 + res.tx;
                if (score < bestScore) {
                    bestScore = score;
                    bestRes = res;
                }
            }
        }
        
        if (bestRes) {
            current.poly = bestRes.poly;
            current.tx = bestRes.tx;
            current.ty = bestRes.ty;
            improved = true;
        }
      }
      if (!improved) break;
    }

    let finalMaxAltura = 0;
    for(const col of colocadas) {
      const h = col.ty + col.bb.h + margen; 
      if (h > finalMaxAltura) finalMaxAltura = h;
    }

    return { 
      layout: colocadas.map(c => ({ id: c.id, tx: c.tx, ty: c.ty, rotada: c.rotada })), 
      altura: finalMaxAltura
    };
  }

  self.onmessage = function(e) {
    if (e.data.cmd === 'start') {
      const piezas = e.data.piezas;
      const telaW = e.data.telaW;
      const iteraciones = e.data.iteraciones;
      let lastUpdate = 0;
      let stagnation = 0;
      
      self.postMessage({ cmd: 'progress', msg: 'Iniciando...' });

      let mejorOrden = [...piezas].sort((a, b) => (b.bb.w * b.bb.h) - (a.bb.w * a.bb.h));
      let mejorResultado = calcularLayout(mejorOrden, telaW);
      
      let ordenActual = [...mejorOrden];
      let resultadoActual = mejorResultado;
      
      self.postMessage({ cmd: 'update', layout: resultadoActual.layout, altura: resultadoActual.altura });

      for (let i = 0; i < iteraciones; i++) {
        let nuevoOrden = [...ordenActual];
        
        if (stagnation > 25) {
            let largestIdx = 0;
            let maxArea = 0;
            for(let k=0; k<nuevoOrden.length; k++) {
                const area = nuevoOrden[k].bb.w * nuevoOrden[k].bb.h;
                if(area > maxArea) { maxArea = area; largestIdx = k; }
            }
            const movedLargest = nuevoOrden.splice(largestIdx, 1)[0];
            const insertPos = Math.floor(Math.random() * (nuevoOrden.length - 1)) + 1;
            nuevoOrden.splice(insertPos, 0, movedLargest);
            stagnation = 0;
        } 
        else if (Math.random() < 0.5) {
            const idx1 = Math.floor(Math.random() * nuevoOrden.length);
            const idx2 = Math.floor(Math.random() * nuevoOrden.length);
            const movedPiece = nuevoOrden.splice(idx1, 1)[0];
            nuevoOrden.splice(idx2, 0, movedPiece);
        } 
        else {
            const idx1 = Math.floor(Math.random() * nuevoOrden.length);
            const idx2 = Math.floor(Math.random() * nuevoOrden.length);
            [nuevoOrden[idx1], nuevoOrden[idx2]] = [nuevoOrden[idx2], nuevoOrden[idx1]];
        }
        
        let nuevoResultado = calcularLayout(nuevoOrden, telaW);
        
        let diff = nuevoResultado.altura - resultadoActual.altura;
        let temperatura = 1 - (i / iteraciones);
        
        if (diff < 0 || Math.random() < Math.exp(-diff / (temperatura * 2 + 0.05))) {
          ordenActual = nuevoOrden;
          resultadoActual = nuevoResultado;
        }
        
        if (resultadoActual.altura < mejorResultado.altura - 0.1) {
          mejorResultado = resultadoActual;
          mejorOrden = ordenActual;
          stagnation = 0; 
        } else {
          stagnation++;
        }
        
        const now = Date.now();
        if (now - lastUpdate > 800) { 
          self.postMessage({ cmd: 'update', layout: resultadoActual.layout, altura: resultadoActual.altura });
          lastUpdate = now;
        }
        
        if (i % 10 === 0) {
          self.postMessage({ cmd: 'progress', msg: 'Iteración ' + (i+1) + '/' + iteraciones + ' | Récord: ' + mejorResultado.altura.toFixed(2) + 'cm' });
        }
      }
      self.postMessage({ cmd: 'done', layout: mejorResultado.layout, altura: mejorResultado.altura });
    }
  };
`;

const workerBlob = new Blob([workerCode], { type: 'application/javascript' });

function initWorker() {
  if (worker) worker.terminate();
  worker = new Worker(URL.createObjectURL(workerBlob));
  
  // Capturador de errores: si el worker falla, nos avisará en pantalla
  worker.onerror = function(e) {
    document.getElementById('status').textContent = "Error interno en el Worker: " + e.message;
    const btn = document.getElementById('btnNesting');
    btn.disabled = false; btn.textContent = 'Ejecutar Nesting';
    document.getElementById('btnStop').classList.add('hidden');
  };
  
  worker.onmessage = function(e) {
    if (e.data.cmd === 'progress') {
      document.getElementById('status').textContent = e.data.msg;
    } else if (e.data.cmd === 'update') {
      document.getElementById('status').textContent = 'Explorando... Altura actual: ' + e.data.altura.toFixed(2) + 'cm';
      dibujarLayout(e.data.layout);
    } else if (e.data.cmd === 'done') {
      document.getElementById('status').textContent = 'Nesting completado. Altura final: ' + e.data.altura.toFixed(2) + 'cm';
      procesarResultado(e.data.layout, e.data.altura);
      const btn = document.getElementById('btnNesting');
      btn.disabled = false; btn.textContent = 'Ejecutar Nesting';
      document.getElementById('btnStop').classList.add('hidden');
    }
  };
}
initWorker();

// ======================================================================
// 3. INTERFAZ Y EVENTOS
// ======================================================================
function cargarJSON(texto) {
  const data = JSON.parse(texto);
  const pxPerCm = data.pxPerCm || 37.79527559055118;
  const figs = data.figures.filter(f => f.closed !== false && f.vertices && f.vertices.length >= 3);
  
  piezas = figs.map((fig, idx) => {
    const verts = fig.vertices.map(v => ({
      x: v.xCm !== undefined ? v.xCm : v.x / pxPerCm,
      y: v.yCm !== undefined ? v.yCm : v.y / pxPerCm
    }));
    const n = verts.length, edgeMap = {};
    fig.edges.forEach(e => { edgeMap[e.start + '_' + e.end] = e; });
    const segs = [];
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n, e = edgeMap[i + '_' + j];
      if (!e) segs.push({type:'line', pts:[{...verts[i]}, {...verts[j]}]});
      else if (!e.curved) segs.push({type:'line', pts:[{...verts[e.start]}, {...verts[e.end]}]});
      else segs.push({type:'quad', pts:[{...verts[e.start]}, {x:e.controlX/pxPerCm, y:e.controlY/pxPerCm}, {...verts[e.end]}]});
    }
    
    const poly = construirPoligono(segs);
    const minX = Math.min(...poly.map(p => p.x));
    const minY = Math.min(...poly.map(p => p.y));
    const polyN = poly.map(p => ({ x: p.x - minX, y: p.y - minY }));
    const bb = Geo.bbox(polyN);
    const area = polygonArea(polyN);
    return { id: idx, poly: polyN, bb, area };
  });
  
  document.getElementById('status').textContent = piezas.length + ' piezas cargadas. Listo para nesting.';
  document.getElementById('btnNesting').disabled = false;
  TELA_W = parseFloat(document.getElementById('inputAncho').value) || 160;
  dibujarVistaPrevia();
}

function dibujarVistaPrevia() {
  const PX = 4;
  canvas.width = TELA_W * PX;
  canvas.height = 600;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#f8f9fa'; ctx.fillRect(0,0,canvas.width, canvas.height);
  
  let currentX = 0, currentY = 0, rowMaxH = 0;
  piezas.forEach(p => {
    if (currentX + p.bb.w > TELA_W) { currentX = 0; currentY += rowMaxH + 2; rowMaxH = 0; }
    let poly = Geo.translate(p.poly, currentX, currentY);
    ctx.beginPath();
    ctx.moveTo(poly[0].x * PX, poly[0].y * PX);
    for (let i = 1; i < poly.length; i++) ctx.lineTo(poly[i].x * PX, poly[i].y * PX);
    ctx.closePath();
    ctx.fillStyle = '#e4e4e4'; ctx.fill();
    ctx.strokeStyle = '#333'; ctx.lineWidth = 1; ctx.stroke();
    currentX += p.bb.w + 2;
    if (p.bb.h > rowMaxH) rowMaxH = p.bb.h;
  });
  document.getElementById('resultPanel').classList.add('hidden');
}

function dibujarLayout(layout) {
  const PX = 4;
  let maxY = 0;
  layout.forEach(item => {
    const p = piezas.find(pz => pz.id === item.id);
    if (p) { const h = item.ty + p.bb.h; if (h > maxY) maxY = h; }
  });
  
  canvas.width = TELA_W * PX;
  canvas.height = (maxY + 5) * PX;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#fff'; ctx.fillRect(0,0,canvas.width, canvas.height);
  
  layout.forEach(item => {
    const pieza = piezas.find(p => p.id === item.id);
    if (!pieza) return;
    let poly = pieza.poly;
    if (item.rotada) poly = Geo.rotate180(poly);
    poly = Geo.translate(poly, item.tx, item.ty);
    
    ctx.beginPath();
    ctx.moveTo(poly[0].x * PX, poly[0].y * PX);
    for (let i = 1; i < poly.length; i++) ctx.lineTo(poly[i].x * PX, poly[i].y * PX);
    ctx.closePath();
    ctx.fillStyle = item.rotada ? '#cce5ff' : '#e4e4e4';
    ctx.fill();
    ctx.strokeStyle = '#333'; ctx.lineWidth = 1; ctx.stroke();
  });
}

onst lerp=(a,b,t)=>a+(b-a)*t;
const ptLerp=(a,b,t)=>({x:lerp(a.x,b.x,t),y:lerp(a.y,b.y,t)});
const ptQuad=(p0,cp,p1,t)=>ptLerp(ptLerp(p0,cp,t),ptLerp(cp,p1,t),t);

function construirPoligono(segs){
  const poly=[];
  segs.forEach(seg=>{
    if(seg.type==='line')poly.push({...seg.pts[0]});
    else for(let i=0;i<8;i++)poly.push(ptQuad(seg.pts[0],seg.pts[1],seg.pts[2],i/8));
  });
  return poly;
}
function polygonArea(poly){
  let a=0;
  for(let i=0;i<poly.length;i++){const j=(i+1)%poly.length;a+=poly[i].x*poly[j].y-poly[j].x*poly[i].y;}
  return Math.abs(a/2);
}
const GeoMain={
  bbox(poly){
    let minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity;
    for(const p of poly){if(p.x<minX)minX=p.x;if(p.x>maxX)maxX=p.x;if(p.y<minY)minY=p.y;if(p.y>maxY)maxY=p.y;}
    return{minX,minY,maxX,maxY,w:maxX-minX,h:maxY-minY};
  },
  translate(poly,dx,dy){return poly.map(p=>({x:p.x+dx,y:p.y+dy}));},
  rotate180(poly){const bb=this.bbox(poly);return poly.map(p=>({x:bb.w-(p.x-bb.minX),y:bb.h-(p.y-bb.minY)}));}
};

let piezas=[];
let TELA_W=160;
let worker=null;
let mejorOrdenGlobal=null;
let mejorAlturaGlobal=Infinity;

function actualizarMemStatus(){
  const el=document.getElementById('memStatus');
  if(mejorOrdenGlobal){
    el.textContent='Memoria: mejor conocido = '+mejorAlturaGlobal.toFixed(2)+' cm (punto de partida para proxima ejecucion)';
  }else{
    el.textContent='Sin memoria acumulada.';
  }
}

const WORKER_SRC=`
const MARGEN=0.5;
const STEP=0.5;
const Geo={
  bbox(poly){
    let minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity;
    for(const p of poly){if(p.x<minX)minX=p.x;if(p.x>maxX)maxX=p.x;if(p.y<minY)minY=p.y;if(p.y>maxY)maxY=p.y;}
    return{minX,minY,maxX,maxY,w:maxX-minX,h:maxY-minY};
  },
  translate(poly,dx,dy){return poly.map(p=>({x:p.x+dx,y:p.y+dy}));},
  rotate180(poly){const bb=this.bbox(poly);return poly.map(p=>({x:bb.w-(p.x-bb.minX),y:bb.h-(p.y-bb.minY)}));},
  pointInPoly(pt,poly){
    let inside=false;
    for(let i=0,j=poly.length-1;i<poly.length;j=i++){
      if(((poly[i].y>pt.y)!==(poly[j].y>pt.y))&&
        (pt.x<(poly[j].x-poly[i].x)*(pt.y-poly[i].y)/(poly[j].y-poly[i].y)+poly[i].x))inside=!inside;
    }
    return inside;
  },
  segmentsIntersect(p1,p2,p3,p4){
    const d1=(p4.x-p3.x)*(p1.y-p3.y)-(p4.y-p3.y)*(p1.x-p3.x);
    const d2=(p4.x-p3.x)*(p2.y-p3.y)-(p4.y-p3.y)*(p2.x-p3.x);
    const d3=(p2.x-p1.x)*(p3.y-p1.y)-(p2.y-p1.y)*(p3.x-p1.x);
    const d4=(p2.x-p1.x)*(p4.y-p1.y)-(p2.y-p1.y)*(p4.x-p1.x);
    return((d1>0&&d2<0)||(d1<0&&d2>0))&&((d3>0&&d4<0)||(d3<0&&d4>0));
  },
  collide(A,B){
    const bbA=this.bbox(A),bbB=this.bbox(B);
    if(bbA.maxX<=bbB.minX||bbA.minX>=bbB.maxX||bbA.maxY<=bbB.minY||bbA.minY>=bbB.maxY)return false;
    for(let i=0;i<A.length;i++)if(this.pointInPoly(A[i],B))return true;
    for(let i=0;i<B.length;i++)if(this.pointInPoly(B[i],A))return true;
    for(let i=0;i<A.length;i++){
      const a1=A[i],a2=A[(i+1)%A.length];
      for(let j=0;j<B.length;j++)if(this.segmentsIntersect(a1,a2,B[j],B[(j+1)%B.length]))return true;
    }
    return false;
  }
};

function canPlace(poly,x,y,colocadas,telaW){
  const tp=Geo.translate(poly,x+MARGEN,y+MARGEN);
  const bb=Geo.bbox(tp);
  if(bb.minX<0||bb.maxX>telaW||bb.minY<0)return false;
  for(const col of colocadas){
    if(bb.maxX<=col.bbF.minX||bb.minX>=col.bbF.maxX||bb.maxY<=col.bbF.minY||bb.minY>=col.bbF.maxY)continue;
    if(Geo.collide(tp,col.poly))return false;
  }
  return true;
}

function buscarPos(vpoly,vbb,maxAltura,colocadas,telaW){
  const xSet=new Set();
  xSet.add(0);
  const maxX=telaW-vbb.w;
  if(maxX>=0)xSet.add(maxX);
  for(const col of colocadas){
    const cands=[col.tx,col.tx+col.bb.w,col.tx-vbb.w,col.tx+col.bb.w-vbb.w];
    for(const cx of cands)if(cx>=0&&cx<=maxX)xSet.add(cx);
  }
  for(let r=0;r<8;r++)xSet.add(Math.random()*Math.max(0,maxX));

  let mejorX=0,mejorY=maxAltura,mejorScore=Infinity;
  for(const sx of xSet){
    if(sx<0||sx>maxX)continue;
    let tx=sx,ty=maxAltura+STEP;
    let drop=10;
    while(ty-drop>=0&&canPlace(vpoly,tx,ty-drop,colocadas,telaW))ty-=drop;
    while(ty-STEP>=0&&canPlace(vpoly,tx,ty-STEP,colocadas,telaW))ty-=STEP;
    let moved=true;
    while(moved){
      moved=false;
      for(const dx of[-STEP,STEP]){
        if(canPlace(vpoly,tx+dx,ty,colocadas,telaW)){
          let nx=tx+dx,ny=ty;
          while(ny-STEP>=0&&canPlace(vpoly,nx,ny-STEP,colocadas,telaW))ny-=STEP;
          if(ny<ty){tx=nx;ty=ny;moved=true;}
        }
      }
    }
    const score=ty*100000+tx;
    if(score<mejorScore){mejorScore=score;mejorX=tx;mejorY=ty;}
  }
  return{tx:mejorX,ty:mejorY};
}

function calcularLayout(ordenPiezas,telaW){
  const colocadas=[];
  let maxAltura=0;
  for(const p of ordenPiezas){
    const polyRot=Geo.rotate180(p.poly);
    const vars=[{poly:p.poly,bb:p.bb,rot:false},{poly:polyRot,bb:Geo.bbox(polyRot),rot:true}];
    let mejorPos=null,mejorRot=false,mejorScore=Infinity;
    for(const v of vars){
      if(v.bb.w>telaW)continue;
      const pos=buscarPos(v.poly,v.bb,maxAltura,colocadas,telaW);
      const score=pos.ty*100000+pos.tx;
      if(score<mejorScore){mejorScore=score;mejorPos=pos;mejorRot=v.rot;}
    }
    if(!mejorPos)mejorPos={tx:0,ty:maxAltura};
    const v=mejorRot?vars[1]:vars[0];
    const polyF=Geo.translate(v.poly,mejorPos.tx+MARGEN,mejorPos.ty+MARGEN);
    const bbF=Geo.bbox(polyF);
    colocadas.push({id:p.id,poly:polyF,bbF,tx:mejorPos.tx,ty:mejorPos.ty,bb:v.bb,rotada:mejorRot});
    const h=mejorPos.ty+v.bb.h;
    if(h>maxAltura)maxAltura=h;
  }
  // Sacudida ligera: una pasada hacia arriba
  for(let i=0;i<colocadas.length;i++){
    const cur=colocadas[i];
    const others=colocadas.filter((_,idx)=>idx!==i);
    while(true){
      const tp=Geo.translate(cur.poly,0,-STEP);
      const bbT=Geo.bbox(tp);
      if(bbT.minY<0)break;
      let col=false;
      for(const o of others){
        if(bbT.maxX<=o.bbF.minX||bbT.minX>=o.bbF.maxX||bbT.maxY<=o.bbF.minY||bbT.minY>=o.bbF.maxY)continue;
        if(Geo.collide(tp,o.poly)){col=true;break;}
      }
      if(col)break;
      cur.poly=tp;cur.bbF=bbT;cur.ty-=STEP;
    }
  }
  let finalH=0;
  for(const col of colocadas){const h=col.ty+col.bb.h+MARGEN;if(h>finalH)finalH=h;}
  return{layout:colocadas.map(c=>({id:c.id,tx:c.tx,ty:c.ty,rotada:c.rotada})),altura:finalH};
}

// ========== NUEVO: Historial de mutaciones exitosas ==========
let mutHistory = [];
let lastMutType = -1;
let lastMutSuccess = false;

function mutar(orden, iter, totalIter){
  const a=[...orden];

  // Probabilidad adaptativa: si la ultima mutacion funciono, repetir tipo similar
  let useType = -1;
  if (lastMutSuccess && lastMutType >= 0 && Math.random() < 0.4) {
    useType = lastMutType;
  }

  // Si no hay tipo forzado, elegir con probabilidad adaptativa
  if (useType < 0) {
    // Probabilidades adaptativas basadas en historial
    const weights = [0.22, 0.22, 0.15, 0.18, 0.23]; // swap, move, smallFront, bigMid, shuffle
    // Aumentar peso de tipos que han funcionado recientemente
    if (mutHistory.length > 0) {
      const recent = mutHistory.slice(-20);
      const successCounts = [0,0,0,0,0];
      recent.forEach(h => { if (h.success) successCounts[h.type]++; });
      const totalSuccess = successCounts.reduce((a,b)=>a+b,0);
      if (totalSuccess > 0) {
        for (let i=0; i<5; i++) {
          weights[i] = 0.15 + (successCounts[i] / totalSuccess) * 0.4;
        }
      }
    }
    const totalW = weights.reduce((a,b)=>a+b,0);
    let r = Math.random() * totalW;
    for (let i=0; i<5; i++) {
      r -= weights[i];
      if (r <= 0) { useType = i; break; }
    }
    if (useType < 0) useType = 0;
  }

  lastMutType = useType;

  if(useType===0){
    // swap simple - mejorado: swap adyacente con mayor probabilidad si funciono antes
    let i, j;
    if (lastMutSuccess && Math.random() < 0.5) {
      i = Math.floor(Math.random() * (a.length - 1));
      j = i + 1;
    } else {
      i = Math.floor(Math.random() * a.length);
      j = Math.floor(Math.random() * a.length);
    }
    [a[i],a[j]]=[a[j],a[i]];
  }else if(useType===1){
    // mover pieza a otra posicion - mejorado: mover a posicion cercana
    const i = Math.floor(Math.random() * a.length);
    const [p] = a.splice(i, 1);
    let j;
    if (lastMutSuccess && Math.random() < 0.6) {
      // Mover cerca de donde estaba
      const offset = Math.floor(Math.random() * 6) - 3;
      j = Math.max(0, Math.min(a.length, i + offset));
    } else {
      j = Math.floor(Math.random() * a.length);
    }
    a.splice(j, 0, p);
  }else if(useType===2){
    // pieza pequeña al frente - mejorado: tambien puede mover mediana al frente
    const areaThreshold = 0.3 + Math.random() * 0.4; // 30-70% del percentil
    const sortedByArea = [...a].map((p, idx) => ({idx, area: p.bb.w * p.bb.h})).sort((x,y) => x.area - y.area);
    const cutoffIdx = Math.floor(sortedByArea.length * areaThreshold);
    const smallIndices = sortedByArea.slice(0, Math.max(1, cutoffIdx)).map(x => x.idx);
    const pickFrom = smallIndices.length > 0 ? smallIndices : [1 + Math.floor(Math.random() * (a.length - 1))];
    const i = pickFrom[Math.floor(Math.random() * pickFrom.length)];
    const [p] = a.splice(i, 1);
    a.unshift(p);
  }else if(useType===3){
    // pieza grande al medio o final - mejorado: buscar en primeros 5 lugares, no 3
    let bigIdx = 0;
    let bigArea = a[0].bb.w * a[0].bb.h;
    for (let k = 1; k < Math.min(5, a.length); k++) {
      const area = a[k].bb.w * a[k].bb.h;
      if (area > bigArea) { bigArea = area; bigIdx = k; }
    }
    const [p] = a.splice(bigIdx, 1);
    // Insertar en segunda mitad, pero con variacion
    const insertMin = Math.floor(a.length / 3);
    const insertMax = Math.floor(a.length * 0.85);
    const insertPos = insertMin + Math.floor(Math.random() * Math.max(1, insertMax - insertMin));
    a.splice(insertPos, 0, p);
  }else{
    // shuffle de segmento - mejorado: segmento mas largo si estamos atascados
    const start = Math.floor(Math.random() * a.length);
    const len = 2 + Math.floor(Math.random() * (lastMutSuccess ? 3 : 5));
    const end = Math.min(start + len, a.length);
    const seg = a.slice(start, end);
    // Shuffle parcial: algunos elementos quedan en lugar, otros se mezclan
    for (let i = seg.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [seg[i], seg[j]] = [seg[j], seg[i]];
    }
    a.splice(start, seg.length, ...seg);
  }
  return a;
}

self.onmessage=function(e){
  if(e.data.cmd!=='start')return;
  const{piezas,telaW,iteraciones,semillaOrden}=e.data;

  // ========== NUEVO: Poblacion de elite ==========
  const ELITE_SIZE = 5;
  let elite = []; // Guarda los mejores ordenes encontrados

  let mejorOrden=semillaOrden
    ?semillaOrden.map(id=>piezas.find(p=>p.id===id)).filter(Boolean)
    :[...piezas].sort((a,b)=>(b.bb.w*b.bb.h)-(a.bb.w*a.bb.h));

  let mejorResultado=calcularLayout(mejorOrden,telaW);
  elite.push({orden: [...mejorOrden], altura: mejorResultado.altura});

  let ordenActual=[...mejorOrden];
  let resultadoActual=mejorResultado;
  let sinMejora=0;
  const RESTART=Math.max(15,Math.floor(iteraciones*0.12));
  let lastUpdate=0;

  // Resetear historial
  mutHistory = [];
  lastMutType = -1;
  lastMutSuccess = false;

  self.postMessage({cmd:'update',layout:mejorResultado.layout,altura:mejorResultado.altura});

  for(let i=0;i<iteraciones;i++){
    // ========== NUEVO: Mas candidatos, y algunos desde elite ==========
    const N_MUT = 5; // Aumentado de 3 a 5
    let candidatoOrden=null,candidatoRes=null,candidatoScore=Infinity;

    for(let m=0;m<N_MUT;m++){
      let ord;
      if (m === 0 && elite.length > 1) {
        // 1 de cada 5 candidatos: cruzar dos elites
        const e1 = elite[Math.floor(Math.random() * elite.length)];
        const e2 = elite[Math.floor(Math.random() * elite.length)];
        ord = cruzarOrdenes(e1.orden, e2.orden);
      } else if (m === 1 && elite.length > 0 && Math.random() < 0.3) {
        // Otro: mutar un elite aleatorio
        const base = elite[Math.floor(Math.random() * elite.length)].orden;
        ord = mutar([...base], i, iteraciones);
      } else {
        ord = mutar(ordenActual, i, iteraciones);
      }
      const res = calcularLayout(ord, telaW);
      if(res.altura<candidatoScore){candidatoScore=res.altura;candidatoRes=res;candidatoOrden=ord;}
    }

    const diff=candidatoRes.altura-resultadoActual.altura;
    const temp=1-(i/iteraciones);

    // Aceptar si mejora, o con probabilidad de simulated annealing
    if(diff<0||Math.random()<Math.exp(-diff/(temp*2+0.05))){
      ordenActual=candidatoOrden;resultadoActual=candidatoRes;
      lastMutSuccess = diff < 0;
    } else {
      lastMutSuccess = false;
    }

    // Registrar en historial
    mutHistory.push({type: lastMutType, success: lastMutSuccess});
    if (mutHistory.length > 50) mutHistory.shift();

    // Actualizar elite
    if (resultadoActual.altura < mejorResultado.altura - 0.05) {
      mejorResultado = resultadoActual;
      mejorOrden = ordenActual;
      sinMejora = 0;
      // Agregar a elite, mantener solo los mejores
      elite.push({orden: [...mejorOrden], altura: mejorResultado.altura});
      elite.sort((a,b) => a.altura - b.altura);
      if (elite.length > ELITE_SIZE) elite = elite.slice(0, ELITE_SIZE);
    } else {
      sinMejora++;
    }

    // ========== NUEVO: Reinicio suave en lugar de brusco ==========
    if(sinMejora>=RESTART){
      sinMejora=0;

      // Opcion 1: Reiniciar desde un elite aleatorio (no siempre el mejor)
      if (elite.length > 1 && Math.random() < 0.6) {
        const pick = elite[Math.floor(Math.random() * Math.min(3, elite.length))];
        ordenActual = [...pick.orden];
        resultadoActual = calcularLayout(ordenActual, telaW);
      } else {
        // Opcion 2: Mutar fuertemente el mejor
        const base = [...mejorOrden];
        // Hacer 2-3 mutaciones seguidas para mayor variacion
        for (let mm = 0; mm < 2 + Math.floor(Math.random() * 2); mm++) {
          const mutated = mutar(base, i, iteraciones);
          // Aplicar la mutacion
          for (let k = 0; k < base.length; k++) base[k] = mutated[k];
        }
        ordenActual = base;
        resultadoActual = calcularLayout(ordenActual, telaW);
      }

      // Si el reinicio dio algo bueno, actualizar
      if (resultadoActual.altura < mejorResultado.altura - 0.05) {
        mejorResultado = resultadoActual;
        mejorOrden = ordenActual;
        elite.push({orden: [...mejorOrden], altura: mejorResultado.altura});
        elite.sort((a,b) => a.altura - b.altura);
        if (elite.length > ELITE_SIZE) elite = elite.slice(0, ELITE_SIZE);
      }
    }

    const now=Date.now();
    if(now-lastUpdate>600){
      self.postMessage({cmd:'update',layout:resultadoActual.layout,altura:resultadoActual.altura});
      lastUpdate=now;
    }
    if(i%10===0){
      self.postMessage({cmd:'progress',msg:'Iter '+(i+1)+'/'+iteraciones+' | Mejor: '+mejorResultado.altura.toFixed(2)+'cm | Elite:'+elite.length});
    }
  }

  self.postMessage({
    cmd:'done',
    layout:mejorResultado.layout,
    altura:mejorResultado.altura,
    mejorOrden:mejorOrden.map(p=>p.id)
  });
};

// ========== NUEVO: Funcion de cruce entre dos ordenes ==========
function cruzarOrdenes(o1, o2) {
  const n = o1.length;
  const result = new Array(n).fill(null);
  const used = new Set();

  // Copiar elementos que coinciden en posicion
  for (let i = 0; i < n; i++) {
    if (o1[i].id === o2[i].id && !used.has(o1[i].id)) {
      result[i] = o1[i];
      used.add(o1[i].id);
    }
  }

  // Llenar huecos alternando entre o1 y o2
  let useO1 = Math.random() < 0.5;
  for (let i = 0; i < n; i++) {
    if (result[i] !== null) continue;
    let found = false;
    // Buscar en la fuente elegida
    const source = useO1 ? o1 : o2;
    for (let j = 0; j < n; j++) {
      if (!used.has(source[j].id)) {
        result[i] = source[j];
        used.add(source[j].id);
        found = true;
        break;
      }
    }
    // Si no encontro, buscar en la otra
    if (!found) {
      const other = useO1 ? o2 : o1;
      for (let j = 0; j < n; j++) {
        if (!used.has(other[j].id)) {
          result[i] = other[j];
          used.add(other[j].id);
          break;
        }
      }
    }
    useO1 = !useO1;
  }

  return result;
}
`;

function initWorker(){
  if(worker)worker.terminate();
  const blob=new Blob([WORKER_SRC],{type:'application/javascript'});
  worker=new Worker(URL.createObjectURL(blob));
  worker.onerror=function(e){
    document.getElementById('status').textContent='Error worker: '+e.message;
    resetUI();
  };
  worker.onmessage=function(e){
    const d=e.data;
    if(d.cmd==='progress'){
      document.getElementById('status').textContent=d.msg;
    }else if(d.cmd==='update'){
      document.getElementById('status').textContent='Explorando... '+d.altura.toFixed(2)+' cm';
      dibujarLayout(d.layout);
    }else if(d.cmd==='done'){
      if(d.altura<mejorAlturaGlobal){
        mejorAlturaGlobal=d.altura;
        mejorOrdenGlobal=d.mejorOrden;
      }
      actualizarMemStatus();
      dibujarLayout(d.layout);
      procesarResultado(d.layout,d.altura);
      document.getElementById('status').textContent='Listo. Altura: '+d.altura.toFixed(2)+' cm';
      resetUI();
    }
  };
}

function resetUI(){
  const btn=document.getElementById('btnNesting');
  btn.disabled=false;btn.textContent='Ejecutar Nesting';
  document.getElementById('btnStop').style.display='none';
}

const canvas=document.getElementById('c');
const ctx=canvas.getContext('2d');

function dibujarLayout(layout){
  const PX=4;
  let maxY=0;
  layout.forEach(item=>{
    const p=piezas.find(pz=>pz.id===item.id);
    if(p){const h=item.ty+p.bb.h;if(h>maxY)maxY=h;}
  });
  canvas.width=TELA_W*PX;canvas.height=(maxY+5)*PX;
  ctx.clearRect(0,0,canvas.width,canvas.height);
  layout.forEach(item=>{
    const pieza=piezas.find(p=>p.id===item.id);if(!pieza)return;
    let poly=pieza.poly;
    if(item.rotada)poly=GeoMain.rotate180(poly);
    poly=GeoMain.translate(poly,item.tx,item.ty);
    ctx.beginPath();ctx.moveTo(poly[0].x*PX,poly[0].y*PX);
    for(let i=1;i<poly.length;i++)ctx.lineTo(poly[i].x*PX,poly[i].y*PX);
    ctx.closePath();
    ctx.fillStyle=item.rotada?'#1a3a5c':'#3a3a3a';ctx.fill();
    ctx.strokeStyle='#888';ctx.lineWidth=1;ctx.stroke();
  });
}

function dibujarVistaPrevia(){
  const PX=4;
  canvas.width=TELA_W*PX;canvas.height=500;
  ctx.clearRect(0,0,canvas.width,canvas.height);
  let cx=0,cy=0,rowH=0;
  piezas.forEach(p=>{
    if(cx+p.bb.w>TELA_W){cx=0;cy+=rowH+2;rowH=0;}
    const poly=GeoMain.translate(p.poly,cx,cy);
    ctx.beginPath();ctx.moveTo(poly[0].x*PX,poly[0].y*PX);
    for(let i=1;i<poly.length;i++)ctx.lineTo(poly[i].x*PX,poly[i].y*PX);
    ctx.closePath();ctx.fillStyle='#3a3a3a';ctx.fill();ctx.strokeStyle='#888';ctx.lineWidth=1;ctx.stroke();
    cx+=p.bb.w+2;if(p.bb.h>rowH)rowH=p.bb.h;
  });
  document.getElementById('resultPanel').style.display='none';
}

function cargarJSON(texto){
  const data=JSON.parse(texto);
  const pxPerCm=data.pxPerCm||37.79527559055118;
  const figs=data.figures.filter(f=>f.closed!==false&&f.vertices&&f.vertices.length>=3);
  // Punto 1: preguntar si mantener memoria al cambiar JSON
  if(mejorOrdenGlobal){
    const mantener=confirm('Hay memoria guardada ('+mejorAlturaGlobal.toFixed(2)+' cm).\n¿Mantenerla para el nuevo JSON?');
    if(!mantener){mejorOrdenGlobal=null;mejorAlturaGlobal=Infinity;}
  }
  piezas=figs.map((fig,idx)=>{
    const verts=fig.vertices.map(v=>({
      x:v.xCm!==undefined?v.xCm:v.x/pxPerCm,
      y:v.yCm!==undefined?v.yCm:v.y/pxPerCm
    }));
    const n=verts.length,edgeMap={};
    fig.edges.forEach(e=>{edgeMap[e.start+'_'+e.end]=e;});
    const segs=[];
    for(let i=0;i<n;i++){
      const j=(i+1)%n,e=edgeMap[i+'_'+j];
      if(!e)segs.push({type:'line',pts:[{...verts[i]},{...verts[j]}]});
      else if(!e.curved)segs.push({type:'line',pts:[{...verts[e.start]},{...verts[e.end]}]});
      else segs.push({type:'quad',pts:[{...verts[e.start]},{x:e.controlX/pxPerCm,y:e.controlY/pxPerCm},{...verts[e.end]}]});
    }
    const poly=construirPoligono(segs);
    const minX=Math.min(...poly.map(p=>p.x));
    const minY=Math.min(...poly.map(p=>p.y));
    const polyN=poly.map(p=>({x:p.x-minX,y:p.y-minY}));
    return{id:idx,poly:polyN,bb:GeoMain.bbox(polyN),area:polygonArea(polyN)};
  });
  mejorOrdenGlobal=null;mejorAlturaGlobal=Infinity;
  actualizarMemStatus();
  document.getElementById('btnReset').style.display='none';
  document.getElementById('status').textContent=piezas.length+' piezas cargadas.';
  document.getElementById('btnNesting').disabled=false;
  TELA_W=parseFloat(document.getElementById('inputAncho').value)||160;
  dibujarVistaPrevia();
}

function procesarResultado(layout,maxY){
  let areaTotal=0;
  layout.forEach(item=>{const p=piezas.find(pz=>pz.id===item.id);if(p)areaTotal+=p.area;});
  const aprov=TELA_W*maxY>0?(areaTotal/(TELA_W*maxY))*100:0;
  document.getElementById('resultStats').innerHTML=
    'Aprovechamiento: <b style="color:#28a745">'+aprov.toFixed(2)+'%</b> | Altura: '+maxY.toFixed(2)+' cm';
  document.getElementById('resultTxt').value=JSON.stringify(layout,null,2);
  document.getElementById('resultPanel').style.display='block';
  document.getElementById('btnReset').style.display='';
}

document.getElementById('btnTogglePanel').addEventListener('click',()=>{
  const p=document.getElementById('jsonPanel');
  p.style.display=p.style.display==='none'?'block':'none';
});
document.getElementById('btnCargarTxt').addEventListener('click',()=>{
  const txt=document.getElementById('jsonTxt').value.trim();
  if(!txt){alert('Pega el JSON primero.');return;}
  try{cargarJSON(txt);}catch(e){alert('Error: '+e.message);}
});
document.getElementById('fileInput').addEventListener('change',e=>{
  const f=e.target.files[0];if(!f)return;
  const r=new FileReader();r.onload=ev=>cargarJSON(ev.target.result);r.readAsText(f);
});
document.getElementById('btnNesting').addEventListener('click',()=>{
  if(!piezas.length)return;
  TELA_W=parseFloat(document.getElementById('inputAncho').value)||160;
  const iter=parseInt(document.getElementById('inputIter').value)||300;
  const btn=document.getElementById('btnNesting');
  btn.disabled=true;btn.textContent='Ejecutando...';
  document.getElementById('btnStop').style.display='';
  initWorker();
  worker.postMessage({cmd:'start',piezas,telaW:TELA_W,iteraciones:iter,semillaOrden:mejorOrdenGlobal});
});
document.getElementById('btnStop').addEventListener('click',()=>{
  if(worker)worker.terminate();
  resetUI();
  document.getElementById('status').textContent='Detenido por el usuario.';
});
document.getElementById('btnReset').addEventListener('click',()=>{
  mejorOrdenGlobal=null;mejorAlturaGlobal=Infinity;
  actualizarMemStatus();
  document.getElementById('btnReset').style.display='none';
  document.getElementById('status').textContent='Memoria borrada.';
});
document.getElementById('btnCopiar').addEventListener('click',()=>{
  const txt=document.getElementById('resultTxt');
  txt.select();txt.setSelectionRange(0,99999);
  navigator.clipboard.writeText(txt.value).then(()=>alert('Copiado!')).catch(err=>alert('Error: '+err));
});
document.getElementById('btnDescargar').addEventListener('click',()=>{
  const txt=document.getElementById('resultTxt').value;
  const b=new Blob([txt],{type:'application/json'});
  const url=URL.createObjectURL(b);
  const a=document.createElement('a');a.href=url;a.download='nesting_resultado.json';
  document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(url);
});
document.getElementById('btnGuardarOrden').addEventListener('click',()=>{
  if(!mejorOrdenGlobal){alert('No hay orden guardado aun.');return;}
  const data=JSON.stringify({orden:mejorOrdenGlobal,altura:mejorAlturaGlobal});
  document.getElementById('ordenTxt').value=data;
  document.getElementById('ordenPanel').style.display='block';
});
document.getElementById('btnCargarOrden').addEventListener('click',()=>{
  const txt=document.getElementById('ordenTxt').value.trim();
  if(!txt){alert('Pega el orden primero.');return;}
  try{
    const data=JSON.parse(txt);
    mejorOrdenGlobal=data.orden;
    mejorAlturaGlobal=data.altura||Infinity;
    actualizarMemStatus();
    document.getElementById('btnReset').style.display='';
    alert('Orden cargado. Altura referencia: '+mejorAlturaGlobal.toFixed(2)+' cm');
  }catch(e){alert('Error al parsear: '+e.message);}
});
document.getElementById('btnCerrarOrden').addEventListener('click',()=>{
  document.getElementById('ordenPanel').style.display='none';
});
document.getElementById('btnCargarOrdenCtrl').addEventListener('click',()=>{
  const p=document.getElementById('ordenPanelCtrl');
  p.style.display=p.style.display==='none'?'block':'none';
});
document.getElementById('btnCargarOrdenCtrl2').addEventListener('click',()=>{
  const txt=document.getElementById('ordenTxtCtrl').value.trim();
  if(!txt){alert('Pega el orden primero.');return;}
  try{
    const data=JSON.parse(txt);
    mejorOrdenGlobal=data.orden;
    mejorAlturaGlobal=data.altura||Infinity;
    actualizarMemStatus();
    document.getElementById('btnReset').style.display='';
    document.getElementById('ordenPanelCtrl').style.display='none';
    alert('Orden cargado. Altura referencia: '+mejorAlturaGlobal.toFixed(2)+' cm');
  }catch(e){alert('Error: '+e.message);}
});
document.getElementById('btnCerrarOrdenCtrl').addEventListener('click',()=>{
  document.getElementById('ordenPanelCtrl').style.display='none';
});
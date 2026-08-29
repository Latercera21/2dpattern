#pragma once
// Modulo de geometria para el motor tipo Sparrow (Gardeyn et al., 2025).
// Depende UNICAMENTE de las primitivas ya existentes en nesting_core.hpp
// (Point, BBox, Geo::distPuntoSegmento, Geo::pointInPoly, Geo::bbox,
// Geo::signedArea). No toca Clipper2 en absoluto: los "polos" y la
// penalizacion por forma son geometria pura sobre std::vector<Point>.
//
// Incluir DESPUES de nesting_core.hpp en el proyecto real.

#include <queue>
#include <cmath>
#include <vector>
#include <limits>
#include <chrono>
#include <algorithm>
#include <random>
#include <utility>

namespace Sparrow {

// Un "polo" (pole of inaccessibility): el circulo mas grande que cabe
// dentro de la forma en cierta region. Ver Alg. 2/3 del paper.
struct Pole {
    Point center;
    double radius;
};

// Distancia con signo de un punto al borde del poligono.
// Positiva si el punto esta DENTRO, negativa si esta FUERA.
inline double distanciaAlBorde(const Point& p, const std::vector<Point>& poly) {
    double d = std::numeric_limits<double>::max();
    int n = (int)poly.size();
    for (int i = 0; i < n; i++) {
        double dSeg = Geo::distPuntoSegmento(p, poly[i], poly[(i + 1) % n]);
        if (dSeg < d) d = dSeg;
    }
    return Geo::pointInPoly(p, poly) ? d : -d;
}

namespace detail {

// Celda de la grilla usada por el algoritmo tipo polylabel (Mapbox).
// "d" es la distancia efectiva al borde en el centro; "maxD" es la cota
// superior de esa distancia en cualquier punto dentro de la celda
// (el centro esta a lo sumo h*sqrt(2) de cualquier esquina).
struct Celda {
    double x, y, h;
    double d;
    double maxD;
    bool operator<(const Celda& o) const { return maxD < o.maxD; } // max-heap por maxD
};

} // namespace detail

// Encuentra el polo de inaccesibilidad de un poligono. Si se pasa "evitar",
// la distancia efectiva de cada punto tambien resta la cercania a polos
// ya encontrados -- asi el siguiente polo tiende a cubrir una zona distinta
// de la forma en vez de repetir la misma region. Esta es la base para
// generar el conjunto P(S) de varios polos que pide el paper.
inline Pole poloDeInaccesibilidad(const std::vector<Point>& poly, const BBox& bb,
                                   const std::vector<Pole>& evitar = {},
                                   double precisionRelativa = 0.005) {
    double precision = std::max(0.01, precisionRelativa * std::min(bb.w, bb.h));
    auto distanciaEfectiva = [&](double x, double y) -> double {
        Point p{x, y};
        double d = distanciaAlBorde(p, poly);
        for (const auto& pol : evitar) {
            double dx = x - pol.center.x, dy = y - pol.center.y;
            double distAlCentro = std::sqrt(dx * dx + dy * dy);
            double dEvitando = distAlCentro - pol.radius;
            if (dEvitando < d) d = dEvitando;
        }
        return d;
    };

    double w = bb.w, h = bb.h;
    double cellSize = std::min(w, h);
    if (cellSize < 1e-9) return { {bb.minX, bb.minY}, 0.0 };
    double half = cellSize / 2.0;

    auto hacerCelda = [&](double x, double y, double half) -> detail::Celda {
        detail::Celda c;
        c.x = x; c.y = y; c.h = half;
        c.d = distanciaEfectiva(x, y);
        c.maxD = c.d + half * std::sqrt(2.0);
        return c;
    };

    std::priority_queue<detail::Celda> cola;
    for (double x = bb.minX; x < bb.maxX; x += cellSize)
        for (double y = bb.minY; y < bb.maxY; y += cellSize)
            cola.push(hacerCelda(x + half, y + half, half));

    // Candidato inicial: centro del bbox (evita devolver algo peor que esto).
    detail::Celda mejor = hacerCelda(bb.minX + w / 2.0, bb.minY + h / 2.0, 0.0);

    // Tambien probamos el centroide real del poligono como semilla adicional.
    {
        double cx = 0, cy = 0;
        for (auto& p : poly) { cx += p.x; cy += p.y; }
        cx /= (double)poly.size(); cy /= (double)poly.size();
        detail::Celda semilla = hacerCelda(cx, cy, 0.0);
        if (semilla.d > mejor.d) mejor = semilla;
    }

    int iteraciones = 0;
    const int LIMITE_ITER = 20000; // salvaguarda ante formas patologicas
    while (!cola.empty() && iteraciones++ < LIMITE_ITER) {
        detail::Celda c = cola.top(); cola.pop();
        if (c.d > mejor.d) mejor = c;
        if (c.maxD - mejor.d <= precision) continue; // ya no puede mejorar lo suficiente
        double h2 = c.h / 2.0;
        if (h2 < precision) continue; // demasiado pequena para valer la pena subdividir
        cola.push(hacerCelda(c.x - h2, c.y - h2, h2));
        cola.push(hacerCelda(c.x + h2, c.y - h2, h2));
        cola.push(hacerCelda(c.x - h2, c.y + h2, h2));
        cola.push(hacerCelda(c.x + h2, c.y + h2, h2));
    }

    return { {mejor.x, mejor.y}, mejor.d };
}

// Genera un conjunto de hasta "maxPolos" que cubren la forma completa,
// de mayor a menor. Para (Sa) el paper recomienda 8-16 polos; se detiene
// antes si el siguiente polo ya es demasiado chico para aportar (esquinas
// finas, detalles menores) segun "radioMinimoRelativo".
inline std::vector<Pole> generarPolos(const std::vector<Point>& poly, int maxPolos = 8,
                                       double radioMinimoRelativo = 0.15) {
    std::vector<Pole> polos;
    if (poly.size() < 3) return polos;
    BBox bb = Geo::bbox(poly);

    Pole primero = poloDeInaccesibilidad(poly, bb, {});
    if (primero.radius <= 0) return polos; // forma degenerada / autointersectada
    polos.push_back(primero);
    double radioMinimo = primero.radius * radioMinimoRelativo;

    for (int i = 1; i < maxPolos; i++) {
        Pole siguiente = poloDeInaccesibilidad(poly, bb, polos);
        if (siguiente.radius < radioMinimo) break;
        polos.push_back(siguiente);
    }
    return polos;
}

// Penalizacion por forma (lambda_a en el paper, Eq. 6): raiz del area del
// convex hull. Reutiliza directamente Piece.hull, que tu proyecto ya
// calcula una vez al cargar cada pieza -- no hace falta recalcular nada.
inline double penalizacionForma(const std::vector<Point>& hull) {
    return std::sqrt(std::abs(Geo::signedArea(hull)));
}

// Penalizacion combinada para un par de piezas (Eq. 7): media geometrica.
inline double penalizacionParCombinada(double lambdaA, double lambdaB) {
    return std::sqrt(lambdaA * lambdaB);
}

// ===================== Fase 2: cuantificar colisiones =====================
// Alg. 1 (evaluate_item_pair), Alg. 3 (overlap_proxy_decay), Alg. 4
// (quantify_collision) del paper. Todo esto se corre en CADA muestra
// evaluada durante la busqueda, asi que solo usa polos + circulos: nunca
// vuelve a calcular interseccion exacta de poligonos salvo para el chequeo
// binario inicial (que reutiliza Geo::collide, ya existente).

// Profundidad de penetracion entre dos polos (circulos): suma de radios
// menos distancia entre centros. Positiva si se solapan.
inline double profundidadPenetracion(const Pole& a, const Pole& b) {
    double dx = a.center.x - b.center.x, dy = a.center.y - b.center.y;
    double dist = std::sqrt(dx * dx + dy * dy);
    return (a.radius + b.radius) - dist;
}

// Diametro de una forma: distancia maxima entre dos puntos de su convex
// hull (alcanza con el hull, no hace falta recorrer todo el poligono).
inline double diametroForma(const std::vector<Point>& hull) {
    double maxD = 0.0;
    int n = (int)hull.size();
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            double dx = hull[i].x - hull[j].x, dy = hull[i].y - hull[j].y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d > maxD) maxD = d;
        }
    return maxD;
}

// Alg. 3: overlap_proxy_decay(Sa, Sb). Recibe polos y diametros ya
// calculados (se calculan una vez por pieza/rotacion, no en cada llamada
// -- esto es lo que hace que sea rapido evaluar miles de muestras).
inline double overlapProxyDecay(const std::vector<Pole>& polosA, const std::vector<Pole>& polosB,
                                 double diametroA, double diametroB, double Repsilon = 0.1) {
    double epsilon = Repsilon * std::max(diametroA, diametroB);
    if (epsilon < 1e-9) epsilon = 1e-9;
    double alpha = 0.0;
    for (const auto& pa : polosA) {
        for (const auto& pb : polosB) {
            double delta = profundidadPenetracion(pa, pb);
            double deltaPrima = (delta >= epsilon) ? delta : (epsilon * epsilon) / (-delta + 2 * epsilon);
            double diamMin = std::min(pa.radius, pb.radius) * 2.0;
            alpha += deltaPrima * diamMin;
        }
    }
    return alpha;
}

// Alg. 4: quantify_collision(Sa, Sb).
inline double cuantificarColision(const std::vector<Pole>& polosA, const std::vector<Pole>& polosB,
                                   double diametroA, double diametroB,
                                   double lambdaA, double lambdaB) {
    double alpha = overlapProxyDecay(polosA, polosB, diametroA, diametroB);
    double lambdaAB = penalizacionParCombinada(lambdaA, lambdaB);
    return std::sqrt(std::max(0.0, alpha)) * lambdaAB;
}

// Datos precalculados de una pieza en su posicion/rotacion actual --
// se recalculan al mover la pieza, se reutilizan mientras no se mueve.
struct DatosPieza {
    std::vector<Point> poly;       // contorno real de la pieza (sin margen)
    std::vector<Point> polyMargen; // poly inflado por margen/2 -- SOLO para el chequeo de colision
                                    // exacta (evaluarParPiezas). Ver "Correccion de margen" mas abajo.
    std::vector<Pole> polos;
    std::vector<Point> hull;
    double diametro;
    double lambda;
};

// CORRECCION DE MARGEN (encontrada al comparar contra el motor NFP real): hasta esta correccion,
// evaluarParPiezas() chequeaba colision contra el contorno EXACTO de cada pieza, sin ningun
// margen de separacion -- a diferencia de NestingEngine (que infla la pieza por el margen de
// corte, 0.05cm en este proyecto, antes de calcular el NFP; ver NFPGeom::CalculadorNFP::margen
// en nesting_core.hpp) y a diferencia de cualquier nesting real de produccion (que necesita un
// hueco minimo de corte entre piezas). Esto hacia que las comparaciones de altura entre Sparrow
// y el motor NFP (o contra un nesting profesional real) no fueran comparables: Sparrow podia
// empaquetar piezas literalmente tocandose, ganando una ventaja artificial. Se agrega
// `polyMargen` (el mismo contorno inflado por margen/2, igual que hace joint_sa.hpp con su
// propio `inflar()`) y evaluarParPiezas() ahora chequea colision contra polyMargen en vez de
// poly. Los polos (usados solo para cuantificar que tan severa es una colision, no para decidir
// si hay colision) se mantienen sobre el contorno original -- es un proxy, no necesita ser exacto.
inline std::vector<Point> inflarPoly(const std::vector<Point>& poly, double m) {
    if (m <= 0 || poly.size() < 3) return poly;
    Clipper2Lib::PathD p;
    p.reserve(poly.size());
    for (auto& pt : poly) p.push_back(Clipper2Lib::PointD(pt.x, pt.y));
    auto res = Clipper2Lib::InflatePaths({ p }, m, Clipper2Lib::JoinType::Miter,
                                          Clipper2Lib::EndType::Polygon, 4.0, 4);
    if (res.empty()) return poly;
    std::vector<Point> out;
    out.reserve(res[0].size());
    for (auto& pt : res[0]) out.push_back({ pt.x, pt.y });
    return out;
}

// Margen de separacion GLOBAL para este modulo (mismo valor que nesting_core.hpp/joint_sa.hpp
// en este proyecto: 0.05cm). Si el proyecto real cambia su margen, cambiar aqui tambien --
// idealmente este valor se recibe como parametro en vez de ser una constante global, pendiente
// de limpieza para la integracion final (ver seccion de pendientes en el informe).
inline double MARGEN_SEPARACION = 0.05;

// Alg. 1: evaluate_item_pair(a, b). Primero el chequeo binario EXACTO
// (reutilizando Geo::collide, ya existente en el proyecto) -- si no hay
// colision real, 0 sin gastar nada mas. Si la hay, se cuantifica con polos.
inline double evaluarParPiezas(const DatosPieza& a, const DatosPieza& b) {
    if (!Geo::collide(a.polyMargen, b.polyMargen)) return 0.0;
    return cuantificarColision(a.polos, b.polos, a.diametro, b.diametro, a.lambda, b.lambda);
}

// ===================== Fase 3: Guided Local Search =====================
// Alg. 5 (move_items), 6/7 (search_position/evaluate_sample), 8
// (update_weights) y una version simplificada del loop de Alg. 9
// (separate). Version de traslacion pura (sin rotacion todavia) para
// mantener el primer test verificable; la rotacion se agrega en la
// integracion final con tu Piece/Placed reales.

// Matriz de pesos dinamicos por par de piezas, simetrica, indexada
// por (i,j) con i<j.
struct MatrizPesos {
    int n;
    std::vector<double> w;
    explicit MatrizPesos(int n_) : n(n_), w((size_t)n_ * (n_ > 0 ? n_ - 1 : 0) / 2, 1.0) {}
    int idx(int i, int j) const {
        if (i > j) std::swap(i, j);
        return i * n - (i * (i + 1)) / 2 + (j - i - 1);
    }
    double& operator()(int i, int j) { return w[idx(i, j)]; }
    double operator()(int i, int j) const { return w[idx(i, j)]; }
};

inline void trasladarEnSitio(DatosPieza& d, double dx, double dy) {
    for (auto& p : d.poly) { p.x += dx; p.y += dy; }
    for (auto& p : d.polyMargen) { p.x += dx; p.y += dy; }
    for (auto& p : d.hull) { p.x += dx; p.y += dy; }
    for (auto& p : d.polos) { p.center.x += dx; p.center.y += dy; }
}

inline DatosPieza trasladada(const DatosPieza& d, double dx, double dy) {
    DatosPieza r = d;
    trasladarEnSitio(r, dx, dy);
    return r;
}

// Alg. 8: update_weights. Md decae el peso de pares sin colision; Ml/Mu
// escalan el aumento de peso segun que tan severa es la colision en
// relacion a la peor colision presente en la solucion actual.
inline void actualizarPesos(MatrizPesos& pesos, const std::vector<DatosPieza>& piezas,
                             double Ml = 1.0, double Mu = 1.3, double Md = 0.97) {
    int n = (int)piezas.size();
    double eMax = 0.0;
    std::vector<double> evals((size_t)n * (n - 1) / 2, 0.0);
    int k = 0;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            double e = evaluarParPiezas(piezas[a], piezas[b]);
            evals[k++] = e;
            if (e > eMax) eMax = e;
        }
    k = 0;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            double e = evals[k++];
            double m = (eMax > 0 && e > 0) ? (Ml + (Mu - Ml) * (e / eMax)) : Md;
            double& wab = pesos(a, b);
            wab = std::max(1.0, wab * m);
        }
}

// Alg. 7: evaluate_sample. Suma ponderada de colisiones de la pieza i
// (ya trasladada a la posicion candidata) contra todas las demas.
inline double evaluarMuestra(int i, const DatosPieza& candidata,
                              const std::vector<DatosPieza>& piezas,
                              const MatrizPesos& pesos) {
    double e = 0.0;
    for (int c = 0; c < (int)piezas.size(); c++) {
        if (c == i) continue;
        double col = evaluarParPiezas(candidata, piezas[c]);
        if (col > 0) e += pesos(i, c) * col;
    }
    return e;
}

// Alg. 6: search_position, con muestreo T_div (al azar en todo el
// contenedor) + T_foc (al azar cerca de la posicion actual) y
// refinamiento por descenso de coordenadas sobre los mejores candidatos.
inline DatosPieza buscarPosicion(int i, const DatosPieza& actual,
                                  const std::vector<DatosPieza>& piezas,
                                  const MatrizPesos& pesos,
                                  double contW, double contH,
                                  std::mt19937& rng,
                                  int nDiv = 20, int nFoc = 20, int nRefinar = 3) {
    std::uniform_real_distribution<double> ux(0.0, contW), uy(0.0, contH);
    BBox bbActual = Geo::bbox(actual.poly);
    double cx = (bbActual.minX + bbActual.maxX) / 2.0, cy = (bbActual.minY + bbActual.maxY) / 2.0;
    double radioFoco = std::max(bbActual.w, bbActual.h) * 1.5;
    if (radioFoco < 1e-6) radioFoco = 1.0;
    std::normal_distribution<double> nx(0.0, radioFoco), ny(0.0, radioFoco);

    // La pieza debe quedar entera dentro del contenedor [0,contW]x[0,contH]
    // (Eq. 1 del paper) -- se garantiza fijando el rango donde puede caer
    // su centro, usando su propio bbox como margen.
    double halfW = bbActual.w / 2.0, halfH = bbActual.h / 2.0;
    double loX = halfW, hiX = std::max(halfW, contW - halfW);
    double loY = halfH, hiY = std::max(halfH, contH - halfH);
    auto clamp = [](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };

    std::vector<std::pair<double, DatosPieza>> candidatos;
    auto probar = [&](double tx, double ty) {
        tx = clamp(tx, loX, hiX);
        ty = clamp(ty, loY, hiY);
        DatosPieza cand = trasladada(actual, tx - cx, ty - cy);
        double e = evaluarMuestra(i, cand, piezas, pesos);
        candidatos.push_back({e, std::move(cand)});
    };
    for (int k = 0; k < nDiv; k++) probar(ux(rng), uy(rng));
    for (int k = 0; k < nFoc; k++) probar(cx + nx(rng), cy + ny(rng));

    std::sort(candidatos.begin(), candidatos.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    DatosPieza mejor = candidatos.empty() ? actual : candidatos[0].second;
    double mejorEval = candidatos.empty() ? evaluarMuestra(i, actual, piezas, pesos) : candidatos[0].first;

    int limite = std::min(nRefinar, (int)candidatos.size());
    for (int k = 0; k < limite; k++) {
        DatosPieza c = candidatos[k].second;
        double eActual = candidatos[k].first;
        double paso = radioFoco * 0.3;
        for (int iter = 0; iter < 10 && paso > 0.05; iter++) {
            bool mejoro = false;
            static const std::pair<double,double> dirs[4] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& dir : dirs) {
                DatosPieza probando = trasladada(c, dir.first * paso, dir.second * paso);
                BBox bbP = Geo::bbox(probando.poly);
                if (bbP.minX < 0 || bbP.minY < 0 || bbP.maxX > contW || bbP.maxY > contH) continue;
                double e = evaluarMuestra(i, probando, piezas, pesos);
                if (e < eActual) { c = std::move(probando); eActual = e; mejoro = true; }
            }
            if (!mejoro) paso *= 0.5;
        }
        if (eActual < mejorEval) { mejorEval = eActual; mejor = std::move(c); }
    }
    return mejor;
}

inline bool piezaColisiona(int i, const std::vector<DatosPieza>& piezas) {
    for (int c = 0; c < (int)piezas.size(); c++)
        if (c != i && evaluarParPiezas(piezas[i], piezas[c]) > 0) return true;
    return false;
}

// Alg. 5: move_items. Reposiciona, en orden aleatorio, solo las piezas
// que estan en colision.
inline void moverPiezas(std::vector<DatosPieza>& piezas, const MatrizPesos& pesos,
                         double contW, double contH, std::mt19937& rng) {
    std::vector<int> idx;
    for (int i = 0; i < (int)piezas.size(); i++)
        if (piezaColisiona(i, piezas)) idx.push_back(i);
    std::shuffle(idx.begin(), idx.end(), rng);
    for (int i : idx)
        piezas[i] = buscarPosicion(i, piezas[i], piezas, pesos, contW, contH, rng);
}

inline double perdidaTotal(const std::vector<DatosPieza>& piezas) {
    double total = 0.0;
    for (int a = 0; a < (int)piezas.size(); a++)
        for (int b = a + 1; b < (int)piezas.size(); b++)
            total += evaluarParPiezas(piezas[a], piezas[b]);
    return total;
}

// Version simplificada de Alg. 9 (separate): repite move_items +
// update_weights, se queda con el mejor estado visto, y si se estanca
// demasiado retrocede a ese mejor estado para reintentar desde ahi.
// (La version completa del paper con k_max/n_max y reinicios completos
// se ajusta en la integracion final; esto ya prueba que el mecanismo
// central -- resolver colisiones sin pasar por NFP -- funciona.)
inline bool separar(std::vector<DatosPieza>& piezas, double contW, double contH,
                     std::mt19937& rng, int maxIterSinMejora = 150, int maxIteraciones = 3000) {
    MatrizPesos pesos((int)piezas.size());
    double mejorPerdida = perdidaTotal(piezas);
    auto mejorEstado = piezas;
    int sinMejora = 0;
    int iter = 0;
    for (; iter < maxIteraciones && mejorPerdida > 1e-6; iter++) {
        moverPiezas(piezas, pesos, contW, contH, rng);
        actualizarPesos(pesos, piezas);
        double p = perdidaTotal(piezas);
        if (p < mejorPerdida - 1e-9) {
            mejorPerdida = p; mejorEstado = piezas; sinMejora = 0;
        } else {
            sinMejora++;
            if (sinMejora > maxIterSinMejora) { piezas = mejorEstado; sinMejora = 0; }
        }
    }
    piezas = mejorEstado;
    return mejorPerdida <= 1e-6;
}

// ===================== Adaptador con tus estructuras reales =====================
// Convierte una pieza YA colocada (tu Placed) a DatosPieza. Reutiliza el
// hull que tu proyecto ya calcula -- solo genera los polos, que es lo
// unico que no existia antes.
inline DatosPieza piezaDesdePlaced(const Placed& p) {
    DatosPieza d;
    d.poly = p.poly;
    d.polyMargen = inflarPoly(p.poly, MARGEN_SEPARACION / 2.0);
    d.hull = p.hull;
    d.polos = generarPolos(p.poly, 8);
    d.diametro = diametroForma(d.hull);
    d.lambda = penalizacionForma(d.hull);
    return d;
}

// ===================== Rotacion discreta (0 / 180, como tu joint_sa.hpp) =====================
// Junto a cada pieza se precalculan sus DOS variantes locales (sin
// trasladar); buscar_posicion elige, en cada muestra, cual de las dos
// conviene mas. La refinacion por descenso de coordenadas se hace sobre
// la rotacion ya elegida (no vuelve a decidir rotacion en cada paso de
// refinamiento -- simplificacion consciente, igual que hace tu SA actual
// al fijar polyInf0/polyInf180 una vez por pieza).

inline DatosPieza construirVariante(const std::vector<Point>& polyLocal) {
    DatosPieza d;
    d.poly = polyLocal;
    d.polyMargen = inflarPoly(polyLocal, MARGEN_SEPARACION / 2.0);
    d.hull = Geo::convexHull(polyLocal);
    d.polos = generarPolos(polyLocal, 8);
    d.diametro = diametroForma(d.hull);
    d.lambda = penalizacionForma(d.hull);
    return d;
}

struct PiezaRotable {
    DatosPieza rot0;
    DatosPieza rot180;
};

inline PiezaRotable prepararRotable(const std::vector<Point>& polyOriginal) {
    PiezaRotable pr;
    pr.rot0 = construirVariante(polyOriginal);
    BBox bb = Geo::bbox(polyOriginal);
    pr.rot180 = construirVariante(Geo::rotate180ConBBox(polyOriginal, bb));
    return pr;
}

inline std::pair<double, DatosPieza> mejorEnPosicion(double tx, double ty,
                                                      const PiezaRotable& variantes, int i,
                                                      const std::vector<DatosPieza>& piezas,
                                                      const MatrizPesos& pesos) {
    auto intentar = [&](const DatosPieza& base) -> std::pair<double, DatosPieza> {
        BBox bb = Geo::bbox(base.poly);
        double cx0 = (bb.minX + bb.maxX) / 2.0, cy0 = (bb.minY + bb.maxY) / 2.0;
        DatosPieza cand = trasladada(base, tx - cx0, ty - cy0);
        double e = evaluarMuestra(i, cand, piezas, pesos);
        return {e, std::move(cand)};
    };
    auto r0 = intentar(variantes.rot0);
    auto r180 = intentar(variantes.rot180);
    return (r0.first <= r180.first) ? r0 : r180;
}

inline DatosPieza buscarPosicionConRotacion(int i, const PiezaRotable& variantes,
                                             const std::vector<DatosPieza>& piezas,
                                             const MatrizPesos& pesos,
                                             double contW, double contH,
                                             std::mt19937& rng,
                                             int nDiv = 20, int nFoc = 20, int nRefinar = 3) {
    const DatosPieza& actual = piezas[i];
    std::uniform_real_distribution<double> ux(0.0, contW), uy(0.0, contH);
    BBox bbActual = Geo::bbox(actual.poly);
    double cx = (bbActual.minX + bbActual.maxX) / 2.0, cy = (bbActual.minY + bbActual.maxY) / 2.0;
    double radioFoco = std::max(bbActual.w, bbActual.h) * 1.5;
    if (radioFoco < 1e-6) radioFoco = 1.0;
    std::normal_distribution<double> nx(0.0, radioFoco), ny(0.0, radioFoco);

    double halfW = bbActual.w / 2.0, halfH = bbActual.h / 2.0;
    double loX = halfW, hiX = std::max(halfW, contW - halfW);
    double loY = halfH, hiY = std::max(halfH, contH - halfH);
    auto clamp = [](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };

    std::vector<std::pair<double, DatosPieza>> candidatos;
    auto probar = [&](double tx, double ty) {
        tx = clamp(tx, loX, hiX);
        ty = clamp(ty, loY, hiY);
        candidatos.push_back(mejorEnPosicion(tx, ty, variantes, i, piezas, pesos));
    };
    for (int k = 0; k < nDiv; k++) probar(ux(rng), uy(rng));
    for (int k = 0; k < nFoc; k++) probar(cx + nx(rng), cy + ny(rng));

    std::sort(candidatos.begin(), candidatos.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    DatosPieza mejor = candidatos.empty() ? actual : candidatos[0].second;
    double mejorEval = candidatos.empty() ? evaluarMuestra(i, actual, piezas, pesos) : candidatos[0].first;

    int limite = std::min(nRefinar, (int)candidatos.size());
    for (int k = 0; k < limite; k++) {
        DatosPieza c = candidatos[k].second;
        double eActual = candidatos[k].first;
        double paso = radioFoco * 0.3;
        for (int iter = 0; iter < 10 && paso > 0.05; iter++) {
            bool mejoro = false;
            static const std::pair<double,double> dirs[4] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& dir : dirs) {
                DatosPieza probando = trasladada(c, dir.first * paso, dir.second * paso);
                BBox bbP = Geo::bbox(probando.poly);
                if (bbP.minX < 0 || bbP.minY < 0 || bbP.maxX > contW || bbP.maxY > contH) continue;
                double e = evaluarMuestra(i, probando, piezas, pesos);
                if (e < eActual) { c = std::move(probando); eActual = e; mejoro = true; }
            }
            if (!mejoro) paso *= 0.5;
        }
        if (eActual < mejorEval) { mejorEval = eActual; mejor = std::move(c); }
    }
    return mejor;
}

inline void moverPiezasConRotacion(std::vector<DatosPieza>& piezas, const std::vector<PiezaRotable>& variantes,
                                    const MatrizPesos& pesos, double contW, double contH, std::mt19937& rng) {
    std::vector<int> idx;
    for (int i = 0; i < (int)piezas.size(); i++)
        if (piezaColisiona(i, piezas)) idx.push_back(i);
    std::shuffle(idx.begin(), idx.end(), rng);
    for (int i : idx)
        piezas[i] = buscarPosicionConRotacion(i, variantes[i], piezas, pesos, contW, contH, rng);
}

inline bool separarConRotacion(std::vector<DatosPieza>& piezas, const std::vector<PiezaRotable>& variantes,
                                double contW, double contH, std::mt19937& rng,
                                int maxIterSinMejora = 150, int maxIteraciones = 3000) {
    MatrizPesos pesos((int)piezas.size());
    double mejorPerdida = perdidaTotal(piezas);
    auto mejorEstado = piezas;
    int sinMejora = 0;
    for (int iter = 0; iter < maxIteraciones && mejorPerdida > 1e-6; iter++) {
        moverPiezasConRotacion(piezas, variantes, pesos, contW, contH, rng);
        actualizarPesos(pesos, piezas);
        double p = perdidaTotal(piezas);
        if (p < mejorPerdida - 1e-9) {
            mejorPerdida = p; mejorEstado = piezas; sinMejora = 0;
        } else {
            sinMejora++;
            if (sinMejora > maxIterSinMejora) { piezas = mejorEstado; sinMejora = 0; }
        }
    }
    piezas = mejorEstado;
    return mejorPerdida <= 1e-6;
}

// ===================== Fase 4 (REESCRITA): exploracion + compresion =====================
// Alg. 10 (move_items_multi), Alg. 11 (solve_ispp), Alg. 12 (explore), Alg. 13 (compress) --
// verificados linea por linea contra el texto del paper (arXiv:2509.13329, secciones 8.3 y 9)
// en esta sesion, con el PDF completo delante. Reemplaza la Fase 4 anterior, que segun el
// informe de la sesion previa NO habia pasado por este control de calidad.
//
// Correcciones respecto a la version anterior (ver informe, seccion "Pendiente" / bug encontrado):
//   1. explorar() ya NO reinicia todas las piezas al azar cuando un intento falla. El paper
//      mantiene un POOL de soluciones infactibles, elige una con probabilidad ponderada hacia
//      las mas cercanas a factibilidad, y la perturba intercambiando el centro de DOS piezas
//      grandes (no todas). Esto conserva casi todo el trabajo ya hecho por la busqueda.
//   2. comprimir() usa un paso de encogimiento que decae LINEALMENTE de R_c^s a R_c^e (Tabla 1:
//      0.05% a 0.001% del ancho) -- mucho mas fino que el 2% fijo usado antes.
//   3. separar() pasa a tener el limite de "strikes" (k_max) del Alg. 9 completo, mas
//      move_items_multi (Alg. 10: probar varios ordenes al azar en paralelo/secuencial y
//      quedarse con el mejor), cosas que la Fase 3 simplificaba deliberadamente.
//
// Parametros por defecto = Tabla 1 del paper (los que tienen sentido fuera de jagua-rs):
//   (Kx, Nx) = (3, 200)         separacion durante exploracion
//   (Kc, Nc) = (5, 100)         separacion durante compresion
//   Rx = 0.1%                   encogimiento por paso en exploracion
//   (Rc^s, Rc^e) = (0.05%, 0.001%)  rango de encogimiento en compresion (decae linealmente)
//   N_WORKERS = 3               intentos paralelos de move_items_multi
//
// Nota de alcance: el paper arranca explore() desde una solucion bottom-left-fill (Burke et
// al., 2006); ese constructor NO esta implementado aqui -- tu proyecto ya tiene su propio
// heuristico constructivo (joint_sa.hpp o similar). explorar() recibe piezas ya colocadas en
// alguna posicion inicial valida (dentro del contenedor) y trabaja desde ahi.
//
// Nota de alcance 2: "swapping two large items" (parrafo bajo Alg. 12) no se especifica con
// mas detalle en el paper. Aqui se interpreta como: elegir al azar dos piezas del tercio
// superior por tamano (lambda) e intercambiar sus centros -- variante estocastica de "las dos
// piezas mas grandes" para no perturbar siempre exactamente igual.
//
// Sin compilar/probar en esta sesion: no se tenia nesting_core.hpp real disponible (solo el
// geo_base.hpp reducido de sesiones anteriores). Revisar tipos/includes contra el proyecto
// real antes de dar esto por bueno -- sigue el mismo proceso de verificacion con tests que las
// Fases 1-3.

// Alg. 10: move_items_multi. Ejecuta el movimiento de piezas en colision "nWorkers" veces con
// ordenes al azar distintos, y se queda con el mejor resultado (menor perdida total).
inline void moverPiezasMulti(std::vector<DatosPieza>& piezas,
                              const std::vector<PiezaRotable>* variantes,
                              const MatrizPesos& pesos, double contW, double contH,
                              std::mt19937& rng, int nWorkers = 3) {
    std::vector<DatosPieza> original = piezas;
    std::vector<DatosPieza> mejor;
    double mejorZ = std::numeric_limits<double>::max();
    for (int w = 0; w < nWorkers; w++) {
        piezas = original;
        if (variantes) moverPiezasConRotacion(piezas, *variantes, pesos, contW, contH, rng);
        else moverPiezas(piezas, pesos, contW, contH, rng);
        double z = perdidaTotal(piezas);
        if (z < mejorZ) { mejorZ = z; mejor = piezas; }
    }
    piezas = mejor.empty() ? original : mejor;
}

// Alg. 9 completo (a diferencia de separar()/separarConRotacion() de la Fase 3, que omitian
// deliberadamente el limite de "strikes" k_max). variantes==nullptr usa las funciones sin
// rotacion (traslacion pura).
inline bool separarCompleto(std::vector<DatosPieza>& piezas,
                             const std::vector<PiezaRotable>* variantes,
                             double contW, double contH, std::mt19937& rng,
                             int kMax, int nMax, int nWorkers = 3) {
    MatrizPesos pesos((int)piezas.size());
    std::vector<DatosPieza> sEstrella = piezas;
    double zEstrella = perdidaTotal(piezas);
    int k = 0;
    while (k < kMax && zEstrella > 1e-6) {
        piezas = sEstrella;
        double zInicioIntento = zEstrella;
        int n = 0;
        while (n < nMax && zEstrella > 1e-6) {
            moverPiezasMulti(piezas, variantes, pesos, contW, contH, rng, nWorkers);
            actualizarPesos(pesos, piezas);
            double z = perdidaTotal(piezas);
            n++;
            if (z < zEstrella - 1e-9) { sEstrella = piezas; zEstrella = z; n = 0; }
        }
        k = (zEstrella < zInicioIntento - 1e-9) ? 0 : k + 1;
    }
    piezas = sEstrella;
    return zEstrella <= 1e-6;
}

// Intercambia los centros de dos piezas grandes elegidas al azar entre el tercio superior por
// tamano (lambda = penalizacionForma). Ver "Nota de alcance 2" arriba.
inline void intercambiarDosGrandes(std::vector<DatosPieza>& piezas, std::mt19937& rng) {
    int n = (int)piezas.size();
    if (n < 2) return;
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return piezas[a].lambda > piezas[b].lambda; });
    int topN = std::max(2, n / 3);
    std::uniform_int_distribution<int> dist(0, topN - 1);
    int ia = idx[dist(rng)];
    int ib = idx[dist(rng)];
    while (ib == ia && topN > 1) ib = idx[dist(rng)];
    if (ia == ib) return;
    BBox bbA = Geo::bbox(piezas[ia].poly), bbB = Geo::bbox(piezas[ib].poly);
    double cxA = (bbA.minX + bbA.maxX) / 2.0, cyA = (bbA.minY + bbA.maxY) / 2.0;
    double cxB = (bbB.minX + bbB.maxX) / 2.0, cyB = (bbB.minY + bbB.maxY) / 2.0;
    trasladarEnSitio(piezas[ia], cxB - cxA, cyB - cyA);
    trasladarEnSitio(piezas[ib], cxA - cxB, cyA - cyB);
}

// Encoge el ancho del contenedor: elige el nuevo borde derecho como eje y desplaza hacia la
// izquierda las piezas cuyo centro cae a la derecha de ese eje (tal como describe el paper
// bajo Alg. 12: "selecting a certain vertical axis... shifting items positioned right of this
// axis to the left").
// CORRECCION DE EJE (esta sesion, tras revisar la definicion formal del 2DISPP en literatura
// relacionada citada por el propio paper): la tira de 2D strip packing tiene ANCHO FIJO y se
// minimiza el LARGO -- ver p.ej. Lastra-Diaz & Ortuno, Peralta et al., Wikipedia "Strip packing
// problem". Esto coincide con la convencion de este proyecto (`telaW` es fijo, `altura` es lo
// que se minimiza en `NestingEngine`). La primera version de esta Fase 4 (mas arriba en el
// historial de este archivo/informe) encogia por error el ANCHO (`encogerAncho`, operando
// sobre `contW`) en vez del LARGO. Se corrige aqui: la funcion de encogimiento y explorar()/
// comprimir() ahora operan sobre `contH` (el largo/altura), dejando `contW` (el ancho de tela)
// fijo durante toda la Fase 4, tal como corresponde al problema real.
inline void encogerLargo(std::vector<DatosPieza>& piezas, double contHViejo, double contHNuevo) {
    double delta = contHViejo - contHNuevo;
    if (delta <= 0) return;
    for (auto& d : piezas) {
        BBox bb = Geo::bbox(d.poly);
        double cy = (bb.minY + bb.maxY) / 2.0;
        if (cy > contHNuevo) trasladarEnSitio(d, 0.0, -delta);
    }
}

// BUG ENCONTRADO Y CORREGIDO EN ESTA SESION (al probar explorar()/comprimir() con un smoke
// test de 4 cuadrados): perdidaTotal() solo mide colision pieza-contra-pieza, nunca contencion
// contra las paredes del contenedor (a diferencia de jagua-rs en el paper, donde la contencion
// es una restriccion dura que el CDE reporta como si fuera otra "colision", ver Eq. 1 y Alg. 1
// del paper). Como consecuencia, encogerLargo() e intercambiarDosGrandes() pueden dejar una
// pieza fuera del contenedor sin que eso se detecte nunca: si esa pieza no queda solapada con
// ninguna otra, piezaColisiona() la ignora para siempre y se queda pegada afuera. Se confirmo
// con un test de 4 cuadrados de 5x5 (version anterior, encogiendo ancho en vez de largo) --
// y en efecto una pieza tenia su bbox saliendose del contenedor.
//
// Correccion: reencajar (clamp duro contra las paredes) cualquier pieza que quede fuera
// inmediatamente despues de las dos operaciones que pueden sacarla de los limites. Si el
// reencaje genera una colision con otra pieza, esa colision SI es detectada por
// piezaColisiona() en la siguiente llamada a separarCompleto() y se resuelve normalmente.
inline void reencajarEnContenedor(DatosPieza& d, double contW, double contH) {
    BBox bb = Geo::bbox(d.poly);
    double dx = 0.0, dy = 0.0;
    if (bb.minX < 0) dx = -bb.minX;
    else if (bb.maxX > contW) dx = contW - bb.maxX;
    if (bb.minY < 0) dy = -bb.minY;
    else if (bb.maxY > contH) dy = contH - bb.maxY;
    if (dx != 0.0 || dy != 0.0) trasladarEnSitio(d, dx, dy);
}

inline void reencajarTodas(std::vector<DatosPieza>& piezas, double contW, double contH) {
    for (auto& d : piezas) reencajarEnContenedor(d, contW, contH);
}

struct ResultadoISPP {
    std::vector<DatosPieza> piezas;
    double largo; // altura/largo de tela usado (contW se mantiene fijo durante toda la Fase 4)
};

// Alg. 12: explore. Arranca desde "piezasIniciales" (ya colocadas, ver nota de alcance),
// encoge el LARGO un poco (Rx), intenta separar; si factible, guarda como mejor y sigue
// encogiendo; si no, guarda el intento en el pool y continua desde una perturbacion de una
// solucion del pool (ver intercambiarDosGrandes). Se detiene por tiempo (tiempoLimiteSeg).
// `contW` (ancho de tela) es fijo durante toda la funcion.
inline ResultadoISPP explorar(std::vector<DatosPieza> piezasIniciales,
                               const std::vector<PiezaRotable>* variantes,
                               double contW, double largoInicial, std::mt19937& rng,
                               double tiempoLimiteSeg,
                               int kX = 3, int nX = 200, double rX = 0.001) {
    auto t0 = std::chrono::steady_clock::now();
    auto transcurrido = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    double largo = largoInicial;
    std::vector<DatosPieza> actual = piezasIniciales;
    std::vector<DatosPieza> sEstrella = actual;
    double largoEstrella = largo;
    std::vector<std::pair<double, std::vector<DatosPieza>>> pool;

    while (transcurrido() < tiempoLimiteSeg) {
        bool factible = separarCompleto(actual, variantes, contW, largo, rng, kX, nX);
        if (factible) {
            sEstrella = actual; largoEstrella = largo;
            double largoViejo = largo;
            largo *= (1.0 - rX);
            encogerLargo(actual, largoViejo, largo);
            reencajarTodas(actual, contW, largo);
            pool.clear();
        } else {
            pool.push_back({perdidaTotal(actual), actual});
            double suma = 0.0;
            std::vector<double> pesosSel(pool.size());
            for (size_t i = 0; i < pool.size(); i++) {
                pesosSel[i] = 1.0 / (1.0 + pool[i].first); // menor perdida -> mas peso
                suma += pesosSel[i];
            }
            std::uniform_real_distribution<double> u(0.0, suma);
            double r = u(rng);
            size_t elegido = 0;
            for (; elegido < pool.size(); elegido++) {
                r -= pesosSel[elegido];
                if (r <= 0) break;
            }
            if (elegido >= pool.size()) elegido = pool.size() - 1;
            actual = pool[elegido].second;
            intercambiarDosGrandes(actual, rng);
            reencajarTodas(actual, contW, largo);
        }
    }
    return { sEstrella, largoEstrella };
}

// Alg. 13: compress. Arranca desde la mejor solucion factible de explorar(). En cada paso
// encoge un poco mas el LARGO (paso r decae LINEALMENTE de rCInicio a rCFin segun el tiempo
// restante de la fase -- Tabla 1: 0.05% a 0.001%), intenta separar con parametros mas
// restrictivos (Kc, Nc); si falla, descarta el intento y reintenta desde el ultimo largo que
// si funciono. `contW` (ancho de tela) es fijo durante toda la funcion.
inline ResultadoISPP comprimir(std::vector<DatosPieza> piezas,
                                const std::vector<PiezaRotable>* variantes,
                                double contW, double largo, std::mt19937& rng,
                                double tiempoLimiteSeg,
                                int kC = 5, int nC = 100,
                                double rCInicio = 0.0005, double rCFin = 0.00001) {
    auto t0 = std::chrono::steady_clock::now();
    auto transcurrido = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    std::vector<DatosPieza> sEstrella = piezas;
    double largoEstrella = largo;

    while (transcurrido() < tiempoLimiteSeg) {
        double tau = transcurrido();
        double r = rCInicio + (rCFin - rCInicio) * std::min(1.0, tau / tiempoLimiteSeg);
        std::vector<DatosPieza> intento = sEstrella;
        double largoNuevo = largoEstrella * (1.0 - r);
        encogerLargo(intento, largoEstrella, largoNuevo);
        reencajarTodas(intento, contW, largoNuevo);
        bool factible = separarCompleto(intento, variantes, contW, largoNuevo, rng, kC, nC);
        if (factible) { sEstrella = intento; largoEstrella = largoNuevo; }
    }
    return { sEstrella, largoEstrella };
}

// Alg. 11: solve_ispp. Encadena explorar() + comprimir(). Los tiempos limite se pasan por
// separado porque en el proyecto real probablemente se quiera repartir un presupuesto total
// (el paper usa 80%/20% -- Tabla 1: TLx=0.8*20min, TLc=0.2*20min). `contW` (ancho de tela)
// fijo, se comprime `largoInicial` (equivalente a "altura" en NestingEngine).
inline ResultadoISPP resolverISPP(std::vector<DatosPieza> piezasIniciales,
                                   const std::vector<PiezaRotable>* variantes,
                                   double contW, double largoInicial, std::mt19937& rng,
                                   double tiempoLimiteExplorarSeg, double tiempoLimiteComprimirSeg) {
    ResultadoISPP r1 = explorar(std::move(piezasIniciales), variantes, contW, largoInicial, rng,
                                 tiempoLimiteExplorarSeg);
    return comprimir(std::move(r1.piezas), variantes, contW, r1.largo, rng, tiempoLimiteComprimirSeg);
}

} // namespace Sparrow

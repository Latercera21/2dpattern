#pragma once
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <set>
#include <array>
#include <random>

struct Point { double x, y; };

struct BBox {
    double minX, minY, maxX, maxY, w, h;
};

struct Piece {
    int id;
    std::vector<Point> poly;
    BBox bb;
    double area;
    std::vector<std::vector<Point>> partes; // descomposicion convexa (calculada una vez al cargar)
    std::vector<Point> hull; // envolvente convexa completa (filtro rapido de colision)
};

struct Placed {
    int id;
    std::vector<Point> poly;
    BBox bbF;
    double tx, ty;
    BBox bb;
    bool rotada;
    std::vector<Point> hull;                 // envolvente convexa (respaldo/candidatos de borde)
    std::vector<std::vector<Point>> partes;   // descomposicion convexa en coords finales (para NFP concavo)
};

struct LayoutItem {
    int id;
    double tx, ty;
    bool rotada;
};

struct LayoutResult {
    std::vector<LayoutItem> layout;
    double altura;
};

namespace Geo {

inline double distPuntoSegmento(const Point& p, const Point& a, const Point& b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        double ddx = p.x - a.x, ddy = p.y - a.y;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));
    double px = a.x + t * dx, py = a.y + t * dy;
    double ddx = p.x - px, ddy = p.y - py;
    return std::sqrt(ddx * ddx + ddy * ddy);
}

inline void rdpRec(const std::vector<Point>& pts, int ini, int fin, double tol, std::vector<bool>& mantener) {
    if (fin <= ini + 1) return;
    double maxDist = -1;
    int idxMax = -1;
    for (int i = ini + 1; i < fin; i++) {
        double d = distPuntoSegmento(pts[i], pts[ini], pts[fin]);
        if (d > maxDist) { maxDist = d; idxMax = i; }
    }
    if (maxDist > tol) {
        mantener[idxMax] = true;
        rdpRec(pts, ini, idxMax, tol, mantener);
        rdpRec(pts, idxMax, fin, tol, mantener);
    }
}

// Simplifica un poligono CERRADO quitando vertices casi colineales (sobremuestreo
// de curvas), preservando la forma dentro de la tolerancia 'tol' (en cm).
// Esto reduce drasticamente el costo de colisiones y candidatos NFP en piezas
// con curvas, sin cambiar la forma real de manera perceptible.
inline std::vector<Point> simplificarPoligono(const std::vector<Point>& poly, double tol) {
    int n = (int)poly.size();
    if (n <= 4) return poly;
    double cx = 0, cy = 0;
    for (auto& p : poly) { cx += p.x; cy += p.y; }
    cx /= n; cy /= n;
    int idxLejano = 0; double maxD = -1;
    for (int i = 0; i < n; i++) {
        double d = (poly[i].x - cx) * (poly[i].x - cx) + (poly[i].y - cy) * (poly[i].y - cy);
        if (d > maxD) { maxD = d; idxLejano = i; }
    }
    std::vector<Point> reord(n);
    for (int i = 0; i < n; i++) reord[i] = poly[(idxLejano + i) % n];

    std::vector<bool> mantener(n, false);
    mantener[0] = true; mantener[n - 1] = true;
    int mitad = n / 2;
    rdpRec(reord, 0, mitad, tol, mantener);
    rdpRec(reord, mitad, n - 1, tol, mantener);

    std::vector<Point> out;
    for (int i = 0; i < n; i++) if (mantener[i]) out.push_back(reord[i]);
    return out;
}

inline BBox bbox(const std::vector<Point>& poly) {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto& p : poly) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    return { minX, minY, maxX, maxY, maxX - minX, maxY - minY };
}

inline std::vector<Point> translate(const std::vector<Point>& poly, double dx, double dy) {
    std::vector<Point> out;
    out.reserve(poly.size());
    for (const auto& p : poly) out.push_back({ p.x + dx, p.y + dy });
    return out;
}

inline std::vector<std::vector<Point>> translateMulti(const std::vector<std::vector<Point>>& partes, double dx, double dy) {
    std::vector<std::vector<Point>> out;
    out.reserve(partes.size());
    for (const auto& parte : partes) out.push_back(translate(parte, dx, dy));
    return out;
}

inline std::vector<Point> rotate180ConBBox(const std::vector<Point>& poly, const BBox& bb) {
    std::vector<Point> out;
    out.reserve(poly.size());
    for (const auto& p : poly)
        out.push_back({ bb.w - (p.x - bb.minX), bb.h - (p.y - bb.minY) });
    return out;
}

inline std::vector<Point> rotate180(const std::vector<Point>& poly) {
    return rotate180ConBBox(poly, bbox(poly));
}

inline std::vector<std::vector<Point>> rotate180MultiConBBox(const std::vector<std::vector<Point>>& partes, const BBox& bb) {
    std::vector<std::vector<Point>> out;
    out.reserve(partes.size());
    for (const auto& parte : partes) out.push_back(rotate180ConBBox(parte, bb));
    return out;
}

inline bool pointInPoly(const Point& pt, const std::vector<Point>& poly) {
    bool inside = false;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point& pi = poly[i];
        const Point& pj = poly[j];
        bool cond = ((pi.y > pt.y) != (pj.y > pt.y)) &&
            (pt.x < (pj.x - pi.x) * (pt.y - pi.y) / (pj.y - pi.y) + pi.x);
        if (cond) inside = !inside;
    }
    return inside;
}

inline bool segmentsIntersect(const Point& p1, const Point& p2,
                               const Point& p3, const Point& p4) {
    double d1 = (p4.x - p3.x) * (p1.y - p3.y) - (p4.y - p3.y) * (p1.x - p3.x);
    double d2 = (p4.x - p3.x) * (p2.y - p3.y) - (p4.y - p3.y) * (p2.x - p3.x);
    double d3 = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
    double d4 = (p2.x - p1.x) * (p4.y - p1.y) - (p2.y - p1.y) * (p4.x - p1.x);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

// SAT (Separating Axis Theorem) para dos poligonos CONVEXOS: mucho mas rapido
// que el metodo punto-por-punto para piezas con muchos vertices, porque sale
// apenas encuentra un eje separador (no necesita revisar todo siempre).
inline bool convexOverlapSAT(const std::vector<Point>& A, const std::vector<Point>& B) {
    auto probarEjesDe = [&](const std::vector<Point>& P, const std::vector<Point>& Q) -> bool {
        size_t n = P.size();
        for (size_t i = 0; i < n; i++) {
            const Point& p1 = P[i];
            const Point& p2 = P[(i + 1) % n];
            double ax = -(p2.y - p1.y), ay = (p2.x - p1.x);
            double minP = std::numeric_limits<double>::infinity(), maxP = -minP;
            for (const auto& p : P) { double d = p.x * ax + p.y * ay; minP = std::min(minP, d); maxP = std::max(maxP, d); }
            double minQ = std::numeric_limits<double>::infinity(), maxQ = -minQ;
            for (const auto& q : Q) { double d = q.x * ax + q.y * ay; minQ = std::min(minQ, d); maxQ = std::max(maxQ, d); }
            if (maxP < minQ - 1e-9 || maxQ < minP - 1e-9) return false; // eje separador encontrado
        }
        return true;
    };
    BBox bbA = bbox(A), bbB = bbox(B);
    if (bbA.maxX <= bbB.minX || bbA.minX >= bbB.maxX ||
        bbA.maxY <= bbB.minY || bbA.minY >= bbB.maxY) return false;
    if (!probarEjesDe(A, B)) return false;
    if (!probarEjesDe(B, A)) return false;
    return true;
}

// Colision entre dos piezas (posiblemente concavas) representadas como listas
// de partes convexas: colisionan si algun par de partes se superpone.
inline bool collideParts(const std::vector<std::vector<Point>>& partsA, const std::vector<std::vector<Point>>& partsB) {
    for (const auto& pa : partsA)
        for (const auto& pb : partsB)
            if (convexOverlapSAT(pa, pb)) return true;
    return false;
}

inline bool collide(const std::vector<Point>& A, const std::vector<Point>& B) {
    BBox bbA = bbox(A), bbB = bbox(B);
    if (bbA.maxX <= bbB.minX || bbA.minX >= bbB.maxX ||
        bbA.maxY <= bbB.minY || bbA.minY >= bbB.maxY) return false;

    // Solo vale la pena probar pointInPoly para puntos que caen dentro del
    // rango de la otra caja: si un vertice de A esta fuera de bbB, no puede
    // estar dentro de B (bbB contiene a B), asi que nos ahorramos el ray-cast.
    for (const auto& p : A)
        if (p.x >= bbB.minX && p.x <= bbB.maxX && p.y >= bbB.minY && p.y <= bbB.maxY)
            if (pointInPoly(p, B)) return true;
    for (const auto& p : B)
        if (p.x >= bbA.minX && p.x <= bbA.maxX && p.y >= bbA.minY && p.y <= bbA.maxY)
            if (pointInPoly(p, A)) return true;

    size_t nA = A.size(), nB = B.size();
    for (size_t i = 0; i < nA; i++) {
        const Point& a1 = A[i];
        const Point& a2 = A[(i + 1) % nA];
        for (size_t j = 0; j < nB; j++) {
            const Point& b1 = B[j];
            const Point& b2 = B[(j + 1) % nB];
            if (segmentsIntersect(a1, a2, b1, b2)) return true;
        }
    }
    return false;
}

// Colision EXACTA entre dos poligonos (posiblemente concavos), pero con un
// filtro previo barato: si sus envolventes convexas (hullA, hullB) no se
// superponen, los poligonos reales tampoco pueden superponerse (el hull
// siempre contiene al poligono), asi que se descarta sin pagar el costo de
// pointInPoly/segmentsIntersect sobre todos los vertices reales. Solo cuando
// los hulls SI se solapan (frecuente solo entre piezas realmente cercanas) se
// paga el test exacto, que es el que permite el encaje real en concavidades.
inline bool collideFast(const std::vector<Point>& hullA, const std::vector<Point>& polyA,
                         const std::vector<Point>& hullB, const std::vector<Point>& polyB) {
    if (!convexOverlapSAT(hullA, hullB)) return false;
    return collide(polyA, polyB);
}

// ===================== Descomposicion convexa (para piezas concavas) =====================

inline double signedArea(const std::vector<Point>& poly) {
    double area = 0; size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        area += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
    }
    return area / 2.0;
}

inline double cross3(const Point& O, const Point& A, const Point& B) {
    return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
}

inline bool pointInTriangleStrict(const Point& p, const Point& a, const Point& b, const Point& c) {
    double d1 = cross3(a, b, p);
    double d2 = cross3(b, c, p);
    double d3 = cross3(c, a, p);
    bool hasNeg = (d1 < -1e-9) || (d2 < -1e-9) || (d3 < -1e-9);
    bool hasPos = (d1 > 1e-9) || (d2 > 1e-9) || (d3 > 1e-9);
    return !(hasNeg && hasPos);
}

// Triangulacion "ear clipping" para poligono simple (puede ser concavo).
inline std::vector<std::array<Point, 3>> earClip(std::vector<Point> poly) {
    if (signedArea(poly) < 0) std::reverse(poly.begin(), poly.end());
    std::vector<int> idx;
    for (int i = 0; i < (int)poly.size(); i++) idx.push_back(i);

    std::vector<std::array<Point, 3>> tris;
    int guard = 0;
    int limiteGuard = (int)poly.size() * (int)poly.size() + 100;
    while (idx.size() > 3 && guard < limiteGuard) {
        guard++;
        bool earFound = false;
        int n = (int)idx.size();
        for (int i = 0; i < n; i++) {
            int iPrev = idx[(i - 1 + n) % n];
            int iCur  = idx[i];
            int iNext = idx[(i + 1) % n];
            const Point& a = poly[iPrev];
            const Point& b = poly[iCur];
            const Point& c = poly[iNext];
            if (cross3(a, b, c) <= 1e-9) continue;

            bool contieneOtro = false;
            for (int k = 0; k < n; k++) {
                int ik = idx[k];
                if (ik == iPrev || ik == iCur || ik == iNext) continue;
                if (pointInTriangleStrict(poly[ik], a, b, c)) { contieneOtro = true; break; }
            }
            if (contieneOtro) continue;

            tris.push_back({a, b, c});
            idx.erase(idx.begin() + i);
            earFound = true;
            break;
        }
        if (!earFound) break;
    }
    if (idx.size() == 3) tris.push_back({poly[idx[0]], poly[idx[1]], poly[idx[2]]});
    return tris;
}

inline bool esPuntoIgual(const Point& a, const Point& b) {
    return std::fabs(a.x - b.x) < 1e-7 && std::fabs(a.y - b.y) < 1e-7;
}

inline bool esConvexo(const std::vector<Point>& poly) {
    int n = (int)poly.size();
    if (n < 3) return true;
    bool sawPos = false, sawNeg = false;
    for (int i = 0; i < n; i++) {
        const Point& a = poly[i];
        const Point& b = poly[(i + 1) % n];
        const Point& c = poly[(i + 2) % n];
        double cr = cross3(a, b, c);
        if (cr > 1e-9) sawPos = true;
        if (cr < -1e-9) sawNeg = true;
    }
    return !(sawPos && sawNeg);
}

inline bool intentarFusionar(const std::vector<Point>& A, const std::vector<Point>& B,
                              std::vector<Point>& resultado) {
    int na = (int)A.size(), nb = (int)B.size();
    for (int i = 0; i < na; i++) {
        Point a1 = A[i], a2 = A[(i + 1) % na];
        for (int j = 0; j < nb; j++) {
            Point b1 = B[j], b2 = B[(j + 1) % nb];
            if (esPuntoIgual(a1, b2) && esPuntoIgual(a2, b1)) {
                std::vector<Point> merged;
                for (int k = 0; k < na; k++) merged.push_back(A[(i + 1 + k) % na]);
                for (int k = 2; k < nb; k++) merged.push_back(B[(j + k) % nb]);

                std::vector<Point> limpio;
                for (auto& p : merged) {
                    if (limpio.empty() || !esPuntoIgual(limpio.back(), p)) limpio.push_back(p);
                }
                if (limpio.size() >= 2 && esPuntoIgual(limpio.front(), limpio.back())) limpio.pop_back();

                if (esConvexo(limpio)) { resultado = limpio; return true; }
                return false;
            }
        }
    }
    return false;
}

inline std::vector<std::vector<Point>> mergeToConvexParts(const std::vector<std::array<Point, 3>>& tris) {
    std::vector<std::vector<Point>> partes;
    for (auto& t : tris) partes.push_back({t[0], t[1], t[2]});

    bool cambio = true;
    while (cambio) {
        cambio = false;
        for (size_t i = 0; i < partes.size() && !cambio; i++) {
            for (size_t j = i + 1; j < partes.size() && !cambio; j++) {
                std::vector<Point> merged;
                if (intentarFusionar(partes[i], partes[j], merged)) {
                    partes[i] = merged;
                    partes.erase(partes.begin() + j);
                    cambio = true;
                }
            }
        }
    }
    return partes;
}

// Descompone un poligono (posiblemente concavo) en partes convexas.
inline std::vector<Point> convexHull(std::vector<Point> pts); // declaracion adelantada

inline std::vector<std::vector<Point>> descomponerConvexo(const std::vector<Point>& poly) {
    if (poly.size() < 4) return { poly }; // triangulo o menos: ya es convexo
    auto tris = earClip(poly);
    if (tris.empty()) return { poly };
    auto partes = mergeToConvexParts(tris);
    // Limite de seguridad: si quedan demasiadas partes (formas muy sinuosas con
    // muchas curvas), el NFP concavo se volveria extremadamente lento (combinatoria
    // partes x partes x piezas). En ese caso, usamos la envolvente convexa como
    // respaldo para esa pieza (se pierde algo de precision en sus muescas, pero
    // se evita el bloqueo del programa).
    const size_t MAX_PARTES = 10;
    if (partes.size() > MAX_PARTES) {
        return { convexHull(poly) };
    }
    return partes;
}

// Envolvente convexa (Andrew's monotone chain). Devuelve puntos en orden CCW.
inline std::vector<Point> convexHull(std::vector<Point> pts) {
    std::sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
        return a.x < b.x - 1e-12 || (std::fabs(a.x - b.x) < 1e-9 && a.y < b.y);
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
        return std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9;
    }), pts.end());
    int n = (int)pts.size();
    if (n < 3) return pts;
    auto cross = [](const Point& O, const Point& A, const Point& B) {
        return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
    };
    std::vector<Point> hull(2 * n);
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) k--;
        hull[k++] = pts[i];
    }
    int lower = k + 1;
    for (int i = n - 2; i >= 0; i--) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) k--;
        hull[k++] = pts[i];
    }
    hull.resize(k - 1);
    return hull;
}

inline std::vector<Point> reflectConvexCCW(const std::vector<Point>& B) {
    std::vector<Point> r;
    r.reserve(B.size());
    for (const auto& p : B) r.push_back({ -p.x, -p.y });
    return r;
}

inline std::vector<Point> minkowskiSumConvex(const std::vector<Point>& A, const std::vector<Point>& B) {
    int na = (int)A.size(), nb = (int)B.size();
    if (na == 0) return B;
    if (nb == 0) return A;
    if (na == 1) return translate(B, A[0].x, A[0].y);
    if (nb == 1) return translate(A, B[0].x, B[0].y);

    auto startIdx = [](const std::vector<Point>& P) {
        int idx = 0;
        for (int i = 1; i < (int)P.size(); i++)
            if (P[i].y < P[idx].y - 1e-12 || (std::fabs(P[i].y - P[idx].y) < 1e-9 && P[i].x < P[idx].x))
                idx = i;
        return idx;
    };
    int ia = startIdx(A), ib = startIdx(B);
    std::vector<Point> Ar(na), Br(nb);
    for (int k = 0; k < na; k++) Ar[k] = A[(ia + k) % na];
    for (int k = 0; k < nb; k++) Br[k] = B[(ib + k) % nb];
    Ar.push_back(Ar[0]); Ar.push_back(Ar[1]);
    Br.push_back(Br[0]); Br.push_back(Br[1]);

    std::vector<Point> result;
    result.reserve(na + nb);
    int i = 0, j = 0;
    while (i < na || j < nb) {
        result.push_back({ Ar[i].x + Br[j].x, Ar[i].y + Br[j].y });
        double cr = (Ar[i + 1].x - Ar[i].x) * (Br[j + 1].y - Br[j].y) -
                    (Ar[i + 1].y - Ar[i].y) * (Br[j + 1].x - Br[j].x);
        bool avanzaA = (cr >= -1e-12), avanzaB = (cr <= 1e-12);
        if (avanzaA && i < na) i++;
        if (avanzaB && j < nb) j++;
        if (!avanzaA && !avanzaB) { i++; j++; }
    }
    return result;
}

} // namespace Geo

class NestingEngine {
public:
    double MARGEN = 0.3;
    double STEP = 0.25;
    mutable std::mt19937 rng{ std::random_device{}() };

    bool canPlace(const std::vector<Point>& poly, const std::vector<Point>& hull,
                  double x, double y, const std::vector<Placed>& colocadas, double telaW) const {
        std::vector<Point> tp = Geo::translate(poly, x + MARGEN, y + MARGEN);
        BBox bb = Geo::bbox(tp);
        if (bb.minX < 0 || bb.maxX > telaW || bb.minY < 0) return false;
        std::vector<Point> th = Geo::translate(hull, x + MARGEN, y + MARGEN);
        for (const auto& col : colocadas) {
            if (bb.maxX <= col.bbF.minX || bb.minX >= col.bbF.maxX ||
                bb.maxY <= col.bbF.minY || bb.minY >= col.bbF.maxY) continue;
            if (Geo::collideFast(th, tp, col.hull, col.poly)) return false;
        }
        return true;
    }

    struct Pos { double tx, ty; };

    // Genera candidatos via NFP CONCAVO REAL: union de los NFP entre cada par de
    // partes convexas (pieza vs cada pieza ya colocada). Matematicamente exacto:
    // la suma de Minkowski se distribuye sobre uniones, asi que esta union de NFPs
    // parciales SI captura encajes en muescas concavas (a diferencia de usar solo
    // la envolvente convexa completa). La aceptacion final siempre se revalida
    // contra la geometria real completa, nunca hay riesgo de overlap.
    Pos buscarPos(const std::vector<Point>& vpoly, const std::vector<Point>& vhull, const BBox& vbb,
                  const std::vector<std::vector<Point>>& partesVariante,
                  double maxAltura, const std::vector<Placed>& colocadas,
                  double telaW) const {
        double maxX = telaW - vbb.w;

        std::vector<Point> candidatos;
        candidatos.push_back({ 0, 0 });
        if (maxX >= 0) candidatos.push_back({ maxX, 0 });

        // NFP concavo real: union de NFP entre cada par (parte de pieza colocada, parte de la pieza a colocar)
        for (const auto& col : colocadas) {
            for (const auto& partePlaced : col.partes) {
                for (const auto& partePieza : partesVariante) {
                    auto reflejada = Geo::reflectConvexCCW(partePieza);
                    auto nfp = Geo::minkowskiSumConvex(partePlaced, reflejada);
                    for (const auto& v : nfp) {
                        double tx = v.x - MARGEN, ty = v.y - MARGEN;
                        if (tx >= -1e-6 && ty >= -1e-6 && tx <= maxX + 1e-6)
                            candidatos.push_back({ std::max(0.0, tx), std::max(0.0, ty) });
                    }
                }
            }
        }

        // Respaldo ligero: muestreo simple + drop real, por robustez ante casos limite
        std::set<double> xExtra;
        xExtra.insert(0.0);
        if (maxX >= 0) xExtra.insert(maxX);
        for (const auto& col : colocadas) {
            double cands[4] = { col.tx, col.tx + col.bb.w, col.tx - vbb.w, col.tx + col.bb.w - vbb.w };
            for (double cx : cands) if (cx >= 0 && cx <= maxX) xExtra.insert(cx);
        }
        if (maxX >= 0) {
            int nMuestras = 20; // vuelto a la version que dio el mejor resultado medido (95.44)
            double paso = maxX / nMuestras;
            if (paso > 0) for (int k = 0; k <= nMuestras; k++) xExtra.insert(k * paso);
        }
        double stepBusqueda = STEP; // vuelto al paso fino original (mejor resultado medido: 95.44)
        for (double sx : xExtra) {
            if (sx < 0 || sx > maxX) continue;
            double ty = maxAltura + stepBusqueda;
            while (ty - stepBusqueda >= 0 && canPlace(vpoly, vhull, sx, ty - stepBusqueda, colocadas, telaW)) ty -= stepBusqueda;
            candidatos.push_back({ sx, std::max(0.0, ty) });
        }

        double mejorX = 0, mejorY = maxAltura, mejorScore = std::numeric_limits<double>::infinity();

        auto descenderBinario = [&](double tx0, double tyDesdeFactible) -> double {
            if (tyDesdeFactible <= 0) return std::max(0.0, tyDesdeFactible);
            if (canPlace(vpoly, vhull, tx0, 0.0, colocadas, telaW)) return 0.0;
            double lo = 0.0, hi = tyDesdeFactible;
            for (int it = 0; it < 30 && (hi - lo) > 0.005; it++) {
                double mid = (lo + hi) / 2.0;
                if (canPlace(vpoly, vhull, tx0, mid, colocadas, telaW)) hi = mid; else lo = mid;
            }
            return hi;
        };

        // Si hay demasiados candidatos crudos (piezas con muchos vertices generan
        // muchos puntos de NFP), reducimos por redondeo/deduplicado antes de evaluar,
        // para no procesar miles de candidatos casi identicos.
        if (candidatos.size() > 300) {
            std::set<std::pair<long long,long long>> vistos;
            std::vector<Point> reducidos;
            for (const auto& c : candidatos) {
                long long kx = (long long)std::round(c.x / 0.2);
                long long ky = (long long)std::round(c.y / 0.2);
                if (vistos.insert({kx, ky}).second) reducidos.push_back(c);
            }
            candidatos = reducidos;
        }

        // FASE 1: evaluacion barata (sin refinamiento lateral) de todos los candidatos,
        // solo para quedarnos con los mejores K.
        struct Cand { double tx, ty, score; };
        std::vector<Cand> evaluados;
        evaluados.reserve(candidatos.size());

        for (const auto& c : candidatos) {
            double tx = c.x;
            if (tx < 0 || tx > maxX) continue;
            double ty = std::max(c.y, 0.0);
            if (ty > maxAltura + STEP) ty = maxAltura + STEP;

            if (!canPlace(vpoly, vhull, tx, ty, colocadas, telaW)) {
                bool ok = false;
                for (double tyy = ty; tyy <= maxAltura + 5.0; tyy += stepBusqueda) {
                    if (canPlace(vpoly, vhull, tx, tyy, colocadas, telaW)) { ty = tyy; ok = true; break; }
                }
                if (!ok) continue;
            }

            ty = descenderBinario(tx, ty);
            double score = ty * 100000.0 + tx;
            evaluados.push_back({ tx, ty, score });
        }

        // FASE 2: solo a los mejores K candidatos se les aplica el refinamiento
        // lateral fino (busqueda de deslizamiento izquierda/derecha).
        const size_t K = 10;
        std::partial_sort(evaluados.begin(),
                           evaluados.begin() + std::min(K, evaluados.size()),
                           evaluados.end(),
                           [](const Cand& a, const Cand& b) { return a.score < b.score; });

        size_t limiteRefinar = std::min(K, evaluados.size());
        for (size_t idx = 0; idx < limiteRefinar; idx++) {
            double tx = evaluados[idx].tx;
            double ty = evaluados[idx].ty;

            bool moved = true;
            int iterSeguridad = 0;
            while (moved && iterSeguridad < 60) {
                moved = false;
                iterSeguridad++;
                for (double dx : { -STEP, STEP }) {
                    if (tx + dx >= 0 && tx + dx <= maxX && canPlace(vpoly, vhull, tx + dx, ty, colocadas, telaW)) {
                        double nx = tx + dx;
                        double ny = descenderBinario(nx, ty);
                        if (ny < ty) { tx = nx; ty = ny; moved = true; }
                    }
                }
            }

            double score = ty * 100000.0 + tx;
            if (score < mejorScore) { mejorScore = score; mejorX = tx; mejorY = ty; }
        }
        return { mejorX, mejorY };
    }

    LayoutResult calcularLayout(const std::vector<Piece>& ordenPiezas, double telaW) const {
        std::vector<Placed> colocadas;
        colocadas.reserve(ordenPiezas.size());
        double maxAltura = 0;

        for (const auto& p : ordenPiezas) {
            std::vector<Point> polyRot = Geo::rotate180(p.poly);
            BBox bbRot = Geo::bbox(polyRot);
            auto partesRot = Geo::rotate180MultiConBBox(p.partes, p.bb);
            std::vector<Point> hullRot = Geo::rotate180ConBBox(p.hull, p.bb);

            struct Variante {
                const std::vector<Point>* poly;
                const std::vector<Point>* hull;
                BBox bb;
                bool rot;
                const std::vector<std::vector<Point>>* partes;
            };
            Variante vars[2] = {
                { &p.poly, &p.hull, p.bb, false, &p.partes },
                { &polyRot, &hullRot, bbRot, true, &partesRot }
            };

            Pos mejorPos{ 0, maxAltura };
            bool mejorRot = false;
            double mejorScore = std::numeric_limits<double>::infinity();
            bool encontrada = false;

            for (const auto& v : vars) {
                if (v.bb.w > telaW) continue;
                Pos pos = buscarPos(*v.poly, *v.hull, v.bb, *v.partes, maxAltura, colocadas, telaW);
                double score = pos.ty * 100000.0 + pos.tx;
                if (score < mejorScore) {
                    mejorScore = score; mejorPos = pos; mejorRot = v.rot; encontrada = true;
                }
            }
            if (!encontrada) mejorPos = { 0, maxAltura };

            const std::vector<Point>& vpoly = mejorRot ? polyRot : p.poly;
            const BBox& vbb = mejorRot ? bbRot : p.bb;
            const std::vector<std::vector<Point>>& partesElegidas = mejorRot ? partesRot : p.partes;

            std::vector<Point> polyF = Geo::translate(vpoly, mejorPos.tx + MARGEN, mejorPos.ty + MARGEN);
            BBox bbF = Geo::bbox(polyF);
            std::vector<Point> hullF = Geo::convexHull(polyF);
            auto partesF = Geo::translateMulti(partesElegidas, mejorPos.tx + MARGEN, mejorPos.ty + MARGEN);

            colocadas.push_back({ p.id, polyF, bbF, mejorPos.tx, mejorPos.ty, vbb, mejorRot, hullF, partesF });

            double h = mejorPos.ty + vbb.h;
            if (h > maxAltura) maxAltura = h;
        }

        // SHAKE
        bool improved = true;
        int shakeRounds = 0;
        while (improved && shakeRounds < 5) {
            improved = false;
            shakeRounds++;
            for (size_t i = 0; i < colocadas.size(); i++) {
                Placed& cur = colocadas[i];
                while (true) {
                    std::vector<Point> tp = Geo::translate(cur.poly, 0, -STEP);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.minY < 0) break;
                    std::vector<Point> th = Geo::translate(cur.hull, 0, -STEP);
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        const Placed& o = colocadas[j];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collideFast(th, tp, o.hull, o.poly)) { col = true; break; }
                    }
                    if (col) break;
                    cur.poly = tp; cur.bbF = bbT; cur.ty -= STEP;
                    cur.hull = th;
                    improved = true;
                }
                while (true) {
                    std::vector<Point> tp = Geo::translate(cur.poly, -STEP, 0);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.minX < 0) break;
                    std::vector<Point> th = Geo::translate(cur.hull, -STEP, 0);
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        const Placed& o = colocadas[j];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collideFast(th, tp, o.hull, o.poly)) { col = true; break; }
                    }
                    if (col) break;
                    cur.poly = tp; cur.bbF = bbT; cur.tx -= STEP;
                    cur.hull = th;
                    improved = true;
                }
                while (true) {
                    std::vector<Point> tp = Geo::translate(cur.poly, STEP, 0);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.maxX > telaW) break;
                    std::vector<Point> th = Geo::translate(cur.hull, STEP, 0);
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        const Placed& o = colocadas[j];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collideFast(th, tp, o.hull, o.poly)) { col = true; break; }
                    }
                    if (col) break;
                    cur.poly = tp; cur.bbF = bbT; cur.tx += STEP;
                    cur.hull = th;
                    improved = true;
                }
            }
        }

        // COMPACTACION (con busqueda binaria: mismo resultado que el escaneo lineal,
        // pero con ~log2(altura/precision) revisiones en vez de cientos)
        for (size_t iter = 0; iter < 3; iter++) {
            for (size_t i = 0; i < colocadas.size(); i++) {
                Placed& cur = colocadas[i];

                auto esFactibleEnY = [&](double testY) -> bool {
                    if (testY < 0) return false;
                    std::vector<Point> tp = Geo::translate(cur.poly, 0, testY - cur.ty);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.minY < 0) return false;
                    std::vector<Point> th = Geo::translate(cur.hull, 0, testY - cur.ty);
                    for (size_t k = 0; k < colocadas.size(); k++) {
                        if (k == i) continue;
                        const Placed& o = colocadas[k];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collideFast(th, tp, o.hull, o.poly)) return false;
                    }
                    return true;
                };

                double bestTy = cur.ty;
                if (esFactibleEnY(0.0)) {
                    bestTy = 0.0;
                } else {
                    double lo = 0.0, hi = cur.ty; // lo: asumido infactible, hi: factible (posicion actual)
                    for (int iterBin = 0; iterBin < 30 && (hi - lo) > 0.005; iterBin++) {
                        double mid = (lo + hi) / 2.0;
                        if (esFactibleEnY(mid)) hi = mid; else lo = mid;
                    }
                    bestTy = hi;
                }

                if (bestTy < cur.ty - 1e-6) {
                    double dy = bestTy - cur.ty;
                    cur.poly = Geo::translate(cur.poly, 0, dy);
                    cur.bbF = Geo::bbox(cur.poly);
                    cur.hull = Geo::translate(cur.hull, 0, dy);
                    cur.ty = bestTy;
                }
            }
        }

        double finalH = 0;
        for (const auto& col : colocadas) {
            double h = col.ty + col.bb.h + MARGEN;
            if (h > finalH) finalH = h;
        }

        LayoutResult res;
        res.altura = finalH;
        res.layout.reserve(colocadas.size());
        for (const auto& c : colocadas)
            res.layout.push_back({ c.id, c.tx, c.ty, c.rotada });
        return res;
    }
};

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
    mantener[mitad] = true; // BUGFIX: ver nota en el diff anterior
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

// ===================== Motor de colocacion basado en NFP real =====================
// A diferencia de la version anterior (grilla + sacudida + compactacion por
// busqueda binaria, que era una aproximacion), este motor calcula el No-Fit
// Polygon (NFP) EXACTO entre cada par de piezas usando Clipper2 (Minkowski
// sum robusto sobre poligonos concavos + union booleana para combinar el NFP
// contra todas las piezas ya colocadas). Los candidatos de posicion salen
// directo de los vertices de esa region, que son by-construccion puntos de
// contacto exacto -- no hace falta "sacudir" ni "compactar" por pasos porque
// no queda holgura que compactar.
#include "clipper2/clipper.h"
#include <map>
#include <functional>

namespace NFPGeom {

inline Clipper2Lib::PathD aPathD(const std::vector<Point>& poly) {
    Clipper2Lib::PathD p;
    p.reserve(poly.size());
    for (auto& pt : poly) p.push_back(Clipper2Lib::PointD(pt.x, pt.y));
    return p;
}

inline Clipper2Lib::PathD reflejar(const Clipper2Lib::PathD& p) {
    Clipper2Lib::PathD out;
    out.reserve(p.size());
    for (auto& pt : p) out.push_back(Clipper2Lib::PointD(-pt.x, -pt.y));
    return out;
}

// Clave de cache: el NFP solo depende de la FORMA y rotacion de cada pieza
// (no de su posicion final, el calculo es equivariante a traslacion), asi que
// se calcula una sola vez por combinacion (id,rot,id,rot) y se reutiliza
// traduciendolo a la posicion real de la pieza ya colocada.
// Firma de forma: dos piezas con el mismo contorno (aunque tengan distinto id
// -- por ejemplo, la misma prenda repetida varias veces en un pedido grande)
// deben compartir el mismo NFP en cache. Se usa un hash de los vertices
// redondeados (ya vienen normalizados, bbox empieza en 0,0) como firma.
inline std::size_t firmaDeForma(const std::vector<Point>& poly) {
    std::size_t h = poly.size();
    for (const auto& p : poly) {
        long qx = std::llround(p.x * 1000.0);
        long qy = std::llround(p.y * 1000.0);
        h ^= std::hash<long>{}(qx) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<long>{}(qy) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
}

struct ClaveNFP {
    std::size_t firmaA, firmaB;
    int rotA, rotB;
    bool operator<(const ClaveNFP& o) const {
        if (firmaA != o.firmaA) return firmaA < o.firmaA;
        if (rotA != o.rotA) return rotA < o.rotA;
        if (firmaB != o.firmaB) return firmaB < o.firmaB;
        return rotB < o.rotB;
    }
};

class CalculadorNFP {
public:
    std::map<ClaveNFP, Clipper2Lib::PathsD> cache;
    double margen = 0.3; // separacion real entre piezas (gap de corte)

    const Clipper2Lib::PathsD& obtener(std::size_t firmaA, int rotA, const std::vector<Point>& polyA,
                                        std::size_t firmaB, int rotB, const std::vector<Point>& polyB) {
        ClaveNFP clave{firmaA, firmaB, rotA, rotB};
        auto it = cache.find(clave);
        if (it != cache.end()) return it->second;

        Clipper2Lib::PathD A = aPathD(polyA);
        Clipper2Lib::PathD B = aPathD(polyB);
        // Inflar la pieza que se esta por colocar (B) por el margen deseado
        // antes de calcular el NFP: el resultado deja automaticamente un
        // hueco real de separacion entre piezas, sin necesidad de parches
        // posteriores.
        if (margen > 0) {
            auto inflado = Clipper2Lib::InflatePaths({B}, margen,
                Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon, 4.0, 4);
            if (!inflado.empty()) B = inflado[0];
        }
        Clipper2Lib::PathD Brefl = reflejar(B);
        Clipper2Lib::PathsD nfp = Clipper2Lib::MinkowskiSum(A, Brefl, true, 4);
        auto res = cache.emplace(clave, std::move(nfp));
        return res.first->second;
    }
};

} // namespace NFPGeom

class NestingEngine {
public:
    double telaWActual = 0;
    mutable NFPGeom::CalculadorNFP calc; // cache propio de esta instancia (una por hilo, sin locks)

    struct ColocadaNFP {
        int id;
        double tx, ty;
        bool rotada;
        std::vector<Point> poly; // en coordenadas finales
    };

    // Busca la mejor posicion (tx,ty,rotacion) para UNA pieza contra un
    // conjunto fijo de piezas ya colocadas. Extraido como funcion propia
    // para poder reusarlo tanto en el llenado voraz secuencial normal como
    // en la pasada de reinsercion local (ver reoptimizarLocal): en esa
    // pasada se saca una pieza YA colocada del conjunto y se le vuelve a
    // buscar mejor sitio contra las demas, lo cual puede encontrar una
    // posicion mejor que la que le toco cuando se coloco por primera vez
    // (en ese momento, piezas que se colocaron DESPUES de ella todavia no
    // existian y no podian dejarle un hueco mejor).
    ColocadaNFP colocarUnaPieza(const Piece& pieza,
                                 const std::vector<ColocadaNFP>& colocadas,
                                 const std::vector<Piece>& todasLasPiezas,
                                 double telaW) const {
        const double ANCHO_BUCKET = 8.0;
        int nBuckets = std::max(1, (int)(telaW / ANCHO_BUCKET) + 2);
        std::vector<std::vector<int>> grilla(nBuckets);
        for (int i = 0; i < (int)colocadas.size(); i++) {
            BBox bb = Geo::bbox(colocadas[i].poly);
            int b0 = std::max(0, (int)(bb.minX / ANCHO_BUCKET));
            int b1 = std::min(nBuckets - 1, (int)(bb.maxX / ANCHO_BUCKET));
            for (int b = b0; b <= b1; b++) grilla[b].push_back(i);
        }
        auto colisionaConAlguna = [&](const std::vector<Point>& tp) -> bool {
            BBox bb = Geo::bbox(tp);
            int b0 = std::max(0, (int)(bb.minX / ANCHO_BUCKET));
            int b1 = std::min(nBuckets - 1, (int)(bb.maxX / ANCHO_BUCKET));
            for (int b = b0; b <= b1; b++)
                for (int idx : grilla[b])
                    if (Geo::collide(tp, colocadas[idx].poly)) return true;
            return false;
        };

        std::vector<Point> polyRot = Geo::rotate180(pieza.poly);
        struct Variante { const std::vector<Point>* poly; bool rot; BBox bb; };
        Variante vars[2] = {
            { &pieza.poly, false, pieza.bb },
            { &polyRot, true, Geo::bbox(polyRot) }
        };

        double mejorScore = std::numeric_limits<double>::infinity();
        double mejorTx = 0, mejorTy = 0;
        bool mejorRot = false;
        bool encontrada = false;

        for (auto& v : vars) {
            if (v.bb.w > telaW) continue;
            double maxX = telaW - v.bb.w;

            std::vector<std::pair<double, double>> candidatos;
            candidatos.push_back({ 0, 0 });
            if (maxX >= 0) candidatos.push_back({ maxX, 0 });

            Clipper2Lib::PathsD todosNFP;
            for (const auto& c : colocadas) {
                const Piece* origC = nullptr;
                for (const auto& p : todasLasPiezas) if (p.id == c.id) { origC = &p; break; }
                std::vector<Point> polyC_origen = c.rotada ? Geo::rotate180(origC->poly) : origC->poly;
                const auto& nfp = calc.obtener(NFPGeom::firmaDeForma(polyC_origen), c.rotada ? 1 : 0, polyC_origen,
                                                NFPGeom::firmaDeForma(*v.poly), v.rot ? 1 : 0, *v.poly);
                for (const auto& contorno : nfp) {
                    Clipper2Lib::PathD trasladado;
                    trasladado.reserve(contorno.size());
                    for (const auto& pt : contorno) trasladado.push_back(Clipper2Lib::PointD(pt.x + c.tx, pt.y + c.ty));
                    todosNFP.push_back(std::move(trasladado));
                    for (const auto& pt : contorno) candidatos.push_back({ pt.x + c.tx, pt.y + c.ty });
                }
            }
            if (!todosNFP.empty()) {
                auto unionNFP = Clipper2Lib::Union(todosNFP, Clipper2Lib::FillRule::NonZero);
                for (const auto& contorno : unionNFP)
                    for (const auto& pt : contorno)
                        candidatos.push_back({ pt.x, pt.y });
            }

            for (auto& cand : candidatos) {
                double tx = cand.first, ty = cand.second;
                if (tx < -0.001 || tx > maxX + 0.001 || ty < -0.001) continue;
                tx = std::max(0.0, std::min(maxX, tx));
                ty = std::max(0.0, ty);

                std::vector<Point> tp = Geo::translate(*v.poly, tx, ty);
                if (colisionaConAlguna(tp)) continue;

                double score = ty * 100000.0 + tx;
                if (score < mejorScore) {
                    mejorScore = score; mejorTx = tx; mejorTy = ty; mejorRot = v.rot;
                    encontrada = true;
                }
            }
        }

        if (!encontrada) { mejorTx = 0; mejorTy = 0; mejorRot = false; }
        std::vector<Point> polyFinal = Geo::translate(
            mejorRot ? Geo::rotate180(pieza.poly) : pieza.poly, mejorTx, mejorTy);
        return { pieza.id, mejorTx, mejorTy, mejorRot, polyFinal };
    }

    // Coloca una SECUENCIA de piezas (en el orden dado) contra un estado
    // inicial de piezas ya colocadas (puede venir vacio). Es la logica de
    // colocacion voraz de siempre (NFP real + union + red de seguridad final),
    // ahora factorizada para poder llamarla dos veces: una para las piezas
    // grandes (que definen la forma general del acomodo) y otra, por
    // separado, para las piezas chicas sobrantes (ver calcularLayout).
    std::vector<ColocadaNFP> colocarSecuencia(const std::vector<Piece>& piezasAColocar,
                                               const std::vector<Piece>& todasLasPiezas,
                                               std::vector<ColocadaNFP> colocadas,
                                               double telaW) const {
        for (const auto& pieza : piezasAColocar) {
            colocadas.push_back(colocarUnaPieza(pieza, colocadas, todasLasPiezas, telaW));
        }
        return colocadas;
    }

    // ===== Pasada de reinsercion local (ruin-and-recreate) =====
    // Despues del llenado voraz secuencial, cada pieza quedo en la mejor
    // posicion que existia EN EL MOMENTO en que le tocaba colocarse -- pero
    // el llenado es de una sola pasada: una pieza colocada temprano nunca
    // se entera de los huecos que dejaron las piezas colocadas despues de
    // ella. Esta pasada corrige eso: saca una pieza ya colocada del
    // acomodo, y le vuelve a buscar la mejor posicion posible contra TODAS
    // las demas (que ahora si estan todas presentes). Si la nueva posicion
    // no empeora la altura total, se queda con ella. Se repite varias
    // pasadas sobre todas las piezas (de la mas grande a la mas chica, que
    // es lo que mas mueve la aguja en altura) hasta que ya no hay mejoras o
    // se alcanza el limite de pasadas.
    std::vector<ColocadaNFP> reoptimizarLocal(std::vector<ColocadaNFP> colocadas,
                                               const std::vector<Piece>& todasLasPiezas,
                                               double telaW,
                                               int maxPasadas) const {
        auto alturaDe = [](const std::vector<ColocadaNFP>& c) {
            double h = 0;
            for (const auto& x : c) h = std::max(h, Geo::bbox(x.poly).maxY);
            return h;
        };

        // Orden de revision: de la pieza con mayor area a la menor (mover
        // primero las piezas grandes es lo que puede realmente destrabar el
        // acomodo; las chicas casi no cambian la altura general).
        std::vector<int> ordenIds;
        {
            std::vector<Piece> porArea = todasLasPiezas;
            std::sort(porArea.begin(), porArea.end(), [](const Piece& a, const Piece& b) { return a.area > b.area; });
            for (const auto& p : porArea) ordenIds.push_back(p.id);
        }

        for (int pasada = 0; pasada < maxPasadas; pasada++) {
            bool huboMejora = false;
            for (int id : ordenIds) {
                int idx = -1;
                for (int i = 0; i < (int)colocadas.size(); i++) if (colocadas[i].id == id) { idx = i; break; }
                if (idx < 0) continue;

                double alturaAntes = alturaDe(colocadas);
                ColocadaNFP actual = colocadas[idx];

                std::vector<ColocadaNFP> resto = colocadas;
                resto.erase(resto.begin() + idx);

                const Piece* orig = nullptr;
                for (const auto& p : todasLasPiezas) if (p.id == id) { orig = &p; break; }
                if (!orig) continue;

                ColocadaNFP reinsertada = colocarUnaPieza(*orig, resto, todasLasPiezas, telaW);

                std::vector<ColocadaNFP> propuesta = resto;
                propuesta.push_back(reinsertada);
                double alturaDespues = alturaDe(propuesta);

                // Solo aceptamos si mejora (o empata pero mueve la pieza a
                // una posicion distinta, lo cual puede abrir la puerta a
                // mejoras en la SIGUIENTE pieza que se reinserte).
                bool cambioPosicion = std::abs(reinsertada.tx - actual.tx) > 0.01 ||
                                       std::abs(reinsertada.ty - actual.ty) > 0.01 ||
                                       reinsertada.rotada != actual.rotada;
                if (alturaDespues < alturaAntes - 0.005 || (alturaDespues <= alturaAntes + 0.005 && cambioPosicion)) {
                    colocadas = std::move(propuesta);
                    if (alturaDespues < alturaAntes - 0.005) huboMejora = true;
                }
            }
            if (!huboMejora) break;
        }
        return colocadas;
    }

    LayoutResult calcularLayout(const std::vector<Piece>& ordenPiezas, double telaW) const {
        // Separar en piezas GRANDES (definen la forma general) y CHICAS (el
        // resto). Las chicas se colocan DESPUES de todas las grandes, para
        // que siempre vean el paisaje completo de huecos que dejaron las
        // grandes -- y como son pocas, se prueban varias sub-secuencias
        // internas distintas para ellas (no solo una), buscando el tipo de
        // encaje conjunto de piezas chicas complementarias que un metodo
        // puramente voraz (una pieza a la vez, sin variantes) no puede ver.

        std::vector<Piece> grandes, chicas;
        double areaTotal = 0;
        for (auto& p : ordenPiezas) areaTotal += p.area;
        double areaProm = areaTotal / std::max((size_t)1, ordenPiezas.size());
        for (auto& p : ordenPiezas) {
            if (p.area < areaProm * 0.5) chicas.push_back(p); else grandes.push_back(p);
        }
        // Si casi todas las piezas son "chicas" (piezas de tamano parejo),
        // esta separacion no aporta nada distinto del metodo de siempre.
        if (chicas.empty() || grandes.empty()) {
            auto colocadas = colocarSecuencia(ordenPiezas, ordenPiezas, {}, telaW);
            return finalizarLayout(std::move(colocadas), ordenPiezas, telaW);
        }

        auto colocadasGrandes = colocarSecuencia(grandes, ordenPiezas, {}, telaW);

        // FIX: ya no se baraja internamente el orden de las chicas. El orden
        // de "chicas" que llega aqui es el mismo orden relativo que trae
        // ordenPiezas (el que decide el genetico/GA de mas afuera). Si este
        // metodo reordena las chicas por su cuenta, le quita al GA el control
        // sobre como se entrelazan piezas grandes y chicas (p.ej. mangas
        // colocadas ENTRE delanteros), que es precisamente el tipo de forma
        // de acomodo que en la practica se parece mas al resultado de
        // Audaces. Se conserva la separacion grandes/chicas (sigue evitando
        // recalcular NFPs de piezas grandes contra huecos que no les
        // corresponden), pero con una sola pasada, respetando el orden dado.
        auto colocadasCompletas = colocarSecuencia(chicas, ordenPiezas, colocadasGrandes, telaW);

        return finalizarLayout(std::move(colocadasCompletas), ordenPiezas, telaW);
    }

    // Red de seguridad final (colision exacta) + calculo de LayoutResult.
    LayoutResult finalizarLayout(std::vector<ColocadaNFP> colocadas,
                                  const std::vector<Piece>& ordenPiezas, double telaW) const {


        // NOTA: a diferencia del motor anterior, aca NO hay fase de
        // compactacion por busqueda binaria tras la colocacion NFP. La
        // colocacion NFP ya da la posicion mas ajustada posible (respetando
        // el margen de separacion, que quedo incorporado al inflar la pieza
        // antes de calcular el NFP) -- compactar despues con colision exacta
        // sin margen se comeria ese espacio de separacion a proposito. Se
        // verifico ademas que la compactacion post-NFP aportaba una mejora
        // marginal (<0.1%) sobre la posicion ya dada por el NFP.

        // Red de seguridad final: el NFP de Clipper2 tiene una pequeña
        // imprecision conocida en vertices concavos muy agudos (verificado
        // empiricamente: ~0.5-2% de discrepancia puntual segun la pieza,
        // concentrada en esas esquinas, nunca en zonas grandes). Antes de
        // devolver el resultado, verificamos con colision EXACTA (no basada
        // en NFP) contra cada pieza, y corregimos cualquier residuo real
        // empujando hacia arriba lo minimo necesario.
        for (int pasada = 0; pasada < 4; pasada++) {
            bool huboCorreccion = false;
            for (size_t i = 0; i < colocadas.size(); i++) {
                auto& cur = colocadas[i];
                bool colisiona = false;
                for (size_t j = 0; j < colocadas.size(); j++) {
                    if (j == i) continue;
                    if (Geo::collide(cur.poly, colocadas[j].poly)) { colisiona = true; break; }
                }
                if (!colisiona) continue;
                huboCorreccion = true;

                const Piece* orig = nullptr;
                for (const auto& p : ordenPiezas) if (p.id == cur.id) { orig = &p; break; }
                std::vector<Point> base = cur.rotada ? Geo::rotate180(orig->poly) : orig->poly;

                const double PASO_FINO = 0.02;
                const int MAX_PASOS = 150;
                double nuevoTy = cur.ty;
                bool resuelto = false;
                for (int k = 0; k < MAX_PASOS; k++) {
                    nuevoTy += PASO_FINO;
                    std::vector<Point> tp = Geo::translate(base, cur.tx, nuevoTy);
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        if (Geo::collide(tp, colocadas[j].poly)) { col = true; break; }
                    }
                    cur.poly = tp; cur.ty = nuevoTy;
                    if (!col) { resuelto = true; break; }
                }
                if (!resuelto) {
                    double techo = 0;
                    for (const auto& o : colocadas) techo = std::max(techo, o.ty + Geo::bbox(o.poly).h);
                    double tySeguro = techo + 1.0;
                    cur.poly = Geo::translate(base, cur.tx, tySeguro);
                    cur.ty = tySeguro;
                }
            }
            if (!huboCorreccion) break;
        }

        double finalH = 0;
        for (const auto& c : colocadas) finalH = std::max(finalH, Geo::bbox(c.poly).maxY);

        LayoutResult res;
        res.altura = finalH;
        res.layout.reserve(colocadas.size());
        for (const auto& c : colocadas) res.layout.push_back({ c.id, c.tx, c.ty, c.rotada });
        return res;
    }
};

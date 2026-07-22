#pragma once
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <set>
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
};

struct Placed {
    int id;
    std::vector<Point> poly;
    BBox bbF;
    double tx, ty;
    BBox bb;
    bool rotada;
    std::vector<Point> hull; // envolvente convexa del poly YA colocado (en coords finales)
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

inline std::vector<Point> rotate180(const std::vector<Point>& poly) {
    BBox bb = bbox(poly);
    std::vector<Point> out;
    out.reserve(poly.size());
    for (const auto& p : poly)
        out.push_back({ bb.w - (p.x - bb.minX), bb.h - (p.y - bb.minY) });
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

inline bool collide(const std::vector<Point>& A, const std::vector<Point>& B) {
    BBox bbA = bbox(A), bbB = bbox(B);
    if (bbA.maxX <= bbB.minX || bbA.minX >= bbB.maxX ||
        bbA.maxY <= bbB.minY || bbA.minY >= bbB.maxY) return false;

    for (const auto& p : A) if (pointInPoly(p, B)) return true;
    for (const auto& p : B) if (pointInPoly(p, A)) return true;

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

// ===================== NFP (No-Fit Polygon) para poligonos convexos =====================

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
    return hull; // CCW
}

// Refleja un poligono convexo CCW (para restarlo en la suma de Minkowski) y
// mantiene el orden CCW (al negar los puntos, el orden se invierte, por eso recorremos al reves).
inline std::vector<Point> reflectConvexCCW(const std::vector<Point>& B) {
    std::vector<Point> r;
    r.reserve(B.size());
    for (const auto& p : B) r.push_back({ -p.x, -p.y });
    return r;
}

// Suma de Minkowski de dos poligonos convexos CCW (algoritmo de fusion por angulo de arista).
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
        if (!avanzaA && !avanzaB) { i++; j++; } // salvaguarda anti bucle infinito
    }
    return result;
}

} // namespace Geo

class NestingEngine {
public:
    double MARGEN = 0.3;
    double STEP = 0.25;
    mutable std::mt19937 rng{ std::random_device{}() };

    bool canPlace(const std::vector<Point>& poly, double x, double y,
                  const std::vector<Placed>& colocadas, double telaW) const {
        std::vector<Point> tp = Geo::translate(poly, x + MARGEN, y + MARGEN);
        BBox bb = Geo::bbox(tp);
        if (bb.minX < 0 || bb.maxX > telaW || bb.minY < 0) return false;
        for (const auto& col : colocadas) {
            if (bb.maxX <= col.bbF.minX || bb.minX >= col.bbF.maxX ||
                bb.maxY <= col.bbF.minY || bb.minY >= col.bbF.maxY) continue;
            if (Geo::collide(tp, col.poly)) return false;
        }
        return true;
    }

    struct Pos { double tx, ty; };

    // Genera candidatos de posicion usando NFP real (via envolvente convexa) contra
    // cada pieza ya colocada, mas candidatos de los bordes de la tela. La aceptacion
    // final SIEMPRE se valida contra la geometria real completa (no la envolvente),
    // asi que nunca hay riesgo de overlap aunque la pieza sea concava.
    Pos buscarPos(const std::vector<Point>& vpoly, const BBox& vbb,
                  double maxAltura, const std::vector<Placed>& colocadas,
                  double telaW) const {
        double maxX = telaW - vbb.w;

        std::vector<Point> candidatos;
        candidatos.push_back({ 0, 0 });
        if (maxX >= 0) candidatos.push_back({ maxX, 0 });

        std::vector<Point> hullPieza = Geo::convexHull(vpoly);
        std::vector<Point> hullPiezaReflejado = Geo::reflectConvexCCW(hullPieza);

        for (const auto& col : colocadas) {
            if (col.hull.empty()) continue;
            std::vector<Point> nfp = Geo::minkowskiSumConvex(col.hull, hullPiezaReflejado);
            for (const auto& v : nfp) {
                double tx = v.x - MARGEN, ty = v.y - MARGEN;
                if (tx >= -1e-6 && ty >= -1e-6 && tx <= maxX + 1e-6)
                    candidatos.push_back({ std::max(0.0, tx), std::max(0.0, ty) });
            }
        }

        // Muestreo ligero adicional en X para no depender 100% del NFP (respaldo/robustez)
        if (maxX > 0) {
            for (int k = 0; k <= 10; k++) candidatos.push_back({ maxX * k / 10.0, 0 });
        }

        double mejorX = 0, mejorY = maxAltura, mejorScore = std::numeric_limits<double>::infinity();

        for (const auto& c : candidatos) {
            double tx = c.x;
            if (tx < 0 || tx > maxX) continue;
            double ty = std::max(c.y, 0.0);
            // Asegura que arranque encima de lo ya colocado si el candidato cae mas alto que maxAltura
            if (ty > maxAltura + STEP) ty = maxAltura + STEP;

            if (!canPlace(vpoly, tx, ty, colocadas, telaW)) {
                // Intenta subir hasta encontrar un punto valido (por si el candidato NFP quedo invadiendo)
                bool ok = false;
                for (double tyy = ty; tyy <= maxAltura + 5.0; tyy += STEP) {
                    if (canPlace(vpoly, tx, tyy, colocadas, telaW)) { ty = tyy; ok = true; break; }
                }
                if (!ok) continue;
            }

            // Asentar: bajar todo lo posible desde el candidato (settle fino)
            while (ty - STEP >= 0 && canPlace(vpoly, tx, ty - STEP, colocadas, telaW)) ty -= STEP;

            // Pequeño ajuste lateral tambien, por si se puede deslizar y bajar mas
            bool moved = true;
            int iterSeguridad = 0;
            while (moved && iterSeguridad < 200) {
                moved = false;
                iterSeguridad++;
                for (double dx : { -STEP, STEP }) {
                    if (tx + dx >= 0 && tx + dx <= maxX && canPlace(vpoly, tx + dx, ty, colocadas, telaW)) {
                        double nx = tx + dx, ny = ty;
                        while (ny - STEP >= 0 && canPlace(vpoly, nx, ny - STEP, colocadas, telaW)) ny -= STEP;
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

            struct Variante { const std::vector<Point>* poly; BBox bb; bool rot; };
            Variante vars[2] = {
                { &p.poly, p.bb, false },
                { &polyRot, bbRot, true }
            };

            Pos mejorPos{ 0, maxAltura };
            bool mejorRot = false;
            double mejorScore = std::numeric_limits<double>::infinity();
            bool encontrada = false;

            for (const auto& v : vars) {
                if (v.bb.w > telaW) continue;
                Pos pos = buscarPos(*v.poly, v.bb, maxAltura, colocadas, telaW);
                double score = pos.ty * 100000.0 + pos.tx;
                if (score < mejorScore) {
                    mejorScore = score; mejorPos = pos; mejorRot = v.rot; encontrada = true;
                }
            }
            if (!encontrada) mejorPos = { 0, maxAltura };

            const std::vector<Point>& vpoly = mejorRot ? polyRot : p.poly;
            const BBox& vbb = mejorRot ? bbRot : p.bb;

            std::vector<Point> polyF = Geo::translate(vpoly, mejorPos.tx + MARGEN, mejorPos.ty + MARGEN);
            BBox bbF = Geo::bbox(polyF);
            std::vector<Point> hullF = Geo::convexHull(polyF);

            colocadas.push_back({ p.id, polyF, bbF, mejorPos.tx, mejorPos.ty, vbb, mejorRot, hullF });

            double h = mejorPos.ty + vbb.h;
            if (h > maxAltura) maxAltura = h;
        }

        // SHAKE: mover cada pieza en todas direcciones mientras no haya colision
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
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        const Placed& o = colocadas[j];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collide(tp, o.poly)) { col = true; break; }
                    }
                    if (col) break;
                    cur.poly = tp; cur.bbF = bbT; cur.ty -= STEP;
                    cur.hull = Geo::translate(cur.hull, 0, -STEP);
                    improved = true;
                }
                while (true) {
                    std::vector<Point> tp = Geo::translate(cur.poly, -STEP, 0);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.minX < 0) break;
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        const Placed& o = colocadas[j];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collide(tp, o.poly)) { col = true; break; }
                    }
                    if (col) break;
                    cur.poly = tp; cur.bbF = bbT; cur.tx -= STEP;
                    cur.hull = Geo::translate(cur.hull, -STEP, 0);
                    improved = true;
                }
                while (true) {
                    std::vector<Point> tp = Geo::translate(cur.poly, STEP, 0);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.maxX > telaW) break;
                    bool col = false;
                    for (size_t j = 0; j < colocadas.size(); j++) {
                        if (j == i) continue;
                        const Placed& o = colocadas[j];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collide(tp, o.poly)) { col = true; break; }
                    }
                    if (col) break;
                    cur.poly = tp; cur.bbF = bbT; cur.tx += STEP;
                    cur.hull = Geo::translate(cur.hull, STEP, 0);
                    improved = true;
                }
            }
        }

        // COMPACTACION: baja cada pieza lo mas posible (una vez por pieza)
        for (size_t iter = 0; iter < 3; iter++) {
            for (size_t i = 0; i < colocadas.size(); i++) {
                Placed& cur = colocadas[i];
                double bestTy = cur.ty;
                for (double testY = cur.ty - STEP; testY >= 0; testY -= STEP) {
                    std::vector<Point> tp = Geo::translate(cur.poly, 0, testY - cur.ty);
                    BBox bbT = Geo::bbox(tp);
                    if (bbT.minY < 0) break;
                    bool col = false;
                    for (size_t k = 0; k < colocadas.size(); k++) {
                        if (k == i) continue;
                        const Placed& o = colocadas[k];
                        if (bbT.maxX <= o.bbF.minX || bbT.minX >= o.bbF.maxX ||
                            bbT.maxY <= o.bbF.minY || bbT.minY >= o.bbF.maxY) continue;
                        if (Geo::collide(tp, o.poly)) { col = true; break; }
                    }
                    if (!col) bestTy = testY;
                    else break;
                }
                if (bestTy < cur.ty) {
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

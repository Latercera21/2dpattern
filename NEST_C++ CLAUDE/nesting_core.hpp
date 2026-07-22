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

} // namespace Geo

class NestingEngine {
public:
    double MARGEN = 0.3;  // Reduced from 0.5 for tighter packing
    double STEP = 0.25;   // Finer step for better precision
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

    Pos buscarPos(const std::vector<Point>& vpoly, const BBox& vbb,
                  double maxAltura, const std::vector<Placed>& colocadas,
                  double telaW) const {
        std::set<double> xSet;
        xSet.insert(0.0);
        double maxX = telaW - vbb.w;
        if (maxX >= 0) xSet.insert(maxX);
        for (const auto& col : colocadas) {
            double cands[6] = {
                col.tx, col.tx + col.bb.w,
                col.tx - vbb.w, col.tx + col.bb.w - vbb.w,
                col.tx + 0.5, col.tx + col.bb.w - 0.5
            };
            for (double cx : cands)
                if (cx >= 0 && cx <= maxX) xSet.insert(cx);
        }
        if (maxX > 0) {
            static thread_local std::mt19937 rngPos{ std::random_device{}() };
            std::uniform_real_distribution<double> distX(0, maxX);
            for (int k = 0; k < 20; k++) xSet.insert(distX(rngPos));
            for (int k = 0; k <= 20; k++) {
                double gx = maxX * k / 20.0;
                xSet.insert(gx);
            }
        }
        if (maxX >= 0) {
            int nMuestras = 40;
            double paso = maxX / nMuestras;
            if (paso > 0) {
                for (int k = 0; k <= nMuestras; k++) xSet.insert(k * paso);
            }
        }

        double mejorX = 0, mejorY = maxAltura, mejorScore = std::numeric_limits<double>::infinity();

        for (double sx : xSet) {
            if (sx < 0 || sx > maxX) continue;
            double tx = sx, ty = maxAltura + STEP;

            double drop = 5;
            while (ty - drop >= 0 && canPlace(vpoly, tx, ty - drop, colocadas, telaW)) ty -= drop;
            while (ty - STEP >= 0 && canPlace(vpoly, tx, ty - STEP, colocadas, telaW)) ty -= STEP;

            bool moved = true;
            while (moved) {
                moved = false;
                for (double dx : { -STEP, STEP }) {
                    if (canPlace(vpoly, tx + dx, ty, colocadas, telaW)) {
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

            colocadas.push_back({ p.id, polyF, bbF, mejorPos.tx, mejorPos.ty, vbb, mejorRot });

            double h = mejorPos.ty + vbb.h;
            if (h > maxAltura) maxAltura = h;
        }

        // SHAKE: move each piece in all directions while no collision
        bool improved = true;
        int shakeRounds = 0;
        while (improved && shakeRounds < 5) {
            improved = false;
            shakeRounds++;
            for (size_t i = 0; i < colocadas.size(); i++) {
                Placed& cur = colocadas[i];
                // Try moving up
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
                    cur.poly = tp; cur.bbF = bbT; cur.ty -= STEP; improved = true;
                }
                // Try moving left
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
                    cur.poly = tp; cur.bbF = bbT; cur.tx -= STEP; improved = true;
                }
                // Try moving right
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
                    cur.poly = tp; cur.bbF = bbT; cur.tx += STEP; improved = true;
                }
            }
        }

        // COMPACTION: intenta bajar cada pieza lo mas posible (una vez por pieza, no repetido)
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
                    cur.poly = Geo::translate(cur.poly, 0, bestTy - cur.ty);
                    cur.bbF = Geo::bbox(cur.poly);
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

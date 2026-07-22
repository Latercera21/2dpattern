#pragma once
#include "nesting_core.hpp"

// ===================== Curvas y construccion de poligono =====================

struct Segmento {
    // type: "line" -> usa pts[0], pts[1]
    // type: "quad" -> curva bezier cuadratica: pts[0]=p0, pts[1]=control, pts[2]=p1
    bool esLinea;
    Point pts[3];
};

namespace CurveGeo {

inline Point lerp(const Point& a, const Point& b, double t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

inline Point ptQuad(const Point& p0, const Point& cp, const Point& p1, double t) {
    Point a = lerp(p0, cp, t);
    Point b = lerp(cp, p1, t);
    return lerp(a, b, t);
}

// Igual que JS: construirPoligono(segs)
// PUNTOS_POR_CURVA = 8, sin normalizar todavia.
inline std::vector<Point> construirPoligono(const std::vector<Segmento>& segs) {
    std::vector<Point> poly;
    const int PUNTOS_POR_CURVA = 8;
    for (const auto& seg : segs) {
        if (seg.esLinea) {
            poly.push_back(seg.pts[0]);
        } else {
            for (int i = 0; i < PUNTOS_POR_CURVA; i++) {
                double t = (double)i / PUNTOS_POR_CURVA;
                poly.push_back(ptQuad(seg.pts[0], seg.pts[1], seg.pts[2], t));
            }
        }
    }
    return poly;
}

inline double polygonArea(const std::vector<Point>& poly) {
    double area = 0;
    size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        area += poly[i].x * poly[j].y;
        area -= poly[j].x * poly[i].y;
    }
    return std::fabs(area / 2.0);
}

// Normaliza restando minX,minY, y arma el Piece completo (poly + bb + area)
// Equivale a lo que hace cargarJSON despues de construirPoligono en el JS.
inline Piece construirPieza(int id, const std::vector<Segmento>& segs) {
    std::vector<Point> poly = construirPoligono(segs);

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    for (const auto& p : poly) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
    }
    for (auto& p : poly) { p.x -= minX; p.y -= minY; }

    Piece pieza;
    pieza.id = id;
    pieza.poly = poly;
    pieza.bb = Geo::bbox(poly);
    pieza.area = polygonArea(poly);
    return pieza;
}

} // namespace CurveGeo

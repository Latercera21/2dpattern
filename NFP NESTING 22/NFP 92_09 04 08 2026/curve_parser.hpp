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
// PUNTOS_POR_CURVA = 16 (antes 8; se verifico empiricamente que da mejor resultado, similar a la resolucion que usa Audaces internamente), sin normalizar todavia.
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

    // Descomposicion del poligono ORIGINAL (sin simplificar) como referencia segura.
    std::vector<std::vector<Point>> partesOriginal = Geo::descomponerConvexo(poly);

    // Intenta simplificar el contorno (quita puntos casi colineales del sobremuestreo
    // de curvas) para acelerar NFP/colisiones. PERO: si la simplificacion empeora la
    // convexidad (genera MAS partes que el original, por pequeñas irregularidades
    // introducidas al aproximar), se descarta y se usa el original para esa pieza.
    std::vector<Point> polySimplificado = Geo::simplificarPoligono(poly, 0.05);
    std::vector<std::vector<Point>> partesSimplificado = Geo::descomponerConvexo(polySimplificado);

    bool usarSimplificado = partesSimplificado.size() <= partesOriginal.size();

    Piece pieza;
    pieza.id = id;
    if (usarSimplificado) {
        pieza.poly = polySimplificado;
        pieza.partes = partesSimplificado;
    } else {
        pieza.poly = poly;
        pieza.partes = partesOriginal;
    }
    pieza.bb = Geo::bbox(pieza.poly);
    pieza.area = polygonArea(pieza.poly);
    pieza.hull = Geo::convexHull(pieza.poly);
    return pieza;
}

} // namespace CurveGeo

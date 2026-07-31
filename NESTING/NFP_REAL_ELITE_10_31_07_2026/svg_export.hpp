#pragma once
#include "nesting_core.hpp"
#include <fstream>
#include <sstream>

inline std::string layoutASVG(const std::vector<Piece>& piezas, const LayoutResult& res, double telaW) {
    std::ostringstream svg;
    double alturaSvg = res.altura;
    const double franjaTexto = 8.0; // espacio arriba para el texto de medidas, en las mismas unidades (cm)
    double alturaTotal = alturaSvg + franjaTexto;

    // XML declaration for better compatibility
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 -" << franjaTexto << " " << telaW << " " << alturaTotal
        << "\" width=\"" << (telaW * 4) << "\" height=\"" << (alturaTotal * 4) << "\">\n";

    // Texto con la medida final (ancho de tela x altura usada)
    char bufAltura[64];
    snprintf(bufAltura, sizeof(bufAltura), "%.2f", alturaSvg);
    svg << "  <text x=\"1\" y=\"-2\" font-size=\"5\" font-family=\"Arial, sans-serif\" font-weight=\"bold\" fill=\"black\">"
        << "Altura: " << bufAltura << " cm  |  Ancho de tela: " << telaW << " cm</text>\n";

    // Background rectangle
    svg << "  <rect x=\"0\" y=\"0\" width=\"" << telaW << "\" height=\"" << alturaSvg
        << "\" fill=\"none\" stroke=\"black\" stroke-width=\"0.3\"/>\n";

    const char* colores[] = { "#e57373","#64b5f6","#81c784","#ffb74d","#ba68c8","#4db6ac","#f06292","#a1887f" };

    for (size_t i = 0; i < res.layout.size(); i++) {
        const auto& item = res.layout[i];
        const Piece* p = nullptr;
        for (const auto& pz : piezas) if (pz.id == item.id) { p = &pz; break; }
        if (!p) continue;

        std::vector<Point> poly = item.rotada ? Geo::rotate180(p->poly) : p->poly;
        poly = Geo::translate(poly, item.tx, item.ty);

        // Build points string without trailing space
        svg << "  <polygon points=\"";
        for (size_t j = 0; j < poly.size(); j++) {
            svg << poly[j].x << "," << poly[j].y;
            if (j + 1 < poly.size()) svg << " ";
        }
        svg << "\" fill=\"" << colores[i % 8] << "\" fill-opacity=\"0.6\" stroke=\"black\" stroke-width=\"0.2\"/>\n";

        // Better text placement with font-family
        svg << "  <text x=\"" << poly[0].x << "\" y=\"" << poly[0].y
            << "\" font-size=\"2.5\" font-family=\"Arial, sans-serif\" fill=\"black\">" << item.id << "</text>\n";
    }

    svg << "</svg>\n";
    return svg.str();
}

inline void guardarSVG(const std::string& path, const std::vector<Piece>& piezas,
                        const LayoutResult& res, double telaW) {
    std::ofstream out(path);
    out << layoutASVG(piezas, res, telaW);
}

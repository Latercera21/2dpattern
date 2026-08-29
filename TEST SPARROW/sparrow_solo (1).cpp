// Corre SOLO Sparrow (usa el NFP internamente solo como punto de partida,
// no imprime ninguna comparacion) y guarda un SVG con el resultado final.
// Uso: sparrow_solo.exe [telaW] [semilla] [seg_explorar] [seg_comprimir]
#include "json_io.hpp"
#include "sparrow_geo.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

void guardarSVGSparrow(const std::string& path, const std::vector<Sparrow::DatosPieza>& piezas,
                        const std::vector<int>& ids, double telaW, double alturaSvg) {
    std::ostringstream svg;
    const double franjaTexto = 4.5;
    double alturaTotal = alturaSvg + franjaTexto;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 -" << franjaTexto << " " << telaW << " " << alturaTotal
        << "\" width=\"" << (telaW * 4) << "\" height=\"" << (alturaTotal * 4) << "\">\n";
    char buf[64]; snprintf(buf, sizeof(buf), "%.2f", alturaSvg);
    svg << "  <text x=\"1\" y=\"-1\" font-size=\"3\" font-family=\"Arial\" fill=\"#888\">Altura: " << buf
        << " cm | Ancho: " << telaW << " cm (Sparrow)</text>\n";
    svg << "  <rect x=\"0\" y=\"0\" width=\"" << telaW << "\" height=\"" << alturaSvg
        << "\" fill=\"none\" stroke=\"black\" stroke-width=\"0.3\"/>\n";
    const char* colores[] = { "#e57373","#64b5f6","#81c784","#ffb74d","#ba68c8","#4db6ac","#f06292","#a1887f" };
    for (size_t i = 0; i < piezas.size(); i++) {
        const auto& poly = piezas[i].poly;
        svg << "  <polygon points=\"";
        for (size_t j = 0; j < poly.size(); j++) {
            svg << poly[j].x << "," << poly[j].y;
            if (j + 1 < poly.size()) svg << " ";
        }
        svg << "\" fill=\"" << colores[i % 8] << "\" fill-opacity=\"0.6\" stroke=\"black\" stroke-width=\"0.2\"/>\n";
        svg << "  <text x=\"" << poly[0].x << "\" y=\"" << poly[0].y
            << "\" font-size=\"2.5\" font-family=\"Arial\" fill=\"black\">" << ids[i] << "</text>\n";
    }
    svg << "</svg>\n";
    std::ofstream out(path);
    out << svg.str();
}

int main(int argc, char** argv) {
    double telaW = argc > 1 ? std::atof(argv[1]) : 160.0;
    unsigned semilla = argc > 2 ? (unsigned)std::atoi(argv[2]) : 42u;
    double tExplorar = argc > 3 ? std::atof(argv[3]) : 25.0;
    double tComprimir = argc > 4 ? std::atof(argv[4]) : 10.0;

    std::vector<Piece> piezas = cargarPiezasDesdeArchivo("figuras.json");
    std::vector<Piece> ordenArea = piezas;
    std::sort(ordenArea.begin(), ordenArea.end(), [](const Piece& a, const Piece& b){ return a.area > b.area; });
    NestingEngine engine;
    LayoutResult rNFP = engine.calcularLayout(ordenArea, telaW);

    std::vector<Sparrow::PiezaRotable> variantes;
    std::vector<Sparrow::DatosPieza> piezasSparrow;
    std::vector<int> ids;
    for (auto& li : rNFP.layout) {
        const Piece* orig = nullptr;
        for (auto& p : ordenArea) if (p.id == li.id) { orig = &p; break; }
        variantes.push_back(Sparrow::prepararRotable(orig->poly));
        std::vector<Point> polyColocado = li.rotada ? Geo::rotate180(orig->poly) : orig->poly;
        polyColocado = Geo::translate(polyColocado, li.tx, li.ty);
        Sparrow::DatosPieza d;
        d.poly = polyColocado;
        d.polyMargen = Sparrow::inflarPoly(polyColocado, Sparrow::MARGEN_SEPARACION / 2.0);
        d.hull = Geo::convexHull(polyColocado);
        d.polos = Sparrow::generarPolos(polyColocado, 8);
        d.diametro = Sparrow::diametroForma(d.hull);
        d.lambda = Sparrow::penalizacionForma(d.hull);
        piezasSparrow.push_back(d);
        ids.push_back(li.id);
    }

    std::mt19937 rng(semilla);
    auto resultado = Sparrow::resolverISPP(piezasSparrow, &variantes, telaW, rNFP.altura, rng, tExplorar, tComprimir);
    double perdida = Sparrow::perdidaTotal(resultado.piezas);

    printf("Altura final: %.2f cm | telaW=%.1f | factible=%s\n",
           resultado.largo, telaW, perdida<=1e-6 ? "SI" : "NO -- resultado invalido, no usar");

    if (perdida <= 1e-6) {
        guardarSVGSparrow("resultado_sparrow.svg", resultado.piezas, ids, telaW, resultado.largo);
        printf("SVG guardado en: resultado_sparrow.svg\n");
    }
    return 0;
}

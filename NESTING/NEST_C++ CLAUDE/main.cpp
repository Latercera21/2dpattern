#include "json_io.hpp"
#include "genetic.hpp"
#include "svg_export.hpp"
#include <cstdio>
#include <fstream>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    std::string entrada = "/storage/emulated/0/Download/figuras.json";
    std::string salida   = "/storage/emulated/0/Download/resultado.json";
    double telaW = 100;
    int iteraciones = 300;

    if (argc > 1) entrada = argv[1];
    if (argc > 2) salida = argv[2];
    if (argc > 3) telaW = std::stod(argv[3]);
    if (argc > 4) iteraciones = std::stoi(argv[4]);

    std::vector<Piece> piezas;
    try {
        piezas = cargarPiezasDesdeArchivo(entrada);
    } catch (const std::exception& e) {
        printf("Error leyendo '%s': %s\n", entrada.c_str(), e.what());
        return 1;
    }

    if (piezas.empty()) {
        printf("No se cargaron piezas validas desde '%s'.\n", entrada.c_str());
        return 1;
    }

    printf("Piezas cargadas: %zu | telaW=%.2f | iteraciones=%d\n", piezas.size(), telaW, iteraciones);

    std::vector<Piece> ordenArea = piezas, ordenAltura = piezas, ordenAncho = piezas;
    std::sort(ordenArea.begin(), ordenArea.end(), [](const Piece& a, const Piece& b) { return a.area > b.area; });
    std::sort(ordenAltura.begin(), ordenAltura.end(), [](const Piece& a, const Piece& b) { return a.bb.h > b.bb.h; });
    std::sort(ordenAncho.begin(), ordenAncho.end(), [](const Piece& a, const Piece& b) { return a.bb.w > b.bb.w; });

    NestingEngine engineInicial;
    LayoutResult rArea = engineInicial.calcularLayout(ordenArea, telaW);
    LayoutResult rAltura = engineInicial.calcularLayout(ordenAltura, telaW);
    LayoutResult rAncho = engineInicial.calcularLayout(ordenAncho, telaW);
    printf("Semillas iniciales -> area:%.2f altura:%.2f ancho:%.2f\n", rArea.altura, rAltura.altura, rAncho.altura);

    std::vector<Piece> piezasOrdenadas = ordenArea;
    if (rAltura.altura < rArea.altura && rAltura.altura <= rAncho.altura) piezasOrdenadas = ordenAltura;
    else if (rAncho.altura < rArea.altura && rAncho.altura < rAltura.altura) piezasOrdenadas = ordenAncho;

    auto t0 = std::chrono::steady_clock::now();

    unsigned nHilos = std::thread::hardware_concurrency();
    if (nHilos == 0) nHilos = 2;
    if (nHilos > 8) nHilos = 8; // no tiene sentido pasar de 8 arranques independientes
    printf("Corriendo %u arranques independientes en paralelo...\n", nHilos);

    std::vector<LayoutResult> resultados(nHilos);
    std::vector<std::vector<Piece>> ordenes(nHilos);
    SharedPool poolCompartido;
    {
        std::vector<std::thread> pool;
        for (unsigned h = 0; h < nHilos; h++) {
            pool.emplace_back([&, h]() {
                GeneticNester ga; // cada hilo tiene su propio rng (semilla distinta por hilo)
                resultados[h] = ga.optimizar(piezasOrdenadas, telaW, iteraciones, &ordenes[h], (int)h, &poolCompartido);
            });
        }
        for (auto& t : pool) t.join();
    }

    int mejorIdx = 0;
    for (unsigned h = 1; h < nHilos; h++)
        if (resultados[h].altura < resultados[mejorIdx].altura) mejorIdx = h;

    LayoutResult res = resultados[mejorIdx];
    std::vector<Piece> mejorOrden = ordenes[mejorIdx];

    auto t1 = std::chrono::steady_clock::now();
    double segs = std::chrono::duration<double>(t1 - t0).count();

    printf("Alturas por arranque: ");
    for (unsigned h = 0; h < nHilos; h++) printf("%.2f ", resultados[h].altura);
    printf("\n");
    printf("Altura final (mejor de %u arranques): %.2f | tiempo: %.2fs\n", nHilos, res.altura, segs);

    std::string json = layoutAJSON(res);
    std::ofstream out(salida);
    if (!out) {
        printf("No se pudo escribir en '%s'\n", salida.c_str());
        return 1;
    }
    out << json;
    out.close();
    printf("Resultado guardado en: %s\n", salida.c_str());

    std::string svgPath = salida;
    size_t dot = svgPath.rfind('.');
    if (dot != std::string::npos) svgPath = svgPath.substr(0, dot);
    svgPath += ".svg";
    guardarSVG(svgPath, piezas, res, telaW);
    printf("SVG guardado en: %s\n", svgPath.c_str());

    return 0;
}

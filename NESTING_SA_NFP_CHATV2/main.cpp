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

    printf("Diagnostico de descomposicion convexa (partes por pieza):\n");
    for (const auto& p : piezas) {
        printf("  Pieza id=%d: %zu vertices -> %zu partes convexas\n",
               p.id, p.poly.size(), p.partes.size());
    }
    fflush(stdout);

    std::vector<Piece> ordenArea = piezas, ordenAltura = piezas, ordenAncho = piezas;
    std::sort(ordenArea.begin(), ordenArea.end(), [](const Piece& a, const Piece& b) { return a.area > b.area; });
    std::sort(ordenAltura.begin(), ordenAltura.end(), [](const Piece& a, const Piece& b) { return a.bb.h > b.bb.h; });
    std::sort(ordenAncho.begin(), ordenAncho.end(), [](const Piece& a, const Piece& b) { return a.bb.w > b.bb.w; });

    // Orden "piramide": la pieza mas grande al CENTRO de la secuencia, y las
    // siguientes (de mayor a menor area) se reparten alternando hacia afuera
    // (centro-1, centro+1, centro-2, centro+2, ...). Esto imita el patron real
    // observado en acomodos de referencia (Audaces y corridas anteriores que
    // se le parecian): la pieza mas grande (la espalda) termina en el MEDIO
    // del acomodo, flanqueada simetricamente, en vez de pegada a un borde
    // como hace el greedy puro cuando arranca de un orden por-area simple.
    std::vector<Piece> ordenPiramide = ordenArea; // ya viene de mayor a menor area
    {
        int n = (int)ordenPiramide.size();
        std::vector<Piece> piramide(n);
        int centro = n / 2, lo = centro, hi = centro;
        bool haciaIzq = true;
        piramide[centro] = ordenPiramide[0];
        for (int k = 1; k < n; k++) {
            if (haciaIzq) { lo--; if (lo < 0) { hi++; piramide[hi] = ordenPiramide[k]; } else piramide[lo] = ordenPiramide[k]; }
            else { hi++; if (hi >= n) { lo--; piramide[lo] = ordenPiramide[k]; } else piramide[hi] = ordenPiramide[k]; }
            haciaIzq = !haciaIzq;
        }
        ordenPiramide = std::move(piramide);
    }

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

    // Diversifica el punto de partida de cada hilo. Con el motor NFP (preciso y
    // determinista), si todos los hilos arrancan del MISMO orden de piezas suelen
    // converger al mismo resultado (el ruido del muestreo viejo ya no aporta esa
    // diversidad). Por eso: unos hilos parten de los ordenes ya calculados
    // (area/altura/ancho/mejor), y el resto de mezclas aleatorias distintas.
    std::vector<std::vector<Piece>> ordenesIniciales(nHilos);
    std::vector<std::vector<Piece>> semillasBase = { piezasOrdenadas, ordenArea, ordenAltura, ordenAncho, ordenPiramide };
    std::mt19937 rngPrincipal{ std::random_device{}() };
    for (unsigned h = 0; h < nHilos; h++) {
        if (h < semillasBase.size()) {
            ordenesIniciales[h] = semillasBase[h];
        } else {
            std::vector<Piece> mezcla = piezas;
            std::shuffle(mezcla.begin(), mezcla.end(), rngPrincipal);
            ordenesIniciales[h] = mezcla;
        }
    }

    {
        std::vector<std::thread> pool;
        for (unsigned h = 0; h < nHilos; h++) {
            pool.emplace_back([&, h]() {
                GeneticNester ga; // cada hilo tiene su propio rng (semilla distinta por hilo)
                // La mitad de los hilos (pares) se mantiene 100% independiente para
                // preservar diversidad real; la otra mitad (impares) participa del
                // pool compartido para aprovechar ocasionalmente el mejor global,
                // sin que TODOS los hilos terminen homogenizandose entre si.
                SharedPool* poolParaEsteHilo = (h % 2 == 1) ? &poolCompartido : nullptr;
                resultados[h] = ga.optimizar(ordenesIniciales[h], telaW, iteraciones, &ordenes[h], (int)h, poolParaEsteHilo);
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

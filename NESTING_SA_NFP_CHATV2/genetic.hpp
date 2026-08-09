#pragma once
#include "nesting_core.hpp"
#include "joint_sa.hpp"
#include <random>
#include <algorithm>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>
#include <cstdio>

// Se pone en true desde un manejador de Ctrl+C (SIGINT) en main.cpp. Todos los
// hilos revisan esta bandera dentro de su bucle de iteraciones y cortan
// apenas la ven, para que Ctrl+C guarde el mejor resultado encontrado hasta
// ese momento en vez de matar el proceso a la fuerza sin guardar nada.
inline std::atomic<bool> g_detenerOptimizacion{false};

struct EliteItem {
    std::vector<Piece> orden;
    double altura;
};

// Mutex global para que los prints de progreso de varios hilos no se mezclen
inline std::mutex g_printMutex;

// Pool compartido entre hilos: permite migrar buenas soluciones y detectar
// cuando TODOS los hilos combinados ya no encuentran mejoras (para cortar antes).
struct SharedPool {
    std::mutex mtx;
    std::vector<EliteItem> eliteGlobal;
    double mejorGlobalAltura = std::numeric_limits<double>::infinity();
    int chequeosSinMejora = 0;
    int GLOBAL_ELITE_SIZE = 10;
    int UMBRAL_PARADA = 40; // chequeos combinados sin mejora antes de cortar (subido: iteraciones ahora mas baratas, vale la pena mas paciencia)

    void insertar(const std::vector<Piece>& orden, double altura) {
        eliteGlobal.push_back({ orden, altura });
        std::sort(eliteGlobal.begin(), eliteGlobal.end(), [](const EliteItem& a, const EliteItem& b) {
            return a.altura < b.altura;
        });
        if ((int)eliteGlobal.size() > GLOBAL_ELITE_SIZE) eliteGlobal.resize(GLOBAL_ELITE_SIZE);
    }
};

class GeneticNester {
public:
    int ELITE_SIZE = 10;
    std::mt19937 rng{ std::random_device{}() };

    std::vector<double> pesos = { 0.19, 0.19, 0.13, 0.15, 0.19, 0.15 };
    std::vector<int> successCounts = { 0, 0, 0, 0, 0, 0 };
    bool ultimaExito = false;
    int ultimoTipo = -1;

    double randD() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    int randInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

    int elegirTipoMutacion() {
        if (ultimaExito && ultimoTipo >= 0 && randD() < 0.4) return ultimoTipo;
        double total = 0;
        for (double p : pesos) total += p;
        double r = randD() * total, acum = 0;
        for (int i = 0; i < (int)pesos.size(); i++) {
            acum += pesos[i];
            if (r <= acum) return i;
        }
        return (int)pesos.size() - 1;
    }

    std::vector<Piece> mutar(const std::vector<Piece>& base) {
        std::vector<Piece> o = base;
        int n = (int)o.size();
        if (n < 2) return o;
        int tipo = elegirTipoMutacion();

        switch (tipo) {
        case 0: { // swap
            int i, j;
            if (ultimaExito && randD() < 0.5) {
                i = randInt(0, n - 2); j = i + 1;
            } else {
                i = randInt(0, n - 1); j = randInt(0, n - 1);
            }
            std::swap(o[i], o[j]);
            break;
        }
        case 1: { // move
            int i = randInt(0, n - 1);
            int j;
            if (ultimaExito) {
                int delta = randInt(-2, 2);
                j = std::clamp(i + delta, 0, n - 1);
            } else {
                j = randInt(0, n - 1);
            }
            Piece p = o[i];
            o.erase(o.begin() + i);
            o.insert(o.begin() + j, p);
            break;
        }
        case 2: { // smallFront: menor area en percentil 30-70%, mover al frente
            int lo = n * 3 / 10, hi = n * 7 / 10;
            if (hi <= lo) hi = lo + 1;
            hi = std::min(hi, n);
            int idxMenor = lo;
            for (int k = lo; k < hi; k++)
                if (o[k].area < o[idxMenor].area) idxMenor = k;
            Piece p = o[idxMenor];
            o.erase(o.begin() + idxMenor);
            o.insert(o.begin(), p);
            break;
        }
        case 3: { // bigMid: mayor area entre primeras 5, mover a posicion 1/3..85%
            int lim = std::min(5, n);
            int idxMayor = 0;
            for (int k = 0; k < lim; k++)
                if (o[k].area > o[idxMayor].area) idxMayor = k;
            int destLo = n / 3, destHi = (int)(n * 0.85);
            if (destHi <= destLo) destHi = destLo + 1;
            destHi = std::min(destHi, n - 1);
            int dest = randInt(destLo, destHi);
            Piece p = o[idxMayor];
            o.erase(o.begin() + idxMayor);
            o.insert(o.begin() + dest, p);
            break;
        }
        case 4: { // shuffleSeg: segmento 2-5 (menor si exito), Fisher-Yates parcial
            int maxLen = ultimaExito ? 3 : 5;
            int len = randInt(2, std::min(maxLen, n));
            int start = randInt(0, n - len);
            for (int k = len - 1; k > 0; k--) {
                int j = randInt(0, k);
                std::swap(o[start + k], o[start + j]);
            }
            break;
        }
        case 5: { // reverseSeg: invierte un tramo contiguo (tipo 2-opt).
            // Complementa a los anteriores: swap/move/shuffle reubican piezas
            // puntuales, pero ninguno invierte el ORDEN RELATIVO de un tramo
            // completo, que es un movimiento clasico y efectivo en problemas
            // de permutacion (TSP-like) para escapar de optimos locales donde
            // el orden interno de un grupo de piezas es lo que esta mal, no
            // solo su posicion individual.
            int len = randInt(2, std::min(6, n));
            int start = randInt(0, n - len);
            std::reverse(o.begin() + start, o.begin() + start + len);
            break;
        }
        }
        ultimoTipo = tipo;
        return o;
    }

    std::vector<Piece> cruzar(const std::vector<Piece>& o1, const std::vector<Piece>& o2) {
        int n = (int)o1.size();
        std::vector<Piece> result(n);
        std::vector<bool> usado(n, false);
        std::vector<bool> slotLleno(n, false);

        for (int i = 0; i < n; i++) {
            if (o1[i].id == o2[i].id) {
                result[i] = o1[i];
                slotLleno[i] = true;
                usado[o1[i].id] = true;
            }
        }

        auto buscarSiguiente = [&](const std::vector<Piece>& fuente, int desde) -> int {
            for (int k = desde; k < (int)fuente.size(); k++)
                if (!usado[fuente[k].id]) return k;
            return -1;
        };

        int p1 = 0, p2 = 0;
        bool turnoO1 = true;
        for (int i = 0; i < n; i++) {
            if (slotLleno[i]) continue;
            int idx = turnoO1 ? buscarSiguiente(o1, p1) : buscarSiguiente(o2, p2);
            const std::vector<Piece>* fuente = turnoO1 ? &o1 : &o2;
            if (idx == -1) {
                fuente = turnoO1 ? &o2 : &o1;
                idx = buscarSiguiente(*fuente, 0);
            }
            result[i] = (*fuente)[idx];
            usado[result[i].id] = true;
            if (turnoO1) p1 = idx + 1; else p2 = idx + 1;
            turnoO1 = !turnoO1;
        }
        return result;
    }

    void registrarExito(int tipo, bool exito) {
        ultimaExito = exito;
        if (exito && tipo >= 0) {
            successCounts[tipo]++;
            int total = 0;
            for (int c : successCounts) total += c;
            if (total >= 20) {
                int mejor = 0;
                for (int i = 1; i < (int)successCounts.size(); i++)
                    if (successCounts[i] > successCounts[mejor]) mejor = i;
                pesos[mejor] = std::min(0.5, pesos[mejor] + 0.05);
                double suma = 0;
                for (double p : pesos) suma += p;
                for (double& p : pesos) p /= suma;
                std::fill(successCounts.begin(), successCounts.end(), 0);
            }
        }
    }

    void insertarElite(std::vector<EliteItem>& elite, const std::vector<Piece>& orden, double altura) {
        elite.push_back({ orden, altura });
        std::sort(elite.begin(), elite.end(), [](const EliteItem& a, const EliteItem& b) {
            return a.altura < b.altura;
        });
        if ((int)elite.size() > ELITE_SIZE) elite.resize(ELITE_SIZE);
    }

    // ===================== Bucle principal =====================
    LayoutResult optimizar(std::vector<Piece> ordenInicial, double telaW, int iteraciones,
                            std::vector<Piece>* mejorOrdenOut = nullptr, int threadId = -1,
                            SharedPool* pool = nullptr, int iteracionesSA = 8000000) {
        NestingEngine engine;
        int RESTART = std::max(15, std::min(50, (int)(iteraciones * 0.12)));

        auto t0 = std::chrono::steady_clock::now();

        LayoutResult mejorResultado = engine.calcularLayout(ordenInicial, telaW);
        std::vector<Piece> mejorOrden = ordenInicial;
        std::vector<EliteItem> elite;
        elite.push_back({ ordenInicial, mejorResultado.altura });

        std::vector<Piece> ordenActual = ordenInicial;
        LayoutResult resultadoActual = mejorResultado;
        int sinMejora = 0;
        int vecesEstancado = 0;

        {
            std::lock_guard<std::mutex> lock(g_printMutex);
            printf("[Hilo %d] iniciado | altura semilla: %.2f\n", threadId, mejorResultado.altura);
        }

        // Reporte de progreso: por iteraciones (cada ~5%) O cada 3 segundos, lo que ocurra primero
        int pasoReporte = std::max(1, iteraciones / 20);
        auto ultimoReporte = t0;

        // Pulido SA liviano: se dispara cada vez que el genetico encuentra un
        // nuevo mejor resultado (con un limite de frecuencia para no gastar
        // todo el tiempo en esto). A diferencia del pulido pesado del final
        // (millones de iteraciones), este es barato (unos cientos de miles)
        // -- la idea es que el SA vaya afinando el MISMO acomodo que el
        // genetico va mejorando, en vez de enterarse recien al final. Si
        // encuentra algo mejor, ese resultado (altura) pasa a ser el nuevo
        // mejor; el orden que usa el genetico para sus mutaciones/cruces
        // sigue siendo el que ya tenia (el SA cambia posiciones, no orden).
        int iteracionesGAdesdeUltimoSA = 999999;
        auto pulirConSALivianamente = [&](std::vector<Piece>& orden, LayoutResult& resultado) {
            iteracionesGAdesdeUltimoSA++;
            if (iteracionesGAdesdeUltimoSA < 12) return; // no mas de 1 vez cada 12 iteraciones del GA
            iteracionesGAdesdeUltimoSA = 0;

            std::vector<NestingEngine::ColocadaNFP> colocadas;
            for (const auto& li : resultado.layout) {
                const Piece* orig = nullptr;
                for (const auto& p : orden) if (p.id == li.id) { orig = &p; break; }
                if (!orig) continue;
                std::vector<Point> poly = Geo::translate(
                    li.rotada ? Geo::rotate180(orig->poly) : orig->poly, li.tx, li.ty);
                colocadas.push_back({ li.id, li.tx, li.ty, li.rotada, poly });
            }
            JointSAOptimizer saLigero;
            std::mt19937 rngLigero{ std::random_device{}() };
            LayoutResult rSA = saLigero.optimizar(orden, telaW, colocadas, 400000, rngLigero, false, 0.15);
            if (rSA.altura < resultado.altura - 0.01) resultado = rSA;
        };

        for (int i = 0; i < iteraciones; i++) {
            if (g_detenerOptimizacion.load(std::memory_order_relaxed)) {
                std::lock_guard<std::mutex> lock(g_printMutex);
                printf("[Hilo %d] interrumpido por el usuario en iteracion %d/%d, guardando mejor resultado hasta el momento\n",
                       threadId, i, iteraciones);
                break;
            }
            int nCandidatos = 10;
            std::vector<std::vector<Piece>> candidatos(nCandidatos);
            std::vector<int> tipos(nCandidatos, -1);
            std::vector<LayoutResult> resultados(nCandidatos);

            for (int c = 0; c < nCandidatos; c++) {
                if ((c == 0 || c == 2) && elite.size() > 1) {
                    int a = randInt(0, (int)elite.size() - 1);
                    int b = randInt(0, (int)elite.size() - 1);
                    candidatos[c] = cruzar(elite[a].orden, elite[b].orden);
                } else if (c == 1 && !elite.empty() && randD() < 0.3) {
                    int e = randInt(0, (int)elite.size() - 1);
                    tipos[c] = elegirTipoMutacion();
                    candidatos[c] = mutar(elite[e].orden);
                } else {
                    tipos[c] = elegirTipoMutacion();
                    candidatos[c] = mutar(ordenActual);
                }
                resultados[c] = engine.calcularLayout(candidatos[c], telaW);
            }

            double mejorAlturaCand = std::numeric_limits<double>::infinity();
            std::vector<Piece> mejorCandidato;
            LayoutResult mejorResCand;
            int tipoUsado = -1;
            for (int c = 0; c < nCandidatos; c++) {
                if (resultados[c].altura < mejorAlturaCand) {
                    mejorAlturaCand = resultados[c].altura;
                    mejorCandidato = candidatos[c];
                    mejorResCand = resultados[c];
                    tipoUsado = tipos[c];
                }
            }

            double diff = mejorResCand.altura - resultadoActual.altura;
            double temp = 1.0 - (double)i / iteraciones;
            bool acepta = diff < 0 || randD() < std::exp(-diff / (temp * 2 + 0.15));

            bool exito = false;
            if (acepta) {
                ordenActual = mejorCandidato;
                resultadoActual = mejorResCand;
                exito = diff < 0;
            }
            registrarExito(tipoUsado, exito);

            if (resultadoActual.altura < mejorResultado.altura - 0.05) {
                mejorResultado = resultadoActual;
                mejorOrden = ordenActual;
                sinMejora = 0;
                insertarElite(elite, ordenActual, resultadoActual.altura);
                pulirConSALivianamente(mejorOrden, mejorResultado);
            } else {
                sinMejora++;
            }

            if (sinMejora >= RESTART) {
                sinMejora = 0;
                double r = randD();
                if (r < 0.25) {
                    // Reinicio totalmente fresco: baraja al azar, sin relacion
                    // con el optimo actual. Es la unica forma de escapar de un
                    // optimo local en el que el elite y las mutaciones pequenas
                    // (que solo exploran cerca de lo ya conocido) quedan
                    // atrapados durante muchas iteraciones seguidas. Como cada
                    // evaluacion ahora es barata (motor NFP), probamos varios
                    // barajados y nos quedamos con el mejor punto de partida,
                    // en vez de aceptar el primero que salga a ciegas.
                    double mejorBarajado = std::numeric_limits<double>::infinity();
                    std::vector<Piece> candidatoBarajado;
                    for (int intento = 0; intento < 4; intento++) {
                        std::vector<Piece> prueba = ordenInicial;
                        std::shuffle(prueba.begin(), prueba.end(), rng);
                        double h = engine.calcularLayout(prueba, telaW).altura;
                        if (h < mejorBarajado) { mejorBarajado = h; candidatoBarajado = prueba; }
                    }
                    ordenActual = candidatoBarajado;
                } else if (r < 0.25 + 0.45 && !elite.empty()) {
                    int top = std::min(3, (int)elite.size());
                    int e = randInt(0, top - 1);
                    ordenActual = elite[e].orden;
                } else {
                    ordenActual = mejorOrden;
                    int nMut = randInt(2, 3);
                    for (int k = 0; k < nMut; k++) ordenActual = mutar(ordenActual);
                }
                resultadoActual = engine.calcularLayout(ordenActual, telaW);
                if (resultadoActual.altura < mejorResultado.altura - 0.05) {
                    mejorResultado = resultadoActual;
                    mejorOrden = ordenActual;
                    insertarElite(elite, ordenActual, resultadoActual.altura);
                    pulirConSALivianamente(mejorOrden, mejorResultado);
                }
            }

            // ===== Migracion entre hilos: cada cierto numero de iteraciones =====
            bool debeParar = false;
            bool intentarRefinarGlobal = false;
            std::vector<Piece> ordenGlobalParaRefinar;
            int pasoMigra = std::max(5, iteraciones / 20);
            if (pool && (i + 1) % pasoMigra == 0) {
                std::lock_guard<std::mutex> lock(pool->mtx);
                if (mejorResultado.altura < pool->mejorGlobalAltura - 0.01) {
                    pool->mejorGlobalAltura = mejorResultado.altura;
                    pool->insertar(mejorOrden, mejorResultado.altura);
                    pool->chequeosSinMejora = 0;
                } else {
                    pool->chequeosSinMejora++;
                    // Enriquece el material genetico local (para cruces/mutaciones).
                    // Ademas, con cierta probabilidad, se marca para empujar la
                    // busqueda propia de ESTE hilo hacia el mejor global (mutandolo,
                    // no copiandolo tal cual) -- le da a este hilo una oportunidad
                    // real de refinar esa solucion con su propio camino de busqueda,
                    // sin abandonar del todo su propia trayectoria. La evaluacion
                    // (cara) se hace DESPUES de soltar el candado, para no bloquear
                    // a los demas hilos mientras tanto.
                    if (!pool->eliteGlobal.empty() && pool->eliteGlobal[0].altura < mejorResultado.altura - 0.01) {
                        insertarElite(elite, pool->eliteGlobal[0].orden, pool->eliteGlobal[0].altura);
                        if (randD() < 0.33) {
                            intentarRefinarGlobal = true;
                            ordenGlobalParaRefinar = pool->eliteGlobal[0].orden;
                        }
                    }
                }
                if (pool->chequeosSinMejora >= pool->UMBRAL_PARADA) debeParar = true;
            }
            if (intentarRefinarGlobal) {
                ordenActual = mutar(ordenGlobalParaRefinar);
                resultadoActual = engine.calcularLayout(ordenActual, telaW);
            }
            if (debeParar) {
                std::lock_guard<std::mutex> lock(g_printMutex);
                printf("[Hilo %d] corte anticipado en iteracion %d/%d (todos los hilos convergieron sin mejora)\n",
                       threadId, i + 1, iteraciones);
                break;
            }

            if ((i + 1) % pasoReporte == 0 || i == iteraciones - 1 ||
                std::chrono::duration<double>(std::chrono::steady_clock::now() - ultimoReporte).count() >= 3.0) {
                auto ahora = std::chrono::steady_clock::now();
                double transcurrido = std::chrono::duration<double>(ahora - t0).count();
                int pct = (int)(100.0 * (i + 1) / iteraciones);
                {
                    std::lock_guard<std::mutex> lock(g_printMutex);
                    printf("[Hilo %d] %3d%% (%d/%d) | mejor altura: %.2f | %.1fs transcurridos\n",
                           threadId, pct, i + 1, iteraciones, mejorResultado.altura, transcurrido);
                }
                ultimoReporte = ahora;
            }
        }

        // ===== Pulido final: 2-opt sobre pares adyacentes =====
        // Barato (O(n) calcularLayout extra) y a veces rescata mejoras
        // que el azar del GA no encontro.
        {
            bool mejorado = true;
            int vueltas = 0;
            while (mejorado && vueltas < 3) {
                mejorado = false;
                vueltas++;
                for (int i = 0; i + 1 < (int)mejorOrden.size(); i++) {
                    std::vector<Piece> prueba = mejorOrden;
                    std::swap(prueba[i], prueba[i + 1]);
                    LayoutResult r = engine.calcularLayout(prueba, telaW);
                    if (r.altura < mejorResultado.altura - 0.01) {
                        mejorResultado = r;
                        mejorOrden = prueba;
                        mejorado = true;
                    }
                }
            }
        }

        // ===== Pulido final 2: reinsercion local (ruin-and-recreate) =====
        // Mas caro que el 2-opt de arriba (varias pasadas sobre TODAS las
        // piezas, cada una recalculando NFP contra el resto), por eso se
        // hace UNA sola vez aqui al final y no durante la busqueda del GA.
        // A diferencia del reordenamiento (que solo cambia en que SECUENCIA
        // se colocan las piezas), esto opera directo sobre el acomodo ya
        // armado: saca una pieza y la reinserta en la mejor posicion posible
        // contra TODAS las demas ya colocadas, lo cual puede mover una
        // pieza grande de un borde hacia un hueco intermedio si eso reduce
        // la altura -- algo que el llenado voraz de una sola pasada nunca
        // puede corregir por si solo.
        {
            LayoutResult base = engine.calcularLayout(mejorOrden, telaW);
            std::vector<NestingEngine::ColocadaNFP> colocadas;
            colocadas.reserve(base.layout.size());
            for (const auto& li : base.layout) {
                const Piece* orig = nullptr;
                for (const auto& p : mejorOrden) if (p.id == li.id) { orig = &p; break; }
                if (!orig) continue;
                std::vector<Point> poly = Geo::translate(
                    li.rotada ? Geo::rotate180(orig->poly) : orig->poly, li.tx, li.ty);
                colocadas.push_back({ li.id, li.tx, li.ty, li.rotada, poly });
            }
            auto colocadasPulidas = engine.reoptimizarLocal(colocadas, mejorOrden, telaW, 6);
            double h = 0;
            for (const auto& c : colocadasPulidas) h = std::max(h, Geo::bbox(c.poly).maxY);
            if (h < mejorResultado.altura - 0.01) {
                mejorResultado.altura = h;
                mejorResultado.layout.clear();
                for (const auto& c : colocadasPulidas) mejorResultado.layout.push_back({ c.id, c.tx, c.ty, c.rotada });
            }
        }


        // ===== Pulido final 3: optimizador conjunto de posiciones (SA) =====
        // A diferencia de todo lo anterior (que solo cambia el ORDEN o
        // reinserta piezas UNA por vez), esto mueve, rota e intercambia
        // TODAS las piezas a la vez, con las 15 piezas siempre validas
        // (sin colisiones) durante toda la busqueda. Es lo unico en este
        // motor capaz de encontrar, por ejemplo, un acomodo donde una pieza
        // grande termine flanqueada por otras dos -- una estructura que el
        // reordenamiento secuencial no puede plantear a proposito. Es mas
        // caro que los pulidos anteriores, por eso se hace una sola vez al
        // final, con presupuesto de iteraciones ajustable. Como cualquier
        // busqueda estocastica puede no mejorar (o directamente empeorar
        // temporalmente durante la busqueda), solo se acepta el resultado
        // si termina siendo mejor que lo que ya se tenia.
        if (iteracionesSA > 0) {
            std::vector<NestingEngine::ColocadaNFP> colocadas;
            LayoutResult base = engine.calcularLayout(mejorOrden, telaW);
            for (const auto& li : base.layout) {
                const Piece* orig = nullptr;
                for (const auto& p : mejorOrden) if (p.id == li.id) { orig = &p; break; }
                if (!orig) continue;
                std::vector<Point> poly = Geo::translate(
                    li.rotada ? Geo::rotate180(orig->poly) : orig->poly, li.tx, li.ty);
                colocadas.push_back({ li.id, li.tx, li.ty, li.rotada, poly });
            }
            JointSAOptimizer sa;
            std::mt19937 rngSA{ std::random_device{}() };
            if (threadId >= 0) {
                printf("[Hilo %d] Puliendo con SA conjunto (%d iteraciones)...\n", threadId, iteracionesSA);
                fflush(stdout);
            }
            // Temperatura baja: esto es un AFINADO fino sobre un acomodo ya
            // bueno, no una exploracion desde cero (que ya hizo el genetico).
            LayoutResult rSA = sa.optimizar(mejorOrden, telaW, colocadas, iteracionesSA, rngSA,
                                             false, 0.15, threadId);
            if (rSA.altura < mejorResultado.altura - 0.01) {
                mejorResultado = rSA;
            }
            if (threadId >= 0) {
                printf("[Hilo %d] SA terminado -> altura tras pulido: %.2f\n", threadId, mejorResultado.altura);
                fflush(stdout);
            }
        }

        if (mejorOrdenOut) *mejorOrdenOut = mejorOrden;
        return mejorResultado;
    }
};

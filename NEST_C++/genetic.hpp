#pragma once
#include "nesting_core.hpp"
#include <random>
#include <algorithm>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <cstdio>

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
    int GLOBAL_ELITE_SIZE = 8;
    int UMBRAL_PARADA = 20; // chequeos combinados sin mejora antes de cortar

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
    int ELITE_SIZE = 5;
    std::mt19937 rng{ std::random_device{}() };

    std::vector<double> pesos = { 0.22, 0.22, 0.15, 0.18, 0.23 };
    std::vector<int> successCounts = { 0, 0, 0, 0, 0 };
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
                            SharedPool* pool = nullptr) {
        NestingEngine engine;
        int RESTART = std::max(15, (int)(iteraciones * 0.12));

        auto t0 = std::chrono::steady_clock::now();

        LayoutResult mejorResultado = engine.calcularLayout(ordenInicial, telaW);
        std::vector<Piece> mejorOrden = ordenInicial;
        std::vector<EliteItem> elite;
        elite.push_back({ ordenInicial, mejorResultado.altura });

        std::vector<Piece> ordenActual = ordenInicial;
        LayoutResult resultadoActual = mejorResultado;
        int sinMejora = 0;

        {
            std::lock_guard<std::mutex> lock(g_printMutex);
            printf("[Hilo %d] iniciado | altura semilla: %.2f\n", threadId, mejorResultado.altura);
        }

        // Reporte de progreso: por iteraciones (cada ~5%) O cada 3 segundos, lo que ocurra primero
        int pasoReporte = std::max(1, iteraciones / 20);
        auto ultimoReporte = t0;

        for (int i = 0; i < iteraciones; i++) {
            int nCandidatos = 5;
            std::vector<std::vector<Piece>> candidatos(nCandidatos);
            std::vector<int> tipos(nCandidatos, -1);
            std::vector<LayoutResult> resultados(nCandidatos);

            for (int c = 0; c < nCandidatos; c++) {
                if (c == 0 && elite.size() > 1) {
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
            bool acepta = diff < 0 || randD() < std::exp(-diff / (temp * 2 + 0.05));

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
            } else {
                sinMejora++;
            }

            if (sinMejora >= RESTART) {
                sinMejora = 0;
                if (randD() < 0.6 && !elite.empty()) {
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
                }
            }

            // ===== Migracion entre hilos: cada cierto numero de iteraciones =====
            bool debeParar = false;
            int pasoMigra = std::max(5, iteraciones / 10);
            if (pool && (i + 1) % pasoMigra == 0) {
                std::lock_guard<std::mutex> lock(pool->mtx);
                if (mejorResultado.altura < pool->mejorGlobalAltura - 0.01) {
                    pool->mejorGlobalAltura = mejorResultado.altura;
                    pool->insertar(mejorOrden, mejorResultado.altura);
                    pool->chequeosSinMejora = 0;
                } else {
                    pool->chequeosSinMejora++;
                    // Si el pool global tiene algo mejor que lo que este hilo logro, lo adopta
                    if (!pool->eliteGlobal.empty() && pool->eliteGlobal[0].altura < resultadoActual.altura - 0.01) {
                        ordenActual = pool->eliteGlobal[0].orden;
                        resultadoActual = engine.calcularLayout(ordenActual, telaW);
                        insertarElite(elite, ordenActual, resultadoActual.altura);
                        if (resultadoActual.altura < mejorResultado.altura - 0.05) {
                            mejorResultado = resultadoActual;
                            mejorOrden = ordenActual;
                        }
                    }
                }
                if (pool->chequeosSinMejora >= pool->UMBRAL_PARADA) debeParar = true;
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

        if (mejorOrdenOut) *mejorOrdenOut = mejorOrden;
        return mejorResultado;
    }
};

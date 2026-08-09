#pragma once
#include "nesting_core.hpp"
#include <random>
#include <cmath>
#include <chrono>

// ===========================================================================
// Prototipo: optimizador de posicion SIMULTANEA para un grupo de piezas
// (pensado para las piezas "grandes": espalda, mangas, delanteros, etc).
//
// A diferencia del motor voraz de siempre (que coloca una pieza detras de
// otra, en un orden fijo, sin volver atras), esto mantiene TODAS las piezas
// del grupo colocadas a la vez desde el principio, y prueba mover, rotar o
// intercambiar piezas de a una (o de a pares) mientras el conjunto completo
// se mantiene siempre valido (sin colisiones). Esto SI puede llegar a un
// acomodo donde, por ejemplo, una pieza grande termine flanqueada por otras
// dos -- algo que el metodo secuencial nunca puede plantear a proposito,
// porque para eso hace falta decidir dejar un hueco libre ANTES de que
// existan las piezas que lo van a ocupar.
//
// Es una busqueda local tipo recocido simulado (simulated annealing) sobre
// (tx, ty, rotacion) de cada pieza del grupo, arrancando desde un acomodo
// valido ya conocido (el que da el motor voraz de siempre).
// ===========================================================================

class JointSAOptimizer {
public:
    struct Estado { double tx, ty; bool rot; };

    static std::vector<Point> inflar(const std::vector<Point>& poly, double m) {
        if (m <= 0) return poly;
        Clipper2Lib::PathD p;
        p.reserve(poly.size());
        for (auto& pt : poly) p.push_back(Clipper2Lib::PointD(pt.x, pt.y));
        auto res = Clipper2Lib::InflatePaths({ p }, m, Clipper2Lib::JoinType::Miter,
                                              Clipper2Lib::EndType::Polygon, 4.0, 4);
        if (res.empty()) return poly;
        std::vector<Point> out;
        out.reserve(res[0].size());
        for (auto& pt : res[0]) out.push_back({ pt.x, pt.y });
        return out;
    }

    // piezas: el grupo a optimizar en conjunto (p.ej. las "grandes").
    // inicial: un acomodo valido de partida para ese mismo grupo (por id),
    //          normalmente el que ya da el motor voraz de siempre.
    LayoutResult optimizar(const std::vector<Piece>& piezas, double telaW,
                            const std::vector<NestingEngine::ColocadaNFP>& inicial,
                            int iteraciones, std::mt19937& rng, bool verbose = false,
                            double T0 = 1.0, int threadId = -1) {
        int n = (int)piezas.size();
        const double margen = 0.3;

        std::vector<Estado> estado(n);
        std::vector<std::vector<Point>> polyInf0(n), polyInf180(n);
        std::vector<BBox> bb0(n), bb180(n);

        for (int i = 0; i < n; i++) {
            polyInf0[i] = inflar(piezas[i].poly, margen / 2.0);
            auto rot = Geo::rotate180(piezas[i].poly);
            polyInf180[i] = inflar(rot, margen / 2.0);
            bb0[i] = piezas[i].bb;
            bb180[i] = Geo::bbox(rot);
            estado[i] = { 0, 0, false };
            for (auto& c : inicial) if (c.id == piezas[i].id) { estado[i] = { c.tx, c.ty, c.rotada }; break; }
        }

        auto bbDe = [&](int i, bool rot) -> const BBox& { return rot ? bb180[i] : bb0[i]; };
        auto polyDe = [&](int i, const Estado& e) {
            const auto& base = e.rot ? polyInf180[i] : polyInf0[i];
            return Geo::translate(base, e.tx, e.ty);
        };

        auto alturaTotal = [&](const std::vector<Estado>& est) {
            double h = 0;
            for (int i = 0; i < n; i++) h = std::max(h, est[i].ty + bbDe(i, est[i].rot).h);
            return h;
        };

        auto valido = [&](int i, const Estado& e, const std::vector<Estado>& est) {
            const BBox& b = bbDe(i, e.rot);
            if (e.tx < -0.001 || e.tx + b.w > telaW + 0.001 || e.ty < -0.001) return false;
            auto tp = polyDe(i, e);
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                if (Geo::collide(tp, polyDe(j, est[j]))) return false;
            }
            return true;
        };

        // Si el estado inicial (tal cual llega) ya no fuera valido por algun
        // motivo, lo dejamos tal cual -- el resto del pipeline (finalizarLayout)
        // tiene una red de seguridad de colision exacta al final igual.
        double alturaActual = alturaTotal(estado);
        std::vector<Estado> mejorEstado = estado;
        double mejorAltura = alturaActual;

        std::uniform_real_distribution<double> u01(0.0, 1.0);
        std::uniform_int_distribution<int> pick(0, n - 1);

        // Muestreo sesgado por area: las piezas grandes son las que definen
        // la altura final, asi que conviene que la busqueda les dedique mas
        // intentos que a las piezas chicas (que solo rellenan huecos y casi
        // no mueven la aguja en altura). Se arma una ruleta ponderada por
        // area (con un piso minimo para que las chicas no queden nunca del
        // todo excluidas).
        std::vector<double> pesos(n);
        double sumaPesos = 0;
        for (int i = 0; i < n; i++) { pesos[i] = std::sqrt(piezas[i].area) + 3.0; sumaPesos += pesos[i]; }
        std::vector<double> acumulado(n);
        { double acc = 0; for (int i = 0; i < n; i++) { acc += pesos[i] / sumaPesos; acumulado[i] = acc; } }
        auto pickSesgado = [&]() {
            double r = u01(rng);
            int lo = 0, hi = n - 1;
            while (lo < hi) { int mid = (lo + hi) / 2; if (acumulado[mid] < r) lo = mid + 1; else hi = mid; }
            return lo;
        };

        // Ruleta inversa (sesgo hacia piezas CHICAS): para el movimiento de
        // flanqueo, la pieza que se reubica normalmente debe ser una chica
        // (una tira, un puno) y la referencia (a cuyo costado se pega) debe
        // ser una grande -- por eso hacen falta las dos ruletas.
        std::vector<double> pesosInv(n);
        double sumaPesosInv = 0;
        for (int i = 0; i < n; i++) { pesosInv[i] = 1.0 / (std::sqrt(piezas[i].area) + 3.0); sumaPesosInv += pesosInv[i]; }
        std::vector<double> acumuladoInv(n);
        { double acc = 0; for (int i = 0; i < n; i++) { acc += pesosInv[i] / sumaPesosInv; acumuladoInv[i] = acc; } }
        auto pickInverso = [&]() {
            double r = u01(rng);
            int lo = 0, hi = n - 1;
            while (lo < hi) { int mid = (lo + hi) / 2; if (acumuladoInv[mid] < r) lo = mid + 1; else hi = mid; }
            return lo;
        };

        int sinMejora = 0;
        int RESTART_STALL = std::max(2000, iteraciones / 200);
        auto tInicio = std::chrono::steady_clock::now();
        auto tUltimoReporte = tInicio;
        for (int it = 0; it < iteraciones; it++) {
            double T = std::max(0.02, T0 * (1.0 - (double)it / iteraciones));

            // Reiniciar-al-mejor: si llevamos muchas iteraciones sin superar
            // el mejor encontrado, la cadena se alejo a una zona mala y a
            // baja temperatura ya no puede "bajar" de vuelta facil. En vez
            // de dejarla vagando, se la trae de vuelta al mejor conocido y
            // sigue explorando desde ahi (evita desperdiciar el resto del
            // presupuesto de iteraciones lejos de donde importa).
            if (sinMejora >= RESTART_STALL) {
                estado = mejorEstado;
                alturaActual = mejorAltura;
                sinMejora = 0;
            }

            double r = u01(rng);
            std::vector<Estado> prop = estado;
            std::vector<int> tocadas;

            if (r < 0.45) {
                // Mover una pieza (paso chico, se achica mas con la temperatura).
                int i = pickSesgado();
                double paso = 1.0 + 5.0 * T;
                prop[i].tx = std::max(0.0, prop[i].tx + (u01(rng) * 2 - 1) * paso);
                prop[i].ty = std::max(0.0, prop[i].ty + (u01(rng) * 2 - 1) * paso);
                tocadas = { i };
            } else if (r < 0.58) {
                // Rotar una pieza in-situ.
                int i = pickSesgado();
                prop[i].rot = !prop[i].rot;
                tocadas = { i };
            } else if (r < 0.80) {
                // Intercambiar posicion+rotacion entre dos piezas: es el
                // movimiento clave para que una pieza pueda terminar
                // "flanqueada" -- si dos piezas ya estan cerca de los
                // costados de una tercera, intercambiarlas con otras puede
                // encontrar una combinacion mejor sin tener que reconstruir
                // el hueco desde cero.
                int i = pickSesgado(), j = pickSesgado();
                if (i == j) continue;
                std::swap(prop[i], prop[j]);
                tocadas = { i, j };
            } else if (r < 0.92) {
                // Flanqueo dirigido: toma una pieza cualquiera (con sesgo
                // hacia las chicas, que son las que suelen terminar flanqueando
                // a una grande) y la pega directo contra el costado
                // izquierdo o derecho de OTRA pieza ya colocada, a la misma
                // altura. El intercambio y el movimiento libre pueden tardar
                // muchas iteraciones en topar por azar con esta configuracion
                // exacta -- este movimiento la propone directamente, para
                // que dejar una pieza "flanqueando" a otra sea algo que la
                // busqueda puede encontrar a proposito, no un accidente raro.
                int i = pickInverso(); // sesgo hacia piezas CHICAS
                int j = pickSesgado(); // sesgo hacia piezas GRANDES
                if (i == j) continue;
                bool nuevoRot = u01(rng) < 0.5 ? prop[i].rot : !prop[i].rot;
                const BBox& bi = bbDe(i, nuevoRot);
                const BBox& bj = bbDe(j, prop[j].rot);
                bool ladoIzq = u01(rng) < 0.5;
                double nuevoTx = ladoIzq ? (prop[j].tx - bi.w) : (prop[j].tx + bj.w);
                double nuevoTy = prop[j].ty + (u01(rng) * 2 - 1) * (bj.h * 0.3);
                prop[i].rot = nuevoRot;
                prop[i].tx = std::max(0.0, nuevoTx);
                prop[i].ty = std::max(0.0, nuevoTy);
                tocadas = { i };
            } else if (r < 0.97) {
                // Apilado dirigido: como el flanqueo, pero pega una pieza
                // contra el borde de ARRIBA o ABAJO de otra (en vez de al
                // costado), alineando el mismo tx. Es el movimiento que hace
                // falta para amontonar piezas rectangulares chatas y anchas
                // (pretinas, puños, franjas) una encima de otra en una
                // columna -- algo que solo mover/intercambiar al azar rara
                // vez encuentra por si solo cuando hay varias piezas asi.
                // A diferencia del flanqueo (sesgado chica-contra-grande),
                // aqui las dos piezas se eligen sin sesgo, porque este
                // patron de "apilar" ocurre tanto entre piezas chicas entre
                // si como entre una chica y el borde de una grande.
                int i = pick(rng), j = pick(rng);
                if (i == j) continue;
                bool nuevoRot = u01(rng) < 0.5 ? prop[i].rot : !prop[i].rot;
                const BBox& bi = bbDe(i, nuevoRot);
                const BBox& bj = bbDe(j, prop[j].rot);
                bool arriba = u01(rng) < 0.5;
                double nuevoTy = arriba ? (prop[j].ty + bj.h) : (prop[j].ty - bi.h);
                double nuevoTx = prop[j].tx + (u01(rng) * 2 - 1) * (bj.w * 0.3);
                prop[i].rot = nuevoRot;
                prop[i].tx = std::max(0.0, nuevoTx);
                prop[i].ty = std::max(0.0, nuevoTy);
                tocadas = { i };
            } else {
                // Salto grande: reposicionar una pieza en un punto aleatorio
                // del ancho de tela. Solo un 3% de los intentos (es
                // disruptivo), y solo se acepta si de verdad no empeora --
                // no tiene sentido dejar que la temperatura "perdone" un
                // teletransporte al azar de una pieza grande, eso solo
                // desperdicia iteraciones.
                int i = pickSesgado();
                bool nuevoRot = u01(rng) < 0.5 ? prop[i].rot : !prop[i].rot;
                const BBox& b = bbDe(i, nuevoRot);
                double maxX = std::max(0.0, telaW - b.w);
                prop[i].rot = nuevoRot;
                prop[i].tx = u01(rng) * maxX;
                prop[i].ty = std::max(0.0, prop[i].ty + (u01(rng) * 2 - 1) * 15.0);
                tocadas = { i };

                bool ok = valido(i, prop[i], prop);
                if (!ok) { sinMejora++; continue; }
                double alturaProp = alturaTotal(prop);
                if (alturaProp <= alturaActual + 0.005) {
                    estado = std::move(prop);
                    alturaActual = alturaProp;
                    if (alturaActual < mejorAltura - 0.005) { mejorAltura = alturaActual; mejorEstado = estado; sinMejora = 0; }
                    else sinMejora++;
                } else sinMejora++;
                continue;
            }

            bool ok = true;
            for (int i : tocadas) if (!valido(i, prop[i], prop)) { ok = false; break; }
            if (!ok) { sinMejora++; continue; }

            double alturaProp = alturaTotal(prop);
            double delta = alturaProp - alturaActual;
            bool acepta = delta < 0 || u01(rng) < std::exp(-delta / (T * 2.5 + 0.08));
            if (acepta) {
                estado = std::move(prop);
                alturaActual = alturaProp;
                if (alturaActual < mejorAltura - 0.005) {
                    mejorAltura = alturaActual;
                    mejorEstado = estado;
                    sinMejora = 0;
                } else sinMejora++;
            } else sinMejora++;

            if (verbose && (it % std::max(1, iteraciones / 10) == 0)) {
                printf("  [SA-grandes] it=%d/%d T=%.2f altura_actual=%.2f mejor=%.2f\n",
                       it, iteraciones, T, alturaActual, mejorAltura);
            }
            if (threadId >= 0) {
                auto ahora = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(ahora - tUltimoReporte).count() >= 3.0) {
                    tUltimoReporte = ahora;
                    double pct = 100.0 * (it + 1) / iteraciones;
                    printf("[Hilo %d]   SA %.0f%% (%d/%d) | altura actual: %.2f | mejor: %.2f\n",
                           threadId, pct, it + 1, iteraciones, alturaActual, mejorAltura);
                    fflush(stdout);
                }
            }
        }

        LayoutResult res;
        res.altura = mejorAltura;
        for (int i = 0; i < n; i++)
            res.layout.push_back({ piezas[i].id, mejorEstado[i].tx, mejorEstado[i].ty, mejorEstado[i].rot });
        return res;
    }
};

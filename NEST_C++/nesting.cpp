#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>

// ============ JSON MINIMAL (sin librería externa) ============
// Parser JSON muy básico solo para nuestro formato

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

double parseNumber(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    size_t start = i;
    while (i < s.size() && (s[i] == '-' || s[i] == '.' || (s[i] >= '0' && s[i] <= '9'))) i++;
    return std::stod(s.substr(start, i - start));
}

// ============ ESTRUCTURAS ============
struct Point { double x, y; };

struct Piece {
    int id;
    std::vector<Point> poly;
    double minX, minY, maxX, maxY, w, h, area;
};

struct Placed {
    int id;
    double x, y;
    bool rotated;
};

// ============ BOUNDING BOX ============
void calcBB(Piece& p) {
    p.minX = p.minY = 1e9;
    p.maxX = p.maxY = -1e9;
    for (auto& pt : p.poly) {
        p.minX = std::min(p.minX, pt.x);
        p.minY = std::min(p.minY, pt.y);
        p.maxX = std::max(p.maxX, pt.x);
        p.maxY = std::max(p.maxY, pt.y);
    }
    p.w = p.maxX - p.minX;
    p.h = p.maxY - p.minY;
    p.area = p.w * p.h;
}

// ============ COLISIONES ============
bool pointInPoly(double px, double py, const std::vector<Point>& poly) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        double xi = poly[i].x, yi = poly[i].y;
        double xj = poly[j].x, yj = poly[j].y;
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi + 1e-9) + xi))
            inside = !inside;
    }
    return inside;
}

bool collide(const Piece& pa, double ax, double ay, const Piece& pb, double bx, double by) {
    // AABB
    double aMinX = ax + pa.minX, aMaxX = ax + pa.maxX;
    double aMinY = ay + pa.minY, aMaxY = ay + pa.maxY;
    double bMinX = bx + pb.minX, bMaxX = bx + pb.maxX;
    double bMinY = by + pb.minY, bMaxY = by + pb.maxY;
    if (aMaxX <= bMinX || bMaxX <= aMinX || aMaxY <= bMinY || bMaxY <= aMinY)
        return false;

    // Puntos de A dentro de B
    for (auto& p : pa.poly)
        if (pointInPoly(p.x + ax - bx, p.y + ay - by, pb.poly))
            return true;

    // Puntos de B dentro de A
    for (auto& p : pb.poly)
        if (pointInPoly(p.x + bx - ax, p.y + by - ay, pa.poly))
            return true;

    return false;
}

// ============ BUSCAR POSICIÓN ============
bool findPos(const Piece& p, double sheetW, double& outX, double& outY,
             const std::vector<Placed>& placed, const std::vector<Piece>& pieces, double maxH) {
    std::vector<double> xSet = {0.0};
    for (auto& pl : placed) {
        const Piece& other = pieces[pl.id];
        xSet.push_back(pl.x + other.w);
        xSet.push_back(pl.x);
    }
    for (int r = 1; r <= 4; r++)
        xSet.push_back(round(sheetW * r / 5.0));

    std::sort(xSet.begin(), xSet.end());
    xSet.erase(std::unique(xSet.begin(), xSet.end()), xSet.end());

    double bestScore = 1e18;
    bool found = false;

    for (double sx : xSet) {
        if (sx + p.w > sheetW + 1e-6) continue;

        double ty = maxH + 1.0;
        double drop = std::max(5.0, std::min(20.0, floor(maxH / 5.0)));

        while (ty - drop >= -1e-6) {
            bool ok = true;
            for (auto& pl : placed)
                if (collide(p, sx, ty - drop, pieces[pl.id], pl.x, pl.y)) {
                    ok = false; break;
                }
            if (!ok) break;
            ty -= drop;
        }
        while (ty - 1.0 >= -1e-6) {
            bool ok = true;
            for (auto& pl : placed)
                if (collide(p, sx, ty - 1.0, pieces[pl.id], pl.x, pl.y)) {
                    ok = false; break;
                }
            if (!ok) break;
            ty -= 1.0;
        }

        // Slide
        for (int dx : {-1, 1}) {
            double nx = sx + dx;
            if (nx < -1e-6 || nx + p.w > sheetW + 1e-6) continue;
            bool ok = true;
            for (auto& pl : placed)
                if (collide(p, nx, ty, pieces[pl.id], pl.x, pl.y)) {
                    ok = false; break;
                }
            if (ok) sx = nx;
        }

        double score = ty * 100000.0 + sx;
        if (score < bestScore) {
            bestScore = score;
            outX = sx; outY = ty;
            found = true;
        }
    }
    return found;
}

// ============ LECTURA JSON MANUAL ============
std::vector<Piece> readPieces(const std::string& filename, double& sheetW) {
    std::vector<Piece> pieces;
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "No pude abrir " << filename << "\n";
        return pieces;
    }

    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    f.close();

    // Buscar sheetWidth
    size_t pos = json.find("\"sheetWidth\"");
    if (pos != std::string::npos) {
        pos = json.find(':', pos);
        if (pos != std::string::npos) {
            pos++;
            sheetW = parseNumber(json, pos);
        }
    }

    // Buscar pieces
    pos = json.find("\"pieces\"");
    if (pos == std::string::npos) return pieces;

    pos = json.find('[', pos);
    if (pos == std::string::npos) return pieces;
    pos++;

    while (true) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        Piece p;
        // Buscar id
        size_t idPos = json.find("\"id\"", objStart);
        if (idPos != std::string::npos) {
            idPos = json.find(':', idPos);
            if (idPos != std::string::npos) {
                idPos++;
                p.id = (int)parseNumber(json, idPos);
            }
        }

        // Buscar points
        size_t ptsPos = json.find("\"points\"", objStart);
        if (ptsPos == std::string::npos) break;

        ptsPos = json.find('[', ptsPos);
        if (ptsPos == std::string::npos) break;
        ptsPos++;

        while (true) {
            while (ptsPos < json.size() && json[ptsPos] != '[') {
                if (json[ptsPos] == ']') break;
                ptsPos++;
            }
            if (ptsPos >= json.size() || json[ptsPos] == ']') break;

            ptsPos++; // skip [
            double x = parseNumber(json, ptsPos);
            while (ptsPos < json.size() && json[ptsPos] != ',' && json[ptsPos] != ']') ptsPos++;
            if (ptsPos < json.size() && json[ptsPos] == ',') ptsPos++;
            double y = parseNumber(json, ptsPos);
            p.poly.push_back({x, y});

            while (ptsPos < json.size() && json[ptsPos] != ']' && json[ptsPos] != '[') ptsPos++;
        }

        calcBB(p);
        pieces.push_back(p);

        pos = objStart + 1;
        size_t nextObj = json.find('{', pos);
        size_t closeArr = json.find(']', pos);
        if (closeArr != std::string::npos && (nextObj == std::string::npos || closeArr < nextObj))
            break;
    }

    return pieces;
}

// ============ ESCRITURA JSON ============
void writeOutput(const std::string& filename, double sheetW, double sheetH,
                 const std::vector<Placed>& placed) {
    std::ofstream f(filename);
    f << "{\n";
    f << "  \"sheetWidth\": " << sheetW << ",\n";
    f << "  \"sheetHeight\": " << sheetH << ",\n";
    f << "  \"placements\": [\n";
    for (size_t i = 0; i < placed.size(); i++) {
        f << "    {\"id\": " << placed[i].id
          << ", \"x\": " << placed[i].x
          << ", \"y\": " << placed[i].y
          << ", \"rotated\": " << (placed[i].rotated ? "true" : "false") << "}";
        if (i < placed.size() - 1) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

// ============ MAIN ============
int main() {
    double sheetW = 100.0;
    std::vector<Piece> pieces = readPieces("input.json", sheetW);

    std::cout << "Piezas leidas: " << pieces.size() << "\n";
    std::cout << "Sheet width: " << sheetW << "\n";

    if (pieces.empty()) {
        std::cerr << "No se pudieron leer piezas\n";
        return 1;
    }

    // Ordenar por area descendente
    std::sort(pieces.begin(), pieces.end(),
        [](const Piece& a, const Piece& b) { return a.area > b.area; });

    std::vector<Placed> placed;
    double maxH = 0;

    for (auto& p : pieces) {
        double tx, ty;
        if (findPos(p, sheetW, tx, ty, placed, pieces, maxH)) {
            placed.push_back({p.id, tx, ty, false});
            maxH = std::max(maxH, ty + p.h);
            std::cout << "Pieza " << p.id << " en (" << tx << ", " << ty << ")\n";
        } else {
            std::cout << "Pieza " << p.id << " NO COLOCO\n";
        }
    }

    writeOutput("output.json", sheetW, maxH, placed);

    std::cout << "\n=== RESULTADO ===\n";
    std::cout << "Altura final: " << maxH << "\n";
    std::cout << "Piezas colocadas: " << placed.size() << "/" << pieces.size() << "\n";

    return 0;
}
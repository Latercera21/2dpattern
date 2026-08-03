#pragma once
#include "curve_parser.hpp"
#include <string>
#include <map>
#include <stdexcept>
#include <fstream>
#include <sstream>

// ===================== Parser JSON minimo (sin dependencias) =====================
// Cubre solo lo necesario para leer el esquema de piezas:
// { "pxPerCm": num, "figures": [ { "closed": bool, "vertices":[{x,y,xCm,yCm}],
//   "edges":[{start,end,curved,controlX,controlY}] } ] }

namespace MiniJSON {

enum class Tipo { Null, Bool, Num, Str, Array, Obj };

struct Valor {
    Tipo tipo = Tipo::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Valor> arr;
    std::map<std::string, Valor> obj;

    bool tieneClave(const std::string& k) const { return obj.count(k) > 0; }
    const Valor& operator[](const std::string& k) const {
        static Valor nulo;
        auto it = obj.find(k);
        return it != obj.end() ? it->second : nulo;
    }
    double asNum(double def = 0) const { return tipo == Tipo::Num ? num : def; }
    bool asBool(bool def = false) const { return tipo == Tipo::Bool ? b : def; }
    bool esNull() const { return tipo == Tipo::Null; }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s), i_(0) {}

    Valor parse() {
        saltarEspacios();
        Valor v = parseValor();
        return v;
    }

private:
    const std::string& s_;
    size_t i_;

    void saltarEspacios() {
        while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) i_++;
    }

    char peek() { return i_ < s_.size() ? s_[i_] : '\0'; }

    Valor parseValor() {
        saltarEspacios();
        char c = peek();
        if (c == '{') return parseObj();
        if (c == '[') return parseArr();
        if (c == '"') return parseStr();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { i_ += 4; return Valor{}; } // null
        return parseNum();
    }

    Valor parseObj() {
        Valor v; v.tipo = Tipo::Obj;
        i_++; // {
        saltarEspacios();
        if (peek() == '}') { i_++; return v; }
        while (true) {
            saltarEspacios();
            Valor clave = parseStr();
            saltarEspacios();
            if (peek() != ':') throw std::runtime_error("esperaba ':' en JSON");
            i_++; // :
            Valor val = parseValor();
            v.obj[clave.str] = val;
            saltarEspacios();
            if (peek() == ',') { i_++; continue; }
            if (peek() == '}') { i_++; break; }
            throw std::runtime_error("formato de objeto JSON invalido");
        }
        return v;
    }

    Valor parseArr() {
        Valor v; v.tipo = Tipo::Array;
        i_++; // [
        saltarEspacios();
        if (peek() == ']') { i_++; return v; }
        while (true) {
            Valor val = parseValor();
            v.arr.push_back(val);
            saltarEspacios();
            if (peek() == ',') { i_++; continue; }
            if (peek() == ']') { i_++; break; }
            throw std::runtime_error("formato de arreglo JSON invalido");
        }
        return v;
    }

    Valor parseStr() {
        Valor v; v.tipo = Tipo::Str;
        if (peek() != '"') throw std::runtime_error("esperaba string en JSON");
        i_++; // "
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_];
            if (c == '\\' && i_ + 1 < s_.size()) {
                char n = s_[i_ + 1];
                if (n == 'n') out += '\n';
                else if (n == 't') out += '\t';
                else out += n;
                i_ += 2;
            } else {
                out += c;
                i_++;
            }
        }
        i_++; // "
        v.str = out;
        return v;
    }

    Valor parseBool() {
        Valor v; v.tipo = Tipo::Bool;
        if (s_.compare(i_, 4, "true") == 0) { v.b = true; i_ += 4; }
        else { v.b = false; i_ += 5; } // false
        return v;
    }

    Valor parseNum() {
        Valor v; v.tipo = Tipo::Num;
        size_t start = i_;
        if (peek() == '-') i_++;
        while (i_ < s_.size() && (std::isdigit((unsigned char)s_[i_]) || s_[i_] == '.' ||
               s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-')) i_++;
        v.num = std::stod(s_.substr(start, i_ - start));
        return v;
    }
};

inline Valor parse(const std::string& texto) {
    Parser p(texto);
    return p.parse();
}

inline std::string leerArchivo(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("no se pudo abrir: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace MiniJSON

// ===================== Carga de piezas desde JSON =====================
// Equivalente a cargarJSON(texto) del JS.

inline std::vector<Piece> cargarPiezasDesdeJSON(const std::string& texto) {
    using namespace MiniJSON;
    Valor data = parse(texto);

    double pxPerCm = data.tieneClave("pxPerCm") ? data["pxPerCm"].asNum() : 37.79527559055118;

    std::vector<Piece> piezas;
    const Valor& figures = data["figures"];
    if (figures.tipo != Tipo::Array) return piezas;

    int idCounter = 0;
    for (const auto& fig : figures.arr) {
        bool closed = fig.tieneClave("closed") ? fig["closed"].asBool(true) : true;
        const Valor& verticesJ = fig["vertices"];
        if (!closed || verticesJ.tipo != Tipo::Array || verticesJ.arr.size() < 3) continue;

        // vertices en cm
        std::vector<Point> verts;
        for (const auto& vj : verticesJ.arr) {
            double x, y;
            if (vj.tieneClave("xCm")) x = vj["xCm"].asNum();
            else x = vj["x"].asNum() / pxPerCm;
            if (vj.tieneClave("yCm")) y = vj["yCm"].asNum();
            else y = vj["y"].asNum() / pxPerCm;
            verts.push_back({ x, y });
        }
        size_t n = verts.size();

        // mapa de edges por "start_end"
        std::map<std::pair<int,int>, const Valor*> edgeMap;
        const Valor& edgesJ = fig["edges"];
        if (edgesJ.tipo == Tipo::Array) {
            for (const auto& e : edgesJ.arr) {
                int start = (int)e["start"].asNum();
                int end = (int)e["end"].asNum();
                edgeMap[{start, end}] = &e;
            }
        }

        std::vector<Segmento> segs;
        for (size_t i = 0; i < n; i++) {
            size_t j = (i + 1) % n;
            auto it = edgeMap.find({ (int)i, (int)j });
            if (it == edgeMap.end()) {
                segs.push_back({ true, { verts[i], verts[j], {} } });
            } else {
                const Valor& e = *it->second;
                bool curved = e.tieneClave("curved") ? e["curved"].asBool(false) : false;
                int st = (int)e["start"].asNum();
                int en = (int)e["end"].asNum();
                if (!curved) {
                    segs.push_back({ true, { verts[st], verts[en], {} } });
                } else {
                    double cx = e["controlX"].asNum() / pxPerCm;
                    double cy = e["controlY"].asNum() / pxPerCm;
                    segs.push_back({ false, { verts[st], { cx, cy }, verts[en] } });
                }
            }
        }

        piezas.push_back(CurveGeo::construirPieza(idCounter++, segs));
    }
    return piezas;
}

inline std::vector<Piece> cargarPiezasDesdeArchivo(const std::string& path) {
    return cargarPiezasDesdeJSON(MiniJSON::leerArchivo(path));
}

// ===================== Escritura de resultado a JSON =====================
// Equivalente a JSON.stringify(layout) del JS.

inline std::string layoutAJSON(const LayoutResult& res) {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < res.layout.size(); i++) {
        const auto& it = res.layout[i];
        ss << "  {\"id\":" << it.id
           << ",\"tx\":" << it.tx
           << ",\"ty\":" << it.ty
           << ",\"rotada\":" << (it.rotada ? "true" : "false") << "}";
        if (i + 1 < res.layout.size()) ss << ",";
        ss << "\n";
    }
    ss << "]\n";
    return ss.str();
}

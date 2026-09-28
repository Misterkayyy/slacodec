#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "core/container.hpp"
#include <fstream>

static void usage() {
    std::fprintf(stderr,
        "Uso: slacodec-tag <arquivo.slac> --chave=\"valor\" [...]\n"
        "Chaves: artist, album, title, date, genre, track, comment\n"        "Capa:  --cover=capa.jpg  (embute a imagem no arquivo)\n"
        "Exemplo:\n"
        "  slacodec-tag musica.slac --artist=\"The Killers\" --album=\"Battle Born\" \\\n"
        "      --title=\"Flesh And Bone\" --date=2012 --genre=\"Rock\" --track=3\n");
}

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string path = argv[1];
    slac::SlacMetadata meta;
    std::string coverPath;

    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--", 0) != 0) { usage(); return 1; }
        auto eq = a.find('=');
        if (eq == std::string::npos) { usage(); return 1; }
        std::string rawkey = a.substr(2, eq - 2);
        if (rawkey == "cover") { coverPath = a.substr(eq + 1); continue; }
        std::string key = rawkey;
        for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        meta.set(key, a.substr(eq + 1));
    }

    // Mostra tags existentes antes de sobrescrever
    slac::SlacMetadata existing;
    if (slac::readMetaFromFile(path, existing)) {
        for (auto& kv : existing.fields) {
            if (!meta.get(kv.first)) meta.set(kv.first, kv.second); // preserva nao informadas
        }
    }

    if (!slac::addMetaToFile(path, meta)) {
        std::fprintf(stderr, "Erro: falha ao remuxar %s\n", path.c_str());
        return 1;
    }
    if (!coverPath.empty()) {
        std::ifstream cf(coverPath, std::ios::binary);
        if (!cf) { std::fprintf(stderr, "Erro: capa nao encontrada: %s\n", coverPath.c_str()); return 1; }
        std::vector<uint8_t> cbuf((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
        std::string mime = "image/jpeg";
        if (coverPath.size() > 4 && coverPath.substr(coverPath.size() - 4) == ".png") mime = "image/png";
        if (!slac::addCoverToFile(path, mime, cbuf)) {
            std::fprintf(stderr, "Erro: falha ao embutir capa em %s\n", path.c_str()); return 1;
        }
        std::printf("Capa embutida: %s (%zu KB)\n", mime.c_str(), cbuf.size() / 1024);
    }
    std::printf("Tags aplicadas em %s:\n", path.c_str());
    for (auto& kv : meta.fields)
        std::printf("  %s = %s\n", kv.first.c_str(), kv.second.c_str());
    return 0;
}

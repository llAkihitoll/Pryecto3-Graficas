#include "texture.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

// ---------------------------------------------------------------- lector de PNG

static uint32_t readU32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Predictor "Paeth" del formato PNG: elige el vecino (izquierda, arriba o
// arriba-izquierda) mas parecido a izquierda + arriba - arriba-izquierda.
static uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    return (uint8_t)(pb <= pc ? b : c);
}

// Lee un PNG de 8 bits por canal (RGB o RGBA) y devuelve sus pixeles fila
// por fila. zlib solo descomprime los datos; la lectura de los bloques del
// archivo y el deshacer los filtros de cada fila estan hechos aqui.
static bool readPNG(const std::string& path, int& width, int& height, int& channels, std::vector<uint8_t>& pixels) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (data.size() < 8 || !std::equal(signature, signature + 8, data.begin())) return false;

    // Bloques: longitud, tipo, datos y CRC. Interesan IHDR (tamano y formato)
    // e IDAT (los pixeles comprimidos, pueden venir en varios bloques).
    std::vector<uint8_t> compressed;
    int bitDepth = 0, colorType = 0, interlace = 0;
    size_t pos = 8;
    while (pos + 8 <= data.size()) {
        uint32_t length = readU32(&data[pos]);
        std::string type(data.begin() + pos + 4, data.begin() + pos + 8);
        const uint8_t* chunk = &data[pos + 8];
        if (pos + 12 + length > data.size()) return false;
        if (type == "IHDR") {
            width = (int)readU32(chunk);
            height = (int)readU32(chunk + 4);
            bitDepth = chunk[8];
            colorType = chunk[9];
            interlace = chunk[12];
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), chunk, chunk + length);
        } else if (type == "IEND") {
            break;
        }
        pos += 12 + length;
    }
    if (bitDepth != 8 || interlace != 0 || (colorType != 2 && colorType != 6)) {
        std::fprintf(stderr, "%s: formato PNG no soportado (se necesita RGB/RGBA de 8 bits)\n", path.c_str());
        return false;
    }
    channels = colorType == 6 ? 4 : 3;

    // Cada fila empieza con un byte que dice que filtro se uso.
    size_t stride = (size_t)width * channels;
    std::vector<uint8_t> raw((stride + 1) * height);
    uLongf rawSize = (uLongf)raw.size();
    if (uncompress(raw.data(), &rawSize, compressed.data(), (uLong)compressed.size()) != Z_OK) return false;

    // Deshacer los filtros: cada byte se guardo como diferencia contra un
    // vecino ya decodificado (a = izquierda, b = arriba, c = arriba-izquierda).
    pixels.assign(stride * height, 0);
    for (int y = 0; y < height; ++y) {
        uint8_t filter = raw[y * (stride + 1)];
        const uint8_t* in = &raw[y * (stride + 1) + 1];
        uint8_t* out = &pixels[y * stride];
        const uint8_t* up = y > 0 ? out - stride : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            int a = i >= (size_t)channels ? out[i - channels] : 0;
            int b = up ? up[i] : 0;
            int c = (up && i >= (size_t)channels) ? up[i - channels] : 0;
            int predicted = 0;
            switch (filter) {
                case 1: predicted = a; break;
                case 2: predicted = b; break;
                case 3: predicted = (a + b) / 2; break;
                case 4: predicted = paeth(a, b, c); break;
            }
            out[i] = (uint8_t)(in[i] + predicted);
        }
    }
    return true;
}

// ---------------------------------------------------------------- mapa de normales

Vec3 NormalTexture::sample(float u, float v) const {
    // Repeticion: solo importa la parte fraccionaria. En la imagen la fila 0
    // es la de arriba, en las UV v = 0 es abajo.
    float fu = u - std::floor(u), fv = 1.0f - (v - std::floor(v));
    float fx = fu * width - 0.5f, fy = fv * height - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    auto at = [&](int x, int y) -> const Vec3& {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
        return texels[(size_t)y * width + x];
    };
    Vec3 top = lerp(at(x0, y0), at(x0 + 1, y0), tx);
    Vec3 bottom = lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx);
    return lerp(top, bottom, ty);
}

std::shared_ptr<NormalTexture> loadNormalTexture(const std::string& path, bool flipGreen) {
    static std::map<std::string, std::shared_ptr<NormalTexture>> cache;
    auto found = cache.find(path);
    if (found != cache.end()) return found->second;

    int width, height, channels;
    std::vector<uint8_t> px;
    if (!readPNG(path, width, height, channels, px)) {
        std::fprintf(stderr, "No se pudo leer el mapa de normales: %s\n", path.c_str());
        return nullptr;
    }

    // Formato empaquetado: el alfa trae la x de la normal y no es casi todo 255.
    bool packed = false;
    if (channels == 4) {
        long long alphaSum = 0;
        for (size_t i = 3; i < px.size(); i += 4) alphaSum += px[i];
        packed = alphaSum < 250LL * width * height;
    }

    auto tex = std::make_shared<NormalTexture>();
    tex->width = width;
    tex->height = height;
    tex->texels.resize((size_t)width * height);
    for (size_t i = 0; i < tex->texels.size(); ++i) {
        const uint8_t* p = &px[i * channels];
        float x, y, z;
        if (packed) {
            x = p[3] / 127.5f - 1.0f;
            y = p[1] / 127.5f - 1.0f;
            z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
        } else {
            x = p[0] / 127.5f - 1.0f;
            y = p[1] / 127.5f - 1.0f;
            z = p[2] / 127.5f - 1.0f;
        }
        if (flipGreen) y = -y;
        tex->texels[i] = normalize(Vec3{x, y, z});
    }
    std::printf("%s: %dx%d, formato %s\n", path.c_str(), width, height, packed ? "empaquetado (x en alfa)" : "RGB");
    cache[path] = tex;
    return tex;
}

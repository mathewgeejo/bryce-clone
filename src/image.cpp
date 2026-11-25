#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#else
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <tiffio.h>
#endif
#include "image.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

void Image::buildLightDistribution() {
    lightCdf.resize(px.size());
    double sum = 0;
    for (int y = 0; y < h; ++y) {
        double area = std::cos(PI * y / h) - std::cos(PI * (y + 1) / h);
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            sum += dot(px[i], V3(.2126f, .7152f, .0722f)) * area;
            lightCdf[i] = sum;
        }
    }
}

static std::shared_ptr<Image> loadHDR(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    char line[1024];
    auto readLine = [&] {
        if (!file.getline(line, sizeof line)) return std::string{};
        std::string text(line);
        if (!text.empty() && text.back() == '\r') text.pop_back();
        return text;
    };
    auto magic = readLine();
    if (magic != "#?RADIANCE" && magic != "#?RGBE") return {};
    bool rgb = false;
    for (auto field = readLine(); !field.empty(); field = readLine()) {
        if (field == "FORMAT=32-bit_rle_rgbe") rgb = true;
        else if (field.rfind("FORMAT=", 0) == 0) return {};
    }
    std::istringstream resolution(readLine());
    int height = 0, width = 0;
    std::string ya, xa;
    if (!rgb || !(resolution >> ya >> height >> xa >> width) ||
        (ya != "-Y" && ya != "+Y") || (xa != "+X" && xa != "-X") ||
        width < 1 || height < 1 || width > 16384 || height > 16384 || (size_t)width * height > 33554432) return {};
    auto image = std::make_shared<Image>();
    image->w = width;
    image->h = height;
    image->hdr = true;
    image->px.resize((size_t)width * height);
    using Pixel = std::array<uint8_t, 4>;
    std::vector<Pixel> row(width);
    for (int y = 0; y < height; ++y) {
        Pixel head;
        if (!file.read(reinterpret_cast<char*>(head.data()), 4)) return {};
        if (width >= 8 && width <= 32767 && head[0] == 2 && head[1] == 2 && !(head[2] & 128)) {
            if (head[2] * 256 + head[3] != width) return {};
            for (int channel = 0; channel < 4; ++channel) {
                for (int x = 0; x < width;) {
                    int code = file.get(), count = code > 128 ? code - 128 : code;
                    if (count <= 0 || count > width - x) return {};
                    int value = file.get();
                    while (count-- > 0) {
                        if (value < 0) return {};
                        row[x++][channel] = (uint8_t)value;
                        if (code <= 128 && count > 0) value = file.get();
                    }
                }
            }
        } else {
            int shift = 0;
            for (int x = 0; x < width;) {
                if (head[0] == 1 && head[1] == 1 && head[2] == 1) {
                    if (!x || shift >= 24) return {};
                    int count = int(head[3]) << shift;
                    if (count > width - x) return {};
                    Pixel previous = row[x - 1];
                    std::fill_n(row.begin() + x, count, previous);
                    x += count;
                    shift += 8;
                } else {
                    row[x++] = head;
                    shift = 0;
                }
                if (x < width && !file.read(reinterpret_cast<char*>(head.data()), 4)) return {};
            }
        }
        size_t offset = (size_t)(ya == "-Y" ? y : height - 1 - y) * width;
        for (int x = 0; x < width; ++x) {
            const Pixel& p = row[x];
            float unit = p[3] ? std::ldexp(1.f, int(p[3]) - 136) : 0;
            V3 c = V3(p[0], p[1], p[2]) * unit;
            image->px[offset + (xa == "+X" ? x : width - 1 - x)] = {std::min(c.x, 1e20f), std::min(c.y, 1e20f), std::min(c.z, 1e20f)};
        }
    }
    image->buildLightDistribution();
    return image;
}

static std::shared_ptr<Image> loadPGM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    auto skip = [&] {
        file >> std::ws;
        while (file.peek() == '#') {
            file.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            file >> std::ws;
        }
    };
    char magic[3]{};
    if (!file.read(magic, 2) || magic[0] != 'P' || (magic[1] != '2' && magic[1] != '5')) return {};
    int w = 0, h = 0, maxValue = 0;
    skip(); file >> w;
    skip(); file >> h;
    skip(); file >> maxValue;
    if (!file || w < 1 || h < 1 || (size_t)w * h > 33554432 || maxValue < 1 || maxValue > 65535) return {};
    if (magic[1] == '5') {
        int separator = file.get();
        if (separator < 0 || !std::isspace((unsigned char)separator)) return {};
        if (separator == '\r' && file.peek() == '\n') file.get();
    }
    auto image = std::make_shared<Image>();
    image->w = w;
    image->h = h;
    image->px.resize((size_t)w * h);
    for (V3& pixel : image->px) {
        int value = -1;
        if (magic[1] == '2') {
            skip();
            if (!(file >> value)) return {};
        } else {
            int first = file.get();
            if (first < 0) return {};
            value = first;
            if (maxValue > 255) {
                int low = file.get();
                if (low < 0) return {};
                value = first * 256 + low;
            }
        }
        if (value < 0 || value > maxValue) return {};
        pixel = V3((float)value / maxValue);
    }
    return image;
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }

V3 Image::sample(float u, float v) const {
    if (w <= 0 || h <= 0) return V3(0.f);
    u -= std::floor(u);
    v -= std::floor(v);
    float x = u * w - 0.5f, y = v * h - 0.5f, fx0 = std::floor(x), fy0 = std::floor(y), fx = x - fx0, fy = y - fy0;
    int x0 = ((int)fx0 % w + w) % w, y0 = ((int)fy0 % h + h) % h, x1 = (x0 + 1) % w, y1 = (y0 + 1) % h;
    return lerp(lerp(px[y0 * w + x0], px[y0 * w + x1], fx), lerp(px[y1 * w + x0], px[y1 * w + x1], fx), fy);
}

V3 Image::sampleClamped(float u, float v, bool nearest) const {
    if (w <= 0 || h <= 0) return {};
    float x = clampf(u * w - 0.5f, 0, (float)w - 1), y = clampf(v * h - 0.5f, 0, (float)h - 1);
    if (nearest) return px[(size_t)std::lround(y) * w + std::lround(x)];
    int x0 = (int)x, y0 = (int)y, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    return lerp(lerp(px[(size_t)y0 * w + x0], px[(size_t)y0 * w + x1], x - x0),
                lerp(px[(size_t)y1 * w + x0], px[(size_t)y1 * w + x1], x - x0), y - y0);
}

float Image::opacity(float u, float v) const {
    if (alpha.empty()) return 1;
    int x = std::clamp((int)(u * w), 0, w - 1), y = std::clamp((int)(v * h), 0, h - 1);
    return alpha[(size_t)y * w + x] / 255.f;
}

std::shared_ptr<Image> loadBMP(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> b;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        b.resize((size_t)sz);
        if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    }
    fclose(f);
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return nullptr;
    uint32_t off = rd32(&b[10]), dib = rd32(&b[14]), comp = rd32(&b[30]), ncol = rd32(&b[46]);
    int w = (int32_t)rd32(&b[18]), h = (int32_t)rd32(&b[22]), bpp = (int)rd16(&b[28]);
    if (w <= 0 || h == 0 || w > 16384 || h < -16384 || h > 16384) return nullptr;
    if (dib < 40 || (size_t)dib + 14 > off || off > b.size() || rd16(&b[26]) != 1) return nullptr;
    if (bpp != 8 && bpp != 24 && bpp != 32) return nullptr;
    if (!(comp == 0 || (comp == 3 && bpp == 32))) return nullptr;
    bool topDown = h < 0;
    h = std::abs(h);
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    if ((size_t)off + stride * h > b.size()) return nullptr;
    if (bpp == 8) {
        if (ncol == 0 || ncol > 256) ncol = 256;
        if ((size_t)14 + dib + ncol * 4 > off) return nullptr;
    }
    const uint8_t* pal = b.data() + 14 + dib;
    auto img = std::make_shared<Image>();
    img->w = w;
    img->h = h;
    img->px.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = &b[off + stride * (topDown ? y : h - 1 - y)];
        for (int x = 0; x < w; x++) {
            const uint8_t* p;
            if (bpp == 8) {
                uint32_t i = row[x];
                p = pal + (i < ncol ? i : 0) * 4;
            } else p = row + x * (bpp / 8);
            img->px[(size_t)y * w + x] = V3(p[2], p[1], p[0]) / 255.f;
        }
    }
    return img;
}

static std::string imageExtension(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return ext;
}

static std::shared_ptr<Image> loadTGA(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return {};
    std::unique_ptr<FILE, decltype(&fclose)> f(file, fclose);
    uint8_t header[18];
    if (fread(header, 1, sizeof header, file) != sizeof header || header[1]) return {};
    int type = header[2], w = rd16(header + 12), h = rd16(header + 14), bytes = header[16] / 8;
    bool gray = type == 3 || type == 11, rle = type == 10 || type == 11;
    if (w < 1 || h < 1 || (size_t)w * h > 64000000 || (!gray && type != 2 && type != 10) ||
        (gray ? bytes != 1 : (bytes != 3 && bytes != 4)) || fseek(file, header[0], SEEK_CUR)) return {};
    auto image = std::make_shared<Image>();
    image->w = w; image->h = h;
    size_t count = (size_t)w * h;
    image->px.resize(count);
    if (bytes == 4 && (header[17] & 15)) image->alpha.resize(count);
    for (size_t i = 0; i < count;) {
        int packet = rle ? fgetc(file) : 0;
        if (packet == EOF) return {};
        int n = (packet & 127) + 1;
        if (i + n > count) return {};
        uint8_t p[4];
        for (int j = 0; j < n; ++j, ++i) {
            if ((!j || !(packet & 128)) && fread(p, 1, bytes, file) != (size_t)bytes) return {};
            int x = (int)(i % w), y = (int)(i / w);
            if (header[17] & 16) x = w - 1 - x;
            if (!(header[17] & 32)) y = h - 1 - y;
            size_t at = (size_t)y * w + x;
            image->px[at] = (gray ? V3((float)p[0]) : V3(p[2], p[1], p[0])) / 255.f;
            if (!image->alpha.empty()) image->alpha[at] = p[3];
        }
    }
    return image;
}

static std::shared_ptr<Image> fromPixels(int w, int h, const uint8_t* bytes, int stride, int channels) {
    auto image = std::make_shared<Image>();
    image->w = w; image->h = h;
    image->px.resize((size_t)w * h);
    if (channels == 4) image->alpha.resize(image->px.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = bytes + (size_t)y * stride + x * channels;
            size_t at = (size_t)y * w + x;
            image->px[at] = V3(p[0], p[1], p[2]) / 255.f;
            if (channels == 4) image->alpha[at] = p[3];
        }
    if (std::all_of(image->alpha.begin(), image->alpha.end(), [](uint8_t a) { return a == 255; })) image->alpha.clear();
    return image;
}

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
struct Wic {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> factory;
    Wic() {
        if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE)
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    }
    ~Wic() {
        factory.Reset();
        if (SUCCEEDED(initialized)) CoUninitialize();
    }
};
}
#endif

std::shared_ptr<Image> loadImage(const std::string& path) {
    if (path == "builtin:survey-marker") {
        auto image = std::make_shared<Image>();
        image->w = 96;
        image->h = 64;
        image->px.resize(96 * 64);
        image->alpha.resize(96 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 96; ++x) {
                int dx = x - 48, dy = y - 31, d = dx * dx + dy * dy;
                size_t at = y * 96 + x;
                bool post = std::abs(dx) < 3 && y > 40;
                image->alpha[at] = d <= 27 * 27 || post ? 255 : 0;
                bool arrow = std::abs(dy) <= 3 && dx > -15 && dx < 13;
                arrow |= dx >= 5 && dx + std::abs(dy) <= 16;
                image->px[at] = arrow || post ? V3(0.08f, 0.12f, 0.13f) :
                    (d > 23 * 23 ? V3(0.94f, 0.87f, 0.72f) : V3(0.96f, 0.49f, 0.11f));
            }
        return image;
    }
    std::string ext = imageExtension(path);
    if (ext == ".hdr" || ext == ".rgbe" || ext == ".pic") return loadHDR(path);
    if (ext == ".pgm") return loadPGM(path);
    if (ext == ".bmp") return loadBMP(path);
    if (ext == ".tga") return loadTGA(path);
#ifdef _WIN32
    Wic wic;
    if (!wic.factory) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic.factory->CreateDecoderFromFilename(std::filesystem::path(path).c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(wic.factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom))) return {};
    UINT w = 0, h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || !w || !h || (size_t)w * h > 64000000) return {};
    WICPixelFormatGUID native;
    if (SUCCEEDED(frame->GetPixelFormat(&native)) && IsEqualGUID(native, GUID_WICPixelFormat16bppGray)) {
        std::vector<uint16_t> gray((size_t)w * h);
        if (FAILED(frame->CopyPixels(nullptr, w * 2, (UINT)(gray.size() * 2), reinterpret_cast<BYTE*>(gray.data())))) return {};
        auto image = std::make_shared<Image>();
        image->w = (int)w;
        image->h = (int)h;
        image->px.reserve(gray.size());
        for (uint16_t value : gray) image->px.emplace_back(value / 65535.f);
        return image;
    }
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()))) return {};
    return fromPixels((int)w, (int)h, pixels.data(), (int)w * 4, 4);
#else
    if (ext == ".tif" || ext == ".tiff") {
        std::unique_ptr<TIFF, decltype(&TIFFClose)> file(TIFFOpen(path.c_str(), "r"), TIFFClose);
        uint32_t w = 0, h = 0;
        if (!file || !TIFFGetField(file.get(), TIFFTAG_IMAGEWIDTH, &w) ||
            !TIFFGetField(file.get(), TIFFTAG_IMAGELENGTH, &h) || !w || !h || (size_t)w * h > 64000000) return {};
        uint16_t bits = 0, channels = 0, color = 0, orientation = 0, format = 0;
        TIFFGetFieldDefaulted(file.get(), TIFFTAG_BITSPERSAMPLE, &bits);
        TIFFGetFieldDefaulted(file.get(), TIFFTAG_SAMPLESPERPIXEL, &channels);
        TIFFGetFieldDefaulted(file.get(), TIFFTAG_ORIENTATION, &orientation);
        TIFFGetFieldDefaulted(file.get(), TIFFTAG_SAMPLEFORMAT, &format);
        TIFFGetField(file.get(), TIFFTAG_PHOTOMETRIC, &color);
        if (bits == 16 && channels == 1 && format == SAMPLEFORMAT_UINT && !TIFFIsTiled(file.get()) &&
            (color == PHOTOMETRIC_MINISBLACK || color == PHOTOMETRIC_MINISWHITE) && orientation >= 1 && orientation <= 4 &&
            TIFFScanlineSize(file.get()) == (tmsize_t)w * 2) {
            auto image = std::make_shared<Image>();
            image->w = (int)w;
            image->h = (int)h;
            image->px.resize((size_t)w * h);
            std::vector<uint16_t> row(w);
            for (uint32_t y = 0; y < h; ++y) {
                if (TIFFReadScanline(file.get(), row.data(), y, 0) < 0) return {};
                size_t offset = (size_t)(orientation >= 3 ? h - 1 - y : y) * w;
                for (uint32_t x = 0; x < w; ++x) {
                    float value = row[x] / 65535.f;
                    image->px[offset + ((orientation == 2 || orientation == 3) ? w - 1 - x : x)] =
                        V3(color == PHOTOMETRIC_MINISWHITE ? 1 - value : value);
                }
            }
            return image;
        }
        std::vector<uint32_t> raster((size_t)w * h);
        if (!TIFFReadRGBAImageOriented(file.get(), w, h, raster.data(), ORIENTATION_TOPLEFT, 1)) return {};
        auto image = std::make_shared<Image>();
        image->w = (int)w; image->h = (int)h;
        image->px.resize(raster.size()); image->alpha.resize(raster.size());
        for (size_t i = 0; i < raster.size(); ++i) {
            uint32_t p = raster[i];
            uint8_t a = TIFFGetA(p);
            image->alpha[i] = a;
            if (a) {
                V3 color = V3(TIFFGetR(p), TIFFGetG(p), TIFFGetB(p)) / a;
                image->px[i] = {clampf(color.x, 0, 1), clampf(color.y, 0, 1), clampf(color.z, 0, 1)};
            }
        }
        if (std::all_of(image->alpha.begin(), image->alpha.end(), [](uint8_t a) { return a == 255; })) image->alpha.clear();
        return image;
    }
    GdkPixbuf* buf = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
    if (!buf) return {};
    int w = gdk_pixbuf_get_width(buf), h = gdk_pixbuf_get_height(buf);
    auto image = (size_t)w * h <= 64000000 ? fromPixels(w, h, gdk_pixbuf_get_pixels(buf),
        gdk_pixbuf_get_rowstride(buf), gdk_pixbuf_get_n_channels(buf)) : nullptr;
    g_object_unref(buf);
    return image;
#endif
}

bool saveHeightTiff(const std::string& path, int width, int height, const std::vector<float>& heights) {
    if (width < 1 || height < 1 || heights.size() != (size_t)width * height || heights.size() > 64000000) return false;
    std::vector<uint16_t> values;
    values.reserve(heights.size());
    for (float v : heights) values.push_back((uint16_t)std::lround(clampf(v, 0, 1) * 65535.f));
#ifdef _WIN32
    Wic wic;
    if (!wic.factory) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    WICPixelFormatGUID format = GUID_WICPixelFormat16bppGray;
    return SUCCEEDED(wic.factory->CreateStream(stream.GetAddressOf())) &&
        SUCCEEDED(stream->InitializeFromFilename(std::filesystem::path(path).c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic.factory->CreateEncoder(GUID_ContainerFormatTiff, nullptr, encoder.GetAddressOf())) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf())) &&
        SUCCEEDED(frame->Initialize(properties.Get())) && SUCCEEDED(frame->SetSize(width, height)) &&
        SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat16bppGray) &&
        SUCCEEDED(frame->WritePixels(height, width * 2, (UINT)(values.size() * 2), reinterpret_cast<BYTE*>(values.data()))) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
#else
    std::unique_ptr<TIFF, decltype(&TIFFClose)> file(TIFFOpen(path.c_str(), "w"), TIFFClose);
    if (!file) return false;
    TIFF* tiff = file.get();
    if (!TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, (uint32_t)width) || !TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, (uint32_t)height) ||
        !TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1) || !TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) ||
        !TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) || !TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) || !TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW)) return false;
    for (int y = 0; y < height; ++y)
        if (TIFFWriteScanline(tiff, values.data() + (size_t)y * width, (uint32_t)y, 0) < 0) return false;
    return TIFFWriteDirectory(tiff) != 0;
#endif
}

bool saveRaster(const std::string& path, int w, int h, const std::vector<uint32_t>& px, bool alpha) {
    if (w < 1 || h < 1 || px.size() != (size_t)w * h || px.size() > 64000000) return false;
    std::string ext = imageExtension(path);
    bool png = ext == ".png";
    if (!png && ext != ".tiff" && ext != ".tif") return false;
    int channels = alpha ? 4 : 3;
    std::vector<uint8_t> bytes(px.size() * channels);
    for (size_t i = 0; i < px.size(); ++i) {
        uint32_t p = px[i];
        bytes[i * channels] = (uint8_t)(p >> 16);
        bytes[i * channels + 1] = (uint8_t)(p >> 8);
        bytes[i * channels + 2] = (uint8_t)p;
        if (alpha) bytes[i * channels + 3] = (uint8_t)(p >> 24);
    }
#ifdef _WIN32
    Wic wic;
    if (!wic.factory) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    ComPtr<IWICBitmap> bitmap;
    const GUID& sourceFormat = alpha ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat24bppRGB;
    if (FAILED(wic.factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromFilename(std::filesystem::path(path).c_str(), GENERIC_WRITE)) ||
        FAILED(wic.factory->CreateEncoder(png ? GUID_ContainerFormatPng : GUID_ContainerFormatTiff, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf())) ||
        FAILED(frame->Initialize(properties.Get())) || FAILED(frame->SetSize(w, h)) ||
        FAILED(wic.factory->CreateBitmapFromMemory(w, h, sourceFormat, w * channels, (UINT)bytes.size(), bytes.data(), bitmap.GetAddressOf()))) return false;
    WICPixelFormatGUID format = sourceFormat;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    ComPtr<IWICFormatConverter> converter;
    IWICBitmapSource* source = bitmap.Get();
    if (!IsEqualGUID(format, sourceFormat)) {
        if (FAILED(wic.factory->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(source, format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
        source = converter.Get();
    }
    return SUCCEEDED(frame->WriteSource(source, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
#else
    if (!png) {
        std::unique_ptr<TIFF, decltype(&TIFFClose)> file(TIFFOpen(path.c_str(), "w"), TIFFClose);
        if (!file) return false;
        TIFF* tiff = file.get();
        if (!TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, (uint32_t)w) || !TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, (uint32_t)h) ||
            !TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, channels) || !TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8) ||
            !TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) || !TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
            !TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) || !TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW)) return false;
        if (alpha) {
            uint16_t tag = EXTRASAMPLE_UNASSALPHA;
            if (!TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, 1, &tag)) return false;
        }
        for (int y = 0; y < h; ++y)
            if (TIFFWriteScanline(tiff, bytes.data() + (size_t)y * w * channels, (uint32_t)y, 0) < 0) return false;
        return TIFFWriteDirectory(tiff) != 0;
    }
    GdkPixbuf* bitmap = gdk_pixbuf_new_from_data(bytes.data(), GDK_COLORSPACE_RGB, alpha, 8, w, h, w * channels, nullptr, nullptr);
    if (!bitmap) return false;
    bool ok = gdk_pixbuf_save(bitmap, path.c_str(), "png", nullptr, nullptr);
    g_object_unref(bitmap);
    return ok;
#endif
}

std::shared_ptr<const Image> loadImageCached(const std::string& path, bool refresh) {
    static std::mutex mu;
    static std::map<std::string, std::shared_ptr<const Image>> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(path);
    if (!refresh && it != cache.end()) return it->second;
    auto img = loadImage(path);
    if (img || it == cache.end()) cache[path] = img;
    return img;
}

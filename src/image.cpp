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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>

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
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()))) return {};
    return fromPixels((int)w, (int)h, pixels.data(), (int)w * 4, 4);
#else
    if (ext == ".tif" || ext == ".tiff") {
        std::unique_ptr<TIFF, decltype(&TIFFClose)> file(TIFFOpen(path.c_str(), "r"), TIFFClose);
        uint32_t w = 0, h = 0;
        if (!file || !TIFFGetField(file.get(), TIFFTAG_IMAGEWIDTH, &w) ||
            !TIFFGetField(file.get(), TIFFTAG_IMAGELENGTH, &h) || !w || !h || (size_t)w * h > 64000000) return {};
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

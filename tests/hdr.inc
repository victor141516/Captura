// Synthetic FP16 scRGB buffers only; desktop duplication itself needs real HDR hardware.
template <typename Check> void testHdr(Check check) {
    using namespace captura;
    bool halves = true;
    for (float value : {0.f, 1.f, 0.5f, 3.5f, 65504.f, -2.25f, 0.000123f, 6.0e-6f})
        if (std::abs(halfToFloat(floatToHalf(value)) - value) > std::abs(value) * 0.001f + 6e-8f)
            halves = false;
    check(halves && halfToFloat(0x3c00) == 1.f && halfToFloat(0xc000) == -2.f &&
              halfToFloat(0x0001) > 0,
          L"FP16 decoding covers normal, negative and subnormal values");

    bool curve = hdrToneMapNits(150, 200, 280) == 150 && hdrToneMapNits(400, 200, 280) == 280 &&
                 std::abs(hdrToneMapNits(1000, 1000, 280) - 280) < 0.5f &&
                 std::abs(hdrToneMapNits(40, 1000, 280) - 40) < 0.2f;
    float previous = 0;
    for (float nits = 1; nits <= 1200; nits += 1) {
        float mapped = hdrToneMapNits(nits, 1000, 280);
        if (mapped < previous || mapped > 280)
            curve = false;
        previous = mapped;
    }
    check(curve, L"Tone curve keeps dark tones, rolls off monotonically and lands peaks on SDR "
                 L"white");

    const float white = 280, units = white / 80;
    auto srgbLinear = [](int code) {
        double c = code / 255.0;
        return float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
    };
    auto encodeLinear = [](float linear) {
        double c = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
        return int(std::lround(std::clamp(c, 0.0, 1.0) * 255));
    };
    // One row per test pixel; a 1-pixel border must stay untouched.
    const int width = 260, height = 3;
    std::vector<uint16_t> source(size_t(width) * height * 4);
    std::vector<uint8_t> target(size_t(width) * height * 4, 7);
    auto put = [&](int x, int y, float r, float g, float b) {
        uint16_t* p = &source[(size_t(y) * width + x) * 4];
        p[0] = floatToHalf(r);
        p[1] = floatToHalf(g);
        p[2] = floatToHalf(b);
        p[3] = floatToHalf(1);
    };
    auto out = [&](int x, int y) { return &target[(size_t(y) * width + x) * 4]; };
    for (int code = 0; code < 256; ++code)
        put(code + 1, 1, srgbLinear(code) * units, srgbLinear(255 - code) * units,
            srgbLinear(code / 2) * units);
    RECT inner{1, 1, 257, 2};
    bool mapped =
        toneMapRegion(source.data(), width, &inner, 1, white, 465, target.data(), width * 4);
    bool identical = !mapped;
    for (int code = 0; code < 256; ++code) {
        const uint8_t* p = out(code + 1, 1);
        if (p[2] != code || p[1] != 255 - code || p[0] != code / 2 || p[3] != 255)
            identical = false;
    }
    check(identical, L"SDR content in an HDR capture reproduces the original 8-bit values");
    check(out(0, 1)[0] == 7 && out(257, 1)[0] == 7 && out(1, 0)[0] == 7 && out(1, 2)[0] == 7,
          L"HDR conversion writes only the requested region");

    // HDR region: a bright warm color, a full-peak white, a dark tone and a wide-gamut green.
    std::fill(target.begin(), target.end(), uint8_t{7});
    for (int x = 1; x < 257; ++x)
        put(x, 1, 0.5f * units, 0.5f * units, 0.5f * units);
    put(1, 1, 4 * units, 4 * units, 4 * units);
    put(2, 1, 1.5f * units, 0.75f * units, 0.375f * units);
    put(3, 1, 0.05f * units, 0.05f * units, 0.05f * units);
    put(4, 1, -0.4f * units, 1.6f * units, -0.2f * units);
    for (int x = 5; x < 257; ++x)
        put(x, 1, 4 * units, 4 * units, 4 * units);
    mapped = toneMapRegion(source.data(), width, &inner, 1, white, 0, target.data(), width * 4);
    const uint8_t *peak = out(1, 1), *warm = out(2, 1), *dark = out(3, 1), *wide = out(4, 1);
    check(mapped && peak[0] == 255 && peak[1] == 255 && peak[2] == 255,
          L"Content above SDR white is tone-mapped so its peak becomes white");
    float red = srgbLinear(warm[2]), green = srgbLinear(warm[1]), blue = srgbLinear(warm[0]);
    check(warm[2] < 255 && std::abs(green / red - 0.5f) < 0.02f &&
              std::abs(blue / red - 0.25f) < 0.02f,
          L"Highlight compression preserves hue instead of clipping channels to yellow/white");
    check(std::abs(dark[0] - encodeLinear(0.05f)) <= 1,
          L"Dark HDR tones below the roll-off knee keep their brightness");
    float luma = 0.2126f * srgbLinear(wide[2]) + 0.7152f * srgbLinear(wide[1]) +
                 0.0722f * srgbLinear(wide[0]);
    check(wide[2] == 0 && wide[1] > wide[0] && wide[0] > 0 && luma > 0.4f,
          L"Wide-gamut colors desaturate toward their luminance instead of clipping");

    // Display peak caps the measured source peak: anything the monitor cannot show is white.
    std::fill(target.begin(), target.end(), uint8_t{7});
    for (int x = 1; x < 257; ++x)
        put(x, 1, (x % 2 ? 4 : 2) * units, (x % 2 ? 4 : 2) * units, (x % 2 ? 4 : 2) * units);
    toneMapRegion(source.data(), width, &inner, 1, white, 2 * white, target.data(), width * 4);
    check(out(1, 1)[1] == 255 && out(2, 1)[1] == 255,
          L"Source peak is capped at the display's maximum luminance");

    // A specular glint below the 99.9th percentile does not darken the rest of the region.
    const int big = 2000;
    std::vector<uint16_t> glint(size_t(big) * 4);
    std::vector<uint8_t> glintOut(size_t(big) * 4);
    for (int x = 0; x < big; ++x) {
        float v = (x == 0 ? 10 : 0.8f) * units, tint = x == 0 ? 0.5f : 1;
        glint[x * 4] = floatToHalf(v);
        glint[x * 4 + 1] = floatToHalf(v * tint);
        glint[x * 4 + 2] = floatToHalf(v * tint);
    }
    RECT row{0, 0, big, 1};
    mapped = toneMapRegion(glint.data(), big, &row, 1, white, 0, glintOut.data(), big * 4);
    check(!mapped && glintOut[4 + 1] == encodeLinear(0.8f) && glintOut[2] == 255 &&
              std::abs(srgbLinear(glintOut[1]) - 0.5f) < 0.02f,
          L"Rare glints keep SDR exposure and clip without changing hue");
}

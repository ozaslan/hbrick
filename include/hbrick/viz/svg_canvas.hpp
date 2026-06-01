#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hbrick {

class SvgCanvas {
public:
    SvgCanvas(uint32_t width_pixels, uint32_t height_pixels);

    [[nodiscard]] uint32_t widthPixels() const noexcept { return width_pixels_; }
    [[nodiscard]] uint32_t heightPixels() const noexcept { return height_pixels_; }

    void clear() noexcept;

    void line(double x1, double y1, double x2, double y2, std::string_view stroke = "#000000");
    void rect(
        double x,
        double y,
        double width,
        double height,
        std::string_view fill,
        std::string_view stroke = "#000000"
    );
    void text(double x, double y, std::string_view content);

    [[nodiscard]] std::string toString() const;

private:
    [[nodiscard]] static std::string escapeXml(std::string_view input);

    uint32_t width_pixels_ = 0;
    uint32_t height_pixels_ = 0;
    std::string body_;
};

}  // namespace hbrick

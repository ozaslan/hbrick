#include "hbrick/viz/svg_canvas.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace hbrick {

SvgCanvas::SvgCanvas(const uint32_t width_pixels, const uint32_t height_pixels)
    : width_pixels_(width_pixels), height_pixels_(height_pixels) {}

void SvgCanvas::clear() noexcept {
    body_.clear();
}

void SvgCanvas::line(
    const double x1,
    const double y1,
    const double x2,
    const double y2,
    const std::string_view stroke,
    const double stroke_width
) {
    std::ostringstream stream;
    stream << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2
           << "\" stroke=\"" << stroke << "\" stroke-width=\"" << stroke_width
           << "\" stroke-linecap=\"round\"/>\n";
    body_ += stream.str();
}

void SvgCanvas::rect(
    const double x,
    const double y,
    const double width,
    const double height,
    const std::string_view fill,
    const std::string_view stroke
) {
    std::ostringstream stream;
    stream << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\""
           << height << "\" fill=\"" << fill << "\" stroke=\"" << stroke << "\"/>\n";
    body_ += stream.str();
}

void SvgCanvas::text(const double x, const double y, const std::string_view content) {
    const std::string escaped = escapeXml(content);
    std::ostringstream stream;
    stream << "<text x=\"" << x << "\" y=\"" << y << "\">" << escaped << "</text>\n";
    body_ += stream.str();
}

void SvgCanvas::triangle(
    const double x1,
    const double y1,
    const double x2,
    const double y2,
    const double x3,
    const double y3,
    const std::string_view fill
) {
    std::ostringstream stream;
    stream << "<polygon points=\"" << x1 << "," << y1 << " " << x2 << "," << y2 << " " << x3
           << "," << y3 << "\" fill=\"" << fill << "\"/>\n";
    body_ += stream.str();
}

bool SvgCanvas::writeTo(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << toString();
    return static_cast<bool>(out);
}

std::string SvgCanvas::toString() const {
    std::ostringstream stream;
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width_pixels_
           << "\" height=\"" << height_pixels_ << "\" viewBox=\"0 0 " << width_pixels_
           << " " << height_pixels_ << "\">\n";
    stream << body_;
    stream << "</svg>\n";
    return stream.str();
}

std::string SvgCanvas::escapeXml(const std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char character : input) {
        switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped += character;
                break;
        }
    }

    return escaped;
}

}  // namespace hbrick

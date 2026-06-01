/**
 * @file svg_canvas.hpp
 * @ingroup hbrick_viz
 * @brief Minimal SVG document builder for tests and visualization.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hbrick {

/**
 * @brief Incrementally constructs a simple SVG document in memory.
 * @ingroup hbrick_viz
 *
 * Supports lines, filled rectangles, and text. Text content is XML-escaped
 * automatically. Intended for lightweight graph rendering rather than full
 * vector-graphics authoring.
 */
class SvgCanvas {
public:
    /**
     * @brief Creates a canvas with the given pixel dimensions.
     * @ingroup hbrick_viz
     *
     * @param width_pixels SVG viewport width.
     * @param height_pixels SVG viewport height.
     */
    SvgCanvas(uint32_t width_pixels, uint32_t height_pixels);

    /** @brief Returns the viewport width in pixels. @return The viewport width in pixels. @ingroup hbrick_viz */
    [[nodiscard]] uint32_t widthPixels() const noexcept { return width_pixels_; }
    /** @brief Returns the viewport height in pixels. @return The viewport height in pixels. @ingroup hbrick_viz */
    [[nodiscard]] uint32_t heightPixels() const noexcept { return height_pixels_; }

    /** @brief Removes all drawn primitives while retaining canvas dimensions. @ingroup hbrick_viz */
    void clear() noexcept;

    /**
     * @brief Appends a line segment.
     * @ingroup hbrick_viz
     *
     * @param x1 Start x coordinate.
     * @param y1 Start y coordinate.
     * @param x2 End x coordinate.
     * @param y2 End y coordinate.
     * @param stroke CSS color string for the stroke attribute.
     */
    void line(double x1, double y1, double x2, double y2, std::string_view stroke = "#000000");

    /**
     * @brief Appends a filled rectangle.
     * @ingroup hbrick_viz
     *
     * @param x Top-left x coordinate.
     * @param y Top-left y coordinate.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param fill CSS color string for the fill attribute.
     * @param stroke CSS color string for the stroke attribute.
     */
    void rect(
        double x,
        double y,
        double width,
        double height,
        std::string_view fill,
        std::string_view stroke = "#000000"
    );

    /**
     * @brief Appends text at (@p x, @p y).
     * @ingroup hbrick_viz
     *
     * @param x Text anchor x coordinate.
     * @param y Text anchor y coordinate.
     * @param content Text payload; special XML characters are escaped.
     */
    void text(double x, double y, std::string_view content);

    /**
     * @brief Serializes the complete SVG document.
     * @ingroup hbrick_viz
     *
     * @return XML string including the root @c svg element and all primitives.
     */
    [[nodiscard]] std::string toString() const;

private:
    /** @brief Escapes characters that are special in XML text nodes. @ingroup hbrick_viz */
    [[nodiscard]] static std::string escapeXml(std::string_view input);

    uint32_t width_pixels_ = 0;
    uint32_t height_pixels_ = 0;
    std::string body_;
};

}  // namespace hbrick

#include "hbrick/io/movingai_loader.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <vector>

namespace hbrick {

namespace {

/** @brief Strips trailing carriage returns and whitespace from one line. */
std::string_view trimRight(std::string_view line) {
    while (!line.empty()
           && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1U);
    }
    return line;
}

/** @brief Reads the next line from @p text starting at @p offset. */
bool nextLine(const std::string_view text, std::size_t& offset, std::string_view& line) {
    if (offset >= text.size()) {
        return false;
    }
    const std::size_t end = text.find('\n', offset);
    if (end == std::string_view::npos) {
        line = trimRight(text.substr(offset));
        offset = text.size();
    } else {
        line = trimRight(text.substr(offset, end - offset));
        offset = end + 1U;
    }
    return true;
}

/** @brief Parses "key value" where value is a non-negative integer. */
bool parseDimensionField(
    const std::string_view line,
    const std::string_view key,
    uint32_t& out
) {
    if (line.size() <= key.size() + 1U || line.substr(0U, key.size()) != key
        || line[key.size()] != ' ') {
        return false;
    }
    const std::string_view value = line.substr(key.size() + 1U);
    const auto [ptr, ec] =
        std::from_chars(value.data(), value.data() + value.size(), out);
    return ec == std::errc{} && ptr == value.data() + value.size();
}

}  // namespace

MovingAiLoadResult parseMovingAiMap(const std::string_view text) {
    MovingAiLoadResult result;

    std::string type_name;
    uint32_t width = 0;
    uint32_t height = 0;
    bool saw_map_marker = false;

    std::size_t offset = 0;
    std::string_view line;
    while (nextLine(text, offset, line)) {
        if (line == "map") {
            saw_map_marker = true;
            break;
        }
        if (line.substr(0U, 5U) == "type ") {
            type_name = std::string(line.substr(5U));
            continue;
        }
        uint32_t value = 0;
        if (parseDimensionField(line, "height", value)) {
            height = value;
            continue;
        }
        if (parseDimensionField(line, "width", value)) {
            width = value;
            continue;
        }
        if (!line.empty()) {
            result.error = "unrecognized header line: '" + std::string(line) + "'";
            return result;
        }
    }

    if (!saw_map_marker) {
        result.error = "missing 'map' header terminator";
        return result;
    }
    if (width == 0 || height == 0) {
        result.error = "header is missing a positive width or height";
        return result;
    }

    std::vector<char> cells;
    cells.reserve(static_cast<std::size_t>(width) * height);

    for (uint32_t row = 0; row < height; ++row) {
        if (!nextLine(text, offset, line)) {
            std::ostringstream message;
            message << "unexpected end of data at row " << row << " of " << height;
            result.error = message.str();
            return result;
        }
        if (line.size() < width) {
            std::ostringstream message;
            message << "row " << row << " has " << line.size()
                    << " cells, expected at least " << width;
            result.error = message.str();
            return result;
        }
        cells.insert(cells.end(), line.begin(), line.begin() + width);
    }

    result.map = MovingAiMap(
        GridDimensions{width, height},
        std::move(type_name),
        std::move(cells)
    );
    return result;
}

MovingAiLoadResult loadMovingAiMap(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        MovingAiLoadResult result;
        result.error = "cannot open file: " + path.string();
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string content = buffer.str();

    MovingAiLoadResult result = parseMovingAiMap(content);
    if (!result.ok()) {
        result.error = path.string() + ": " + result.error;
    }
    return result;
}

}  // namespace hbrick

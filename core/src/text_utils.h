// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

/// Utilidades internas de core para texto UTF-8. No es una cabecera pública.
namespace esceq::core::detail {

/// `text` sin espacios en blanco al principio ni al final.
inline std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

/// Recorta `text` a `maxBytes` bytes como mucho, sin dejar a medias un
/// carácter UTF-8 de varios bytes.
inline void truncateUtf8(std::string& text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return;
    text.resize(maxBytes);

    // Se busca el primer byte del último carácter y se mira cuántos debería tener.
    std::size_t start = text.size();
    while (start > 0 && (static_cast<unsigned char>(text[start - 1]) & 0xC0) == 0x80)
        --start;
    if (start == 0)
        return; // solo bytes de continuación: no es UTF-8 válido, se deja como está
    const auto lead = static_cast<unsigned char>(text[start - 1]);
    const std::size_t expected = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
    if (text.size() - (start - 1) < expected)
        text.resize(start - 1);
}

} // namespace esceq::core::detail

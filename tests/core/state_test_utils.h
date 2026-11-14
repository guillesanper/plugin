// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"

#include <cstddef>
#include <sstream>
#include <string>

namespace esceq::test
{

/// Parámetros que había cuando se escribieron los fixtures de estado de la
/// versión 2 (los de la 0.1).
inline constexpr std::size_t kParametersInV2Fixtures = 338;

/// `text` (un estado) sin las líneas de los parámetros con índice
/// `firstAdded` o mayor: los que se añadieron a la tabla después de escribir
/// un fixture. Un parámetro nuevo se escribe con su valor por defecto; así no
/// oculta un cambio en el resto del formato.
inline std::string withoutParametersAddedAfter(const std::string& text, std::size_t firstAdded)
{
    std::istringstream in(text);
    std::string out;
    for (std::string line; std::getline(in, line);)
    {
        const auto index = core::findParameter(line.substr(0, line.find(' ')));
        if (index && *index >= firstAdded)
            continue;
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace esceq::test

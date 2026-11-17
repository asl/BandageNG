// Copyright 2026 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage-NG is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage-NG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "program/llvm.h"

#include <string>
#include <utility>

namespace bandage {

// A failure shown to the user: a short title and a message.
class TextError : public ErrorInfo<TextError> {
public:
    inline static char ID = 0;

    TextError(std::string title, std::string text)
            : m_title(std::move(title)), m_text(std::move(text)) {}

    void log(raw_ostream &os) const override { os << message(); }

    std::string message() const override { return m_title + ": " + m_text; }

    std::error_code convertToErrorCode() const override {
        return inconvertibleErrorCode();
    }

    [[nodiscard]] const std::string &title() const { return m_title; }
    [[nodiscard]] const std::string &text() const { return m_text; }

private:
    std::string m_title;
    std::string m_text;
};

// TextError keeps its title and sentence. Any other error is reported through
// ErrorInfoBase::message() so an unexpected type does not abort.
template <typename TextFn, typename OtherFn>
void reportError(Error err, TextFn &&onText, OtherFn &&onOther) {
    handleAllErrors(std::move(err), std::forward<TextFn>(onText),
                    [&](const ErrorInfoBase &other) { onOther(other.message()); });
}

} // namespace bandage

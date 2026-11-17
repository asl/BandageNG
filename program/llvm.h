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

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Error.h"

namespace bandage {

using llvm::Error;
using llvm::ErrorInfoBase;
template <typename T>
using Expected = llvm::Expected<T>;
template <typename ThisErrT, typename ParentErrT = llvm::ErrorInfoBase>
using ErrorInfo = llvm::ErrorInfo<ThisErrT, ParentErrT>;

using llvm::consumeError;
using llvm::createStringError;
using llvm::handleAllErrors;
using llvm::inconvertibleErrorCode;
using llvm::make_error;
using llvm::raw_ostream;
using llvm::toString;
using llvm::Twine;

using llvm::SmallVector;
using llvm::make_range;

} // namespace bandage

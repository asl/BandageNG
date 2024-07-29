//===--- raw_ostream.cpp - Implement the raw_ostream classes --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements support for bulk buffered stream output.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringExtras.h"
//#include "llvm/Support/AutoConvert.h"
#include "llvm/Support/Compiler.h"
//#include "llvm/Support/Duration.h"
#include "llvm/Support/ErrorHandling.h"
//#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/NativeFormatting.h"
//#include "llvm/Support/Process.h"
//#include "llvm/Support/Program.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

// <fcntl.h> may provide O_BINARY.
#if defined(HAVE_FCNTL_H)
# include <fcntl.h>
#endif

#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#endif

#if defined(__CYGWIN__)
#include <io.h>
#endif

#if defined(_MSC_VER)
#include <io.h>
#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO 2
#endif
#endif

#ifdef _WIN32
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#endif

using namespace llvm;

raw_ostream::~raw_ostream() {
  // raw_ostream's subclasses should take care to flush the buffer
  // in their destructors.
  assert(OutBufCur == OutBufStart &&
         "raw_ostream destructor called with non-empty buffer!");

  if (BufferMode == BufferKind::InternalBuffer)
    delete [] OutBufStart;
}

size_t raw_ostream::preferred_buffer_size() const {
#ifdef _WIN32
  // On Windows BUFSIZ is only 512 which results in more calls to write. This
  // overhead can cause significant performance degradation. Therefore use a
  // better default.
  return (16 * 1024);
#else
  // BUFSIZ is intended to be a reasonable default.
  return BUFSIZ;
#endif
}

void raw_ostream::SetBuffered() {
  // Ask the subclass to determine an appropriate buffer size.
  if (size_t Size = preferred_buffer_size())
    SetBufferSize(Size);
  else
    // It may return 0, meaning this stream should be unbuffered.
    SetUnbuffered();
}

void raw_ostream::SetBufferAndMode(char *BufferStart, size_t Size,
                                   BufferKind Mode) {
  assert(((Mode == BufferKind::Unbuffered && !BufferStart && Size == 0) ||
          (Mode != BufferKind::Unbuffered && BufferStart && Size != 0)) &&
         "stream must be unbuffered or have at least one byte");
  // Make sure the current buffer is free of content (we can't flush here; the
  // child buffer management logic will be in write_impl).
  assert(GetNumBytesInBuffer() == 0 && "Current buffer is non-empty!");

  if (BufferMode == BufferKind::InternalBuffer)
    delete [] OutBufStart;
  OutBufStart = BufferStart;
  OutBufEnd = OutBufStart+Size;
  OutBufCur = OutBufStart;
  BufferMode = Mode;

  assert(OutBufStart <= OutBufEnd && "Invalid size!");
}

raw_ostream &raw_ostream::operator<<(unsigned long N) {
  write_integer(*this, static_cast<uint64_t>(N), 0, IntegerStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::operator<<(long N) {
  write_integer(*this, static_cast<int64_t>(N), 0, IntegerStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::operator<<(unsigned long long N) {
  write_integer(*this, static_cast<uint64_t>(N), 0, IntegerStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::operator<<(long long N) {
  write_integer(*this, static_cast<int64_t>(N), 0, IntegerStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::write_hex(unsigned long long N) {
  llvm::write_hex(*this, N, HexPrintStyle::Lower);
  return *this;
}

raw_ostream &raw_ostream::write_uuid(const uuid_t UUID) {
  for (int Idx = 0; Idx < 16; ++Idx) {
    *this << format("%02" PRIX32, UUID[Idx]);
    if (Idx == 3 || Idx == 5 || Idx == 7 || Idx == 9)
      *this << "-";
  }
  return *this;
}


raw_ostream &raw_ostream::write_escaped(StringRef Str,
                                        bool UseHexEscapes) {
  for (unsigned char c : Str) {
    switch (c) {
    case '\\':
      *this << '\\' << '\\';
      break;
    case '\t':
      *this << '\\' << 't';
      break;
    case '\n':
      *this << '\\' << 'n';
      break;
    case '"':
      *this << '\\' << '"';
      break;
    default:
      if (isPrint(c)) {
        *this << c;
        break;
      }

      // Write out the escaped representation.
      if (UseHexEscapes) {
        *this << '\\' << 'x';
        *this << hexdigit((c >> 4) & 0xF);
        *this << hexdigit((c >> 0) & 0xF);
      } else {
        // Always use a full 3-character octal escape.
        *this << '\\';
        *this << char('0' + ((c >> 6) & 7));
        *this << char('0' + ((c >> 3) & 7));
        *this << char('0' + ((c >> 0) & 7));
      }
    }
  }

  return *this;
}

raw_ostream &raw_ostream::operator<<(const void *P) {
  llvm::write_hex(*this, (uintptr_t)P, HexPrintStyle::PrefixLower);
  return *this;
}

raw_ostream &raw_ostream::operator<<(double N) {
  llvm::write_double(*this, N, FloatStyle::Exponent);
  return *this;
}

void raw_ostream::flush_nonempty() {
  assert(OutBufCur > OutBufStart && "Invalid call to flush_nonempty.");
  size_t Length = OutBufCur - OutBufStart;
  OutBufCur = OutBufStart;
  write_impl(OutBufStart, Length);
}

raw_ostream &raw_ostream::write(unsigned char C) {
  // Group exceptional cases into a single branch.
  if (LLVM_UNLIKELY(OutBufCur >= OutBufEnd)) {
    if (LLVM_UNLIKELY(!OutBufStart)) {
      if (BufferMode == BufferKind::Unbuffered) {
        write_impl(reinterpret_cast<char *>(&C), 1);
        return *this;
      }
      // Set up a buffer and start over.
      SetBuffered();
      return write(C);
    }

    flush_nonempty();
  }

  *OutBufCur++ = C;
  return *this;
}

raw_ostream &raw_ostream::write(const char *Ptr, size_t Size) {
  // Group exceptional cases into a single branch.
  if (LLVM_UNLIKELY(size_t(OutBufEnd - OutBufCur) < Size)) {
    if (LLVM_UNLIKELY(!OutBufStart)) {
      if (BufferMode == BufferKind::Unbuffered) {
        write_impl(Ptr, Size);
        return *this;
      }
      // Set up a buffer and start over.
      SetBuffered();
      return write(Ptr, Size);
    }

    size_t NumBytes = OutBufEnd - OutBufCur;

    // If the buffer is empty at this point we have a string that is larger
    // than the buffer. Directly write the chunk that is a multiple of the
    // preferred buffer size and put the remainder in the buffer.
    if (LLVM_UNLIKELY(OutBufCur == OutBufStart)) {
      assert(NumBytes != 0 && "undefined behavior");
      size_t BytesToWrite = Size - (Size % NumBytes);
      write_impl(Ptr, BytesToWrite);
      size_t BytesRemaining = Size - BytesToWrite;
      if (BytesRemaining > size_t(OutBufEnd - OutBufCur)) {
        // Too much left over to copy into our buffer.
        return write(Ptr + BytesToWrite, BytesRemaining);
      }
      copy_to_buffer(Ptr + BytesToWrite, BytesRemaining);
      return *this;
    }

    // We don't have enough space in the buffer to fit the string in. Insert as
    // much as possible, flush and start over with the remainder.
    copy_to_buffer(Ptr, NumBytes);
    flush_nonempty();
    return write(Ptr + NumBytes, Size - NumBytes);
  }

  copy_to_buffer(Ptr, Size);

  return *this;
}

void raw_ostream::copy_to_buffer(const char *Ptr, size_t Size) {
  assert(Size <= size_t(OutBufEnd - OutBufCur) && "Buffer overrun!");

  // Handle short strings specially, memcpy isn't very good at very short
  // strings.
  switch (Size) {
  case 4: OutBufCur[3] = Ptr[3]; [[fallthrough]];
  case 3: OutBufCur[2] = Ptr[2]; [[fallthrough]];
  case 2: OutBufCur[1] = Ptr[1]; [[fallthrough]];
  case 1: OutBufCur[0] = Ptr[0]; [[fallthrough]];
  case 0: break;
  default:
    memcpy(OutBufCur, Ptr, Size);
    break;
  }

  OutBufCur += Size;
}

// Formatted output.
raw_ostream &raw_ostream::operator<<(const format_object_base &Fmt) {
  // If we have more than a few bytes left in our output buffer, try
  // formatting directly onto its end.
  size_t NextBufferSize = 127;
  size_t BufferBytesLeft = OutBufEnd - OutBufCur;
  if (BufferBytesLeft > 3) {
    size_t BytesUsed = Fmt.print(OutBufCur, BufferBytesLeft);

    // Common case is that we have plenty of space.
    if (BytesUsed <= BufferBytesLeft) {
      OutBufCur += BytesUsed;
      return *this;
    }

    // Otherwise, we overflowed and the return value tells us the size to try
    // again with.
    NextBufferSize = BytesUsed;
  }

  // If we got here, we didn't have enough space in the output buffer for the
  // string.  Try printing into a SmallVector that is resized to have enough
  // space.  Iterate until we win.
  SmallVector<char, 128> V;

  while (true) {
    V.resize(NextBufferSize);

    // Try formatting into the SmallVector.
    size_t BytesUsed = Fmt.print(V.data(), NextBufferSize);

    // If BytesUsed fit into the vector, we win.
    if (BytesUsed <= NextBufferSize)
      return write(V.data(), BytesUsed);

    // Otherwise, try again with a new size.
    assert(BytesUsed > NextBufferSize && "Didn't grow buffer!?");
    NextBufferSize = BytesUsed;
  }
}

raw_ostream &raw_ostream::operator<<(const formatv_object_base &Obj) {
  Obj.format(*this);
  return *this;
}

raw_ostream &raw_ostream::operator<<(const FormattedString &FS) {
  unsigned LeftIndent = 0;
  unsigned RightIndent = 0;
  const ssize_t Difference = FS.Width - FS.Str.size();
  if (Difference > 0) {
    switch (FS.Justify) {
    case FormattedString::JustifyNone:
      break;
    case FormattedString::JustifyLeft:
      RightIndent = Difference;
      break;
    case FormattedString::JustifyRight:
      LeftIndent = Difference;
      break;
    case FormattedString::JustifyCenter:
      LeftIndent = Difference / 2;
      RightIndent = Difference - LeftIndent;
      break;
    }
  }
  indent(LeftIndent);
  (*this) << FS.Str;
  indent(RightIndent);
  return *this;
}

raw_ostream &raw_ostream::operator<<(const FormattedNumber &FN) {
  if (FN.Hex) {
    HexPrintStyle Style;
    if (FN.Upper && FN.HexPrefix)
      Style = HexPrintStyle::PrefixUpper;
    else if (FN.Upper && !FN.HexPrefix)
      Style = HexPrintStyle::Upper;
    else if (!FN.Upper && FN.HexPrefix)
      Style = HexPrintStyle::PrefixLower;
    else
      Style = HexPrintStyle::Lower;
    llvm::write_hex(*this, FN.HexValue, Style, FN.Width);
  } else {
    llvm::SmallString<16> Buffer;
    llvm::raw_svector_ostream Stream(Buffer);
    llvm::write_integer(Stream, FN.DecValue, 0, IntegerStyle::Integer);
    if (Buffer.size() < FN.Width)
      indent(FN.Width - Buffer.size());
    (*this) << Buffer;
  }
  return *this;
}

raw_ostream &raw_ostream::operator<<(const FormattedBytes &FB) {
  if (FB.Bytes.empty())
    return *this;

  size_t LineIndex = 0;
  auto Bytes = FB.Bytes;
  const size_t Size = Bytes.size();
  HexPrintStyle HPS = FB.Upper ? HexPrintStyle::Upper : HexPrintStyle::Lower;
  uint64_t OffsetWidth = 0;
  if (FB.FirstByteOffset) {
    // Figure out how many nibbles are needed to print the largest offset
    // represented by this data set, so that we can align the offset field
    // to the right width.
    size_t Lines = Size / FB.NumPerLine;
    uint64_t MaxOffset = *FB.FirstByteOffset + Lines * FB.NumPerLine;
    unsigned Power = 0;
    if (MaxOffset > 0)
      Power = llvm::Log2_64_Ceil(MaxOffset);
    OffsetWidth = std::max<uint64_t>(4, llvm::alignTo(Power, 4) / 4);
  }

  // The width of a block of data including all spaces for group separators.
  unsigned NumByteGroups =
      alignTo(FB.NumPerLine, FB.ByteGroupSize) / FB.ByteGroupSize;
  unsigned BlockCharWidth = FB.NumPerLine * 2 + NumByteGroups - 1;

  while (!Bytes.empty()) {
    indent(FB.IndentLevel);

    if (FB.FirstByteOffset) {
      uint64_t Offset = *FB.FirstByteOffset;
      llvm::write_hex(*this, Offset + LineIndex, HPS, OffsetWidth);
      *this << ": ";
    }

    auto Line = Bytes.take_front(FB.NumPerLine);

    size_t CharsPrinted = 0;
    // Print the hex bytes for this line in groups
    for (size_t I = 0; I < Line.size(); ++I, CharsPrinted += 2) {
      if (I && (I % FB.ByteGroupSize) == 0) {
        ++CharsPrinted;
        *this << " ";
      }
      llvm::write_hex(*this, Line[I], HPS, 2);
    }

    if (FB.ASCII) {
      // Print any spaces needed for any bytes that we didn't print on this
      // line so that the ASCII bytes are correctly aligned.
      assert(BlockCharWidth >= CharsPrinted);
      indent(BlockCharWidth - CharsPrinted + 2);
      *this << "|";

      // Print the ASCII char values for each byte on this line
      for (uint8_t Byte : Line) {
        if (isPrint(Byte))
          *this << static_cast<char>(Byte);
        else
          *this << '.';
      }
      *this << '|';
    }

    Bytes = Bytes.drop_front(Line.size());
    LineIndex += Line.size();
    if (LineIndex < Size)
      *this << '\n';
  }
  return *this;
}

template <char C>
static raw_ostream &write_padding(raw_ostream &OS, unsigned NumChars) {
  static const char Chars[] = {C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C};

  // Usually the indentation is small, handle it with a fastpath.
  if (NumChars < std::size(Chars))
    return OS.write(Chars, NumChars);

  while (NumChars) {
    unsigned NumToWrite = std::min(NumChars, (unsigned)std::size(Chars) - 1);
    OS.write(Chars, NumToWrite);
    NumChars -= NumToWrite;
  }
  return OS;
}

/// indent - Insert 'NumSpaces' spaces.
raw_ostream &raw_ostream::indent(unsigned NumSpaces) {
  return write_padding<' '>(*this, NumSpaces);
}

/// write_zeros - Insert 'NumZeros' nulls.
raw_ostream &raw_ostream::write_zeros(unsigned NumZeros) {
  return write_padding<'\0'>(*this, NumZeros);
}

void raw_ostream::anchor() {}

//===----------------------------------------------------------------------===//
//  Formatted Output
//===----------------------------------------------------------------------===//

// Out of line virtual method.
void format_object_base::home() {
}

//===----------------------------------------------------------------------===//
//  outs(), errs(), nulls()
//===----------------------------------------------------------------------===//

/// nulls() - This returns a reference to a raw_ostream which discards output.
raw_ostream &llvm::nulls() {
  static raw_null_ostream S;
  return S;
}

//===----------------------------------------------------------------------===//
//  raw_string_ostream
//===----------------------------------------------------------------------===//

void raw_string_ostream::write_impl(const char *Ptr, size_t Size) {
  OS.append(Ptr, Size);
}

//===----------------------------------------------------------------------===//
//  raw_svector_ostream
//===----------------------------------------------------------------------===//

uint64_t raw_svector_ostream::current_pos() const { return OS.size(); }

void raw_svector_ostream::write_impl(const char *Ptr, size_t Size) {
  OS.append(Ptr, Ptr + Size);
}

void raw_svector_ostream::pwrite_impl(const char *Ptr, size_t Size,
                                      uint64_t Offset) {
  memcpy(OS.data() + Offset, Ptr, Size);
}

bool raw_svector_ostream::classof(const raw_ostream *OS) {
  return OS->get_kind() == OStreamKind::OK_SVecStream;
}

//===----------------------------------------------------------------------===//
//  raw_null_ostream
//===----------------------------------------------------------------------===//

raw_null_ostream::~raw_null_ostream() {
#ifndef NDEBUG
  // ~raw_ostream asserts that the buffer is empty. This isn't necessary
  // with raw_null_ostream, but it's better to have raw_null_ostream follow
  // the rules than to change the rules just for raw_null_ostream.
  flush();
#endif
}

void raw_null_ostream::write_impl(const char *Ptr, size_t Size) {
}

uint64_t raw_null_ostream::current_pos() const {
  return 0;
}

void raw_null_ostream::pwrite_impl(const char *Ptr, size_t Size,
                                   uint64_t Offset) {}

void raw_pwrite_stream::anchor() {}

void buffer_ostream::anchor() {}

void buffer_unique_ostream::anchor() {}

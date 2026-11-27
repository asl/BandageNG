//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "log.hpp"
#include "nucl.hpp"
#include "utils/sfinae_checks.hpp"

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/PointerIntPair.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/Support/TrailingObjects.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Silence bogus gcc warnings
#if defined(__GCC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif

// The short representation stores its flag in the top byte of the second
// word. That byte is the top byte of a heap pointer for a long sequence, so
// this only works when pointers are little-endian and their top byte is zero.
#if defined(__BYTE_ORDER__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "short sequences require a little-endian platform");
#elif !defined(_MSC_VER)
#error "short sequences require a known little-endian platform"
#endif

class Sequence {
    // Type to store Seq in Sequences
    typedef uint64_t ST;
    // Number of bits in ST
    const static size_t STBits = sizeof(ST) << 3;
    // Number of nucleotides in ST
    const static size_t STN = (STBits >> 1);
    // Number of bits in STN (for faster div and mod)
    const static size_t STNBits = log_<STN, 2>::value;

    // Owns the optional SparseBitVector. The all-N flag shares that pointer
    // via PointerIntPair: null+0 is no N's, a heap pointer is sparse N's,
    // null+1 is every base N. PointerIntPair does not own the pointer.
    class EmptyNucls {
        llvm::PointerIntPair<llvm::SparseBitVector<> *, 1, bool> pair_;

      public:
        EmptyNucls() = default;
        ~EmptyNucls() { delete pair_.getPointer(); }

        EmptyNucls(const EmptyNucls &) = delete;
        EmptyNucls &operator=(const EmptyNucls &) = delete;

        EmptyNucls(EmptyNucls &&other) noexcept : pair_(other.pair_) {
            other.pair_.setPointerAndInt(nullptr, false);
        }

        EmptyNucls &operator=(EmptyNucls &&other) noexcept {
            if (this != &other) {
                delete pair_.getPointer();
                pair_ = other.pair_;
                other.pair_.setPointerAndInt(nullptr, false);
            }
            return *this;
        }

        bool allNs() const { return pair_.getInt(); }

        // No recorded N's. This is the common case.
        bool none() const { return pair_.getPointer() == nullptr && !pair_.getInt(); }

        llvm::SparseBitVector<> *bits() const { return pair_.getPointer(); }

        // Safe when there is no bitvector: all-N is true, no N's is false.
        bool test(size_t idx) const {
            if (llvm::SparseBitVector<> *bits = pair_.getPointer())
                return bits->test(idx);
            return pair_.getInt();
        }

        // Every base of a sequence of this length is N.
        bool covers(size_t size) const {
            if (pair_.getInt())
                return true;
            llvm::SparseBitVector<> *bits = pair_.getPointer();
            return bits != nullptr && bits->count() == size;
        }

        bool equals(const EmptyNucls &that) const {
            if (allNs() || that.allNs())
                return allNs() && that.allNs();
            llvm::SparseBitVector<> *aBits = pair_.getPointer();
            llvm::SparseBitVector<> *bBits = that.pair_.getPointer();
            return aBits == bBits || (aBits != nullptr && bBits != nullptr && *aBits == *bBits);
        }

        void setAllNs() {
            delete pair_.getPointer();
            pair_.setPointerAndInt(nullptr, true);
        }

        void set(size_t idx) {
            VERIFY(!pair_.getInt());
            if (pair_.getPointer() == nullptr)
                pair_.setPointerAndInt(new llvm::SparseBitVector<>(), false);
            pair_.getPointer()->set(idx);
        }
    };

    static_assert(sizeof(EmptyNucls) == sizeof(void *),
                  "all-N flag must share the bitvector pointer");

    class ManagedNuclBuffer final : public llvm::ThreadSafeRefCountedBase<ManagedNuclBuffer>,
                                    protected llvm::TrailingObjects<ManagedNuclBuffer, ST> {
        friend TrailingObjects;

        ManagedNuclBuffer() {}

        ManagedNuclBuffer(size_t nucls, ST *buf) {
            std::uninitialized_copy(buf, buf + Sequence::DataSize(nucls), data());
        }

      public:
        void operator delete(void *p) { ::operator delete(p); }

        static ManagedNuclBuffer *create(size_t nucls) {
            void *mem = ::operator new(totalSizeToAlloc<ST>(Sequence::DataSize(nucls)));
            return new (mem) ManagedNuclBuffer();
        }

        static ManagedNuclBuffer *createAllNs() {
            void *mem = ::operator new(totalSizeToAlloc<ST>(0));
            auto *buf = new (mem) ManagedNuclBuffer();
            buf->empty_nucls_.setAllNs();
            return buf;
        }

        // Header plus the 2-bit words. An all-N buffer asks for no words.
        static size_t bytesFor(size_t words) { return totalSizeToAlloc<ST>(words); }

        static ManagedNuclBuffer *create(size_t nucls, ST *data) {
            void *mem = ::operator new(totalSizeToAlloc<ST>(Sequence::DataSize(nucls)));
            return new (mem) ManagedNuclBuffer(nucls, data);
        }

        const ST *data() const { return getTrailingObjects<ST>(); }
        ST *data() { return getTrailingObjects<ST>(); }

        EmptyNucls empty_nucls_;
    };

    // Nucl buffers are words, low bases in the low bits, unused top bits zero:
    //   |0000111122223333|000011112222....|
    // A long sequence is two such words: (size:32, from:31, rtl:1) and a
    // refcount pointer. A short ACGT sequence reuses those 16 bytes in the
    // same word layout, so the rest of the code can keep reading words:
    //   |0000111122223333|000011112222, metadata:8|
    // The metadata byte is size (6 bits), rtl, and a non-zero short flag.
    // 120 bits hold 60 bases. That byte is also the top of the pointer word.
    // User-space pointers here have a zero top byte, so is_short() is
    // "top byte != 0". Short sequences are copied, never shared. A window
    // that does not start at 0 is stored long, so Subseq stays a view.
    // Any N, including all-N, stays long; the inline form has no room for one.
    struct LongRep {
        size_t size_ : 32;
        size_t from_ : 31;
        // BANDAGE_LOCAL begin
        // A bool bit-field is a separate allocation unit in MSVC, so it
        // would not share this word and LongRep would be 24 bytes.
        size_t rtl_ : 1;
        // BANDAGE_LOCAL end
        llvm::IntrusiveRefCntPtr<ManagedNuclBuffer> data_;

        LongRep() : size_(0), from_(0), rtl_(false), data_(nullptr) {}

        LongRep(size_t size, size_t from, bool rtl, ManagedNuclBuffer *data)
                : size_(size), from_(from), rtl_(rtl), data_(data) {
            assert(data == nullptr || (reinterpret_cast<uintptr_t>(data) >> 56) == 0);
        }

        bool rtl() const { return rtl_; }
        size_t from() const { return from_; }
        size_t size() const { return size_; }
    };

    enum { ShortSize = sizeof(LongRep) / sizeof(ST),
           MetadataOffset = (sizeof(ST) - 1) * 8,
           MetadataMask = 0xFF,
           DataMask = (ST(1) << MetadataOffset) - 1,
           RTLOffset = 1, RTLMask = 0x1,
           SizeOffset = 2, SizeMask = 0x3F };

    static_assert(sizeof(ST) == 8, "short sequences assume 64-bit words");
    static_assert(sizeof(LongRep) == 16, "long sequence representation must be 16 bytes");

    struct ShortRep {
        ST data[ShortSize];

        ShortRep() : data{} {}

        uint8_t metadata() const {
            return (data[ShortSize - 1] >> MetadataOffset) & MetadataMask;
        }
        void set_metadata(size_t size, bool rtl) {
            uint8_t meta = static_cast<uint8_t>(((size & SizeMask) << SizeOffset) | (rtl << RTLOffset) | 1);
            data[ShortSize - 1] = (data[ShortSize - 1] & DataMask) | (ST(meta) << MetadataOffset);
        }
        void set_metadata(uint8_t meta) {
            data[ShortSize - 1] = (data[ShortSize - 1] & DataMask) | (ST(meta) << MetadataOffset);
        }

        bool rtl() const { return (metadata() >> RTLOffset) & RTLMask; }
        size_t size() const { return (metadata() >> SizeOffset) & SizeMask; }
        size_t from() const { return 0; }
    };

    // One byte is metadata, so 120 bits / 2 = 60 bases.
    static constexpr size_t ShortBits = (sizeof(ShortRep) - 1) << 3;
    static constexpr size_t ShortN = ShortBits >> 1;

    static_assert(SizeMask >= ShortN, "short size does not fit in the metadata byte");

    union Rep {
        LongRep l;
        ShortRep s;

        // The active member is not known yet, and LongRep / ShortRep have no
        // common initial sequence, so naming either member here is undefined.
        // The object representation is the remaining way to read the byte.
        // Little-endian puts it last: top of the pointer when long, the short
        // flag otherwise.
        uint8_t metadata() const {
            const auto *bytes = reinterpret_cast<const uint8_t *>(this);
            return bytes[sizeof(Rep) - 1];
        }
        bool is_short() const { return metadata() != 0; }

        Rep() noexcept : l() {}
        ~Rep() noexcept { if (!is_short()) l.~LongRep(); }

        Rep(const Rep &rhs) noexcept : l() {
            if (rhs.is_short()) {
                l.~LongRep();
                new (&s) ShortRep(rhs.s);
            } else {
                l = rhs.l;
            }
        }
        Rep(Rep &&rhs) noexcept : l() {
            if (rhs.is_short()) {
                l.~LongRep();
                new (&s) ShortRep(rhs.s);
            } else {
                l = std::move(rhs.l);
            }
        }

        Rep &operator=(const Rep &rhs) noexcept {
            if (this == &rhs)
                return *this;

            bool thisShort = is_short();
            if (rhs.is_short()) {
                if (!thisShort)
                    l.~LongRep();
                if (thisShort)
                    s = rhs.s;
                else
                    new (&s) ShortRep(rhs.s);
            } else if (thisShort) {
                new (&l) LongRep(rhs.l);
            } else {
                l = rhs.l;
            }
            return *this;
        }
        Rep &operator=(Rep &&rhs) noexcept {
            if (this == &rhs)
                return *this;

            bool thisShort = is_short();
            if (rhs.is_short()) {
                if (!thisShort)
                    l.~LongRep();
                if (thisShort)
                    s = rhs.s;
                else
                    new (&s) ShortRep(rhs.s);
            } else if (thisShort) {
                new (&l) LongRep(std::move(rhs.l));
            } else {
                l = std::move(rhs.l);
            }
            return *this;
        }
    };

    Rep rep_;

    enum class Storage { Short, LongBases, AllNs };
    struct Counted {};

    static bool fitsShort(size_t size) { return size <= ShortN; }

    static size_t DataSize(size_t size) {
        return (size + STN - 1) >> STNBits;
    }

    bool is_short() const { return rep_.is_short(); }

    size_t from() const { return is_short() ? rep_.s.from() : rep_.l.from(); }
    bool rtl() const { return is_short() ? rep_.s.rtl() : rep_.l.rtl(); }

    const ST *bytes() const { return is_short() ? rep_.s.data : rep_.l.data_->data(); }
    ST *bytes() { return is_short() ? rep_.s.data : rep_.l.data_->data(); }

    uint8_t metadata() const { return rep_.metadata(); }
    void set_metadata(uint8_t meta) { rep_.s.set_metadata(meta); }

    void adoptLong(size_t size, size_t from, bool rtl, ManagedNuclBuffer *data) {
        rep_.l.~LongRep();
        new (&rep_.l) LongRep(size, from, rtl, data);
    }

    Sequence(size_t size, Storage storage) {
        if (storage == Storage::Short) {
            rep_.l.~LongRep();
            new (&rep_.s) ShortRep();
            rep_.s.set_metadata(size, false);
            return;
        }
        ManagedNuclBuffer *data = storage == Storage::AllNs
                                      ? ManagedNuclBuffer::createAllNs()
                                      : ManagedNuclBuffer::create(size);
        adoptLong(size, 0, false, data);
    }

    template<typename S>
    static bool containsN(const S &s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (is_N(s[i]))
                return true;
        }
        return false;
    }

    template<typename S>
    Sequence(Counted, const S &s, size_t n, bool rc)
            : Sequence(n, (containsN(s, n) || !fitsShort(n)) ? Storage::LongBases : Storage::Short) {
        if (n > 0)
            InitFromNucls(s, rc);
    }

    template<typename S>
    void InitEmptyNucls(const S &s, bool rc) {
        int int_size = static_cast<int>(size());
        int start = rc ? int_size - 1 : 0;
        int end = rc ? -1 : int_size;
        int step = rc ? -1 : 1;
        int cur_index = 0;

        for (int i = start; i != end; i += step) {
            if (LLVM_UNLIKELY(is_N(s[i])))
                rep_.l.data_->empty_nucls_.set(cur_index);
            cur_index++;
        }
    }

    // Packing writes whole words and would clear the short metadata byte.
    // The caller saves that byte and puts it back.
    template<typename S>
    void packNucls(const S &s, size_t n, bool rc) {
        size_t words = DataSize(n);
        ST *dest = bytes();

        VERIFY(is_dignucl(s[0]) || is_nucl(s[0]) || is_N(s[0]));

        bool digit_str = is_dignucl(s[0]);

        ST data = 0;
        size_t cnt = 0;
        size_t cur = 0;

        if (rc) {
            for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
                char c = complement(digit_str ? s[static_cast<unsigned>(i)]
                                              : dignucl(s[static_cast<unsigned>(i)]));
                data = data | (ST(c) << cnt);
                cnt += 2;
                if (cnt == STBits) {
                    dest[cur++] = data;
                    cnt = 0;
                    data = 0;
                }
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                char c = digit_str ? s[i] : dignucl(s[i]);
                data = data | (ST(c) << cnt);
                cnt += 2;
                if (cnt == STBits) {
                    dest[cur++] = data;
                    cnt = 0;
                    data = 0;
                }
            }
        }

        if (cnt != 0)
            dest[cur++] = data;
        for (; cur < words; ++cur)
            dest[cur] = 0;
    }

    template<typename S>
    void InitFromNucls(const S &s, bool rc) {
        if (!is_short())
            InitEmptyNucls(s, rc);

        uint8_t saved = is_short() ? metadata() : 0;
        packNucls(s, size(), rc);
        if (saved)
            set_metadata(saved);
    }

    bool isEmptySymbol(size_t idx) const {
        if (LLVM_LIKELY(rep_.l.data_->empty_nucls_.none()))
            return false;
        return rep_.l.data_->empty_nucls_.test(idx);
    }

    char getNuclFromBuffer(size_t idx) const {
        const ST *data = bytes();
        return static_cast<char>((data[idx >> STNBits] >> ((idx & (STN - size_t{1})) << size_t{1})) & size_t{3});
    }

    // Low level constructor. A long source shares its buffer. A short source
    // is copied, and a window that does not start at 0 is promoted to a long
    // buffer so Subseq stays a view.
    Sequence(const Sequence &seq, size_t from, size_t size, bool rtl) {
        if (!seq.is_short()) {
            rep_.l.size_ = size;
            rep_.l.from_ = from;
            rep_.l.rtl_ = rtl;
            rep_.l.data_ = seq.rep_.l.data_;
            return;
        }

        if (from != seq.from()) {
            VERIFY(seq.from() == 0);
            size_t seqSize = seq.size();
            adoptLong(size, from, rtl, ManagedNuclBuffer::create(seqSize));
            size_t destWords = DataSize(seqSize);
            ST *dest = rep_.l.data_->data();
            memcpy(dest, seq.bytes(), destWords * sizeof(ST));
            if (destWords == ShortSize)
                dest[ShortSize - 1] &= DataMask;
            return;
        }

        rep_.l.~LongRep();
        new (&rep_.s) ShortRep(seq.rep_.s);
        rep_.s.set_metadata(size, rtl);
    }

public:
    explicit Sequence(size_t size, bool allNs = false)
            : Sequence(size, allNs ? Storage::AllNs
                                   : (fitsShort(size) ? Storage::Short : Storage::LongBases)) {}

    /**
     * Sequence initialization (arbitrary size string)
     *
     * @param s ACGT or 0123-string
     */
    explicit Sequence(const char *s, bool rc = false)
            : Sequence(Counted{}, s, strlen(s), rc) {}

    explicit Sequence(char *s, bool rc = false)
            : Sequence(static_cast<const char *>(s), rc) {}

    template<typename S, typename = std::enable_if_t<has_size_method<S>::value>>
    explicit Sequence(const S &s, bool rc = false)
            : Sequence(Counted{}, s, s.size(), rc) {}

    Sequence()
            : Sequence(size_t{0}) {
        uint8_t saved = metadata();
        if (size_t nbytes = DataSize(size()) * sizeof(ST))
            memset(bytes(), 0, nbytes);
        if (saved)
            set_metadata(saved);
    }

    Sequence(const Sequence &s)
            : Sequence(s, s.from(), s.size(), s.rtl()) {}

    Sequence(Sequence &&) noexcept = default;

    Sequence &operator=(const Sequence &) noexcept = default;
    Sequence &operator=(Sequence &&) noexcept = default;

    char operator[](const size_t index) const {
        VERIFY_DEV(index < size());
        size_t i = rtl() ? from() + size() - 1 - index : from() + index;
        if (!is_short() && LLVM_UNLIKELY(isEmptySymbol(i)))
            return 'N';
        char base = getNuclFromBuffer(i);
        if (rtl())
            base = complement(base);
        return nucl(base);
    }

    bool operator==(const Sequence &that) const {
        if (size() != that.size())
            return false;

        if (!is_short() && !that.is_short()) {
            if (rep_.l.data_->empty_nucls_.allNs() && that.rep_.l.data_->empty_nucls_.allNs())
                return true;
            if (rep_.l.data_ == that.rep_.l.data_ &&
                rep_.l.from_ == that.rep_.l.from_ &&
                rep_.l.rtl_ == that.rep_.l.rtl_ &&
                rep_.l.data_->empty_nucls_.equals(that.rep_.l.data_->empty_nucls_))
                return true;
        }

        for (size_t i = 0; i < size(); ++i) {
            if (this->operator[](i) != that[i])
                return false;
        }
        return true;
    }

    bool operator!=(const Sequence &that) const {
        return !(operator==(that));
    }

    /**
     * @todo Might be optimized via int comparison (not so easy)
     */
    bool operator<(const Sequence &that) const {
        size_t s = std::min(size(), that.size());
        for (size_t i = 0; i < s; ++i) {
            if (this->operator[](i) != that[i])
                return this->operator[](i) < that[i];
        }
        return size() < that.size();
    }

    Sequence GetReverseComplement() const {
        return {*this, from(), size(), !rtl()};
    }

    inline Sequence operator<<(char c) const;

    /**
     * @param from inclusive
     * @param to exclusive;
     */
    inline Sequence Subseq(size_t from, size_t to) const;

    inline Sequence Subseq(size_t from) const; // up to size_ by default

    inline Sequence operator+(const Sequence &s) const;

    inline Sequence First(size_t count) const;

    inline Sequence Last(size_t count) const;

    inline size_t find(const Sequence &t, size_t from = 0) const;

    template<class Seq>
    Seq start(size_t k) const;

    template<class Seq>
    Seq end(size_t k) const;

    inline std::string str() const;

    inline std::string err() const;

    size_t size() const { return is_short() ? rep_.s.size() : rep_.l.size(); }

    // Heap bytes of the 2-bit buffer. Inline sequences and all-N report 0.
    size_t capacity() const {
        if (is_short())
            return 0;
        if (LLVM_LIKELY(!rep_.l.data_->empty_nucls_.allNs()))
            return DataSize(size()) * sizeof(ST);
        return 0;
    }

    // The heap allocation for this sequence, header included. An inline
    // ACGT sequence reports 0. An all-N sequence is the header alone.
    size_t allocatedBytes() const {
        if (is_short() || !rep_.l.data_)
            return 0;
        const size_t words = rep_.l.data_->empty_nucls_.allNs() ? 0 : DataSize(size());
        return ManagedNuclBuffer::bytesFor(words);
    }

    bool empty() const { return size() == 0; }

    bool missing() const {
        if (is_short())
            return false;
        if (LLVM_LIKELY(rep_.l.data_->empty_nucls_.none()))
            return false;
        return rep_.l.data_->empty_nucls_.covers(size());
    }

    template<class Seq>
    bool contains(const Seq& s, size_t offset = 0) const {
        VERIFY_DEV(offset + s.size() <= size());

        for (size_t i = 0, e = s.size(); i != e; ++i)
            if (operator[](offset + i) != s[i])
                return false;

        return true;
    }
};

static_assert(sizeof(Sequence) == 16, "Sequence must stay 16 bytes");

inline std::ostream &operator<<(std::ostream &os, const Sequence &s);

template<class Seq>
Seq Sequence::start(size_t k) const {
    return Seq(unsigned(k), *this);
}

template<class Seq>
Seq Sequence::end(size_t k) const {
    return Seq(unsigned(k), *this, size() - k);
}

// O(1) for a long sequence, which shares its buffer. A short sequence whose
// window does not start at 0 is promoted to a long buffer.
//including from, excluding to
//safe if not #DEFINE NDEBUG
Sequence Sequence::Subseq(size_t from, size_t to) const {
    VERIFY(to >= from);
    VERIFY(to <= size());
    if (rtl())
        return {*this, this->from() + size() - to, to - from, true};
    return {*this, this->from() + from, to - from, false};
}

//including from, excluding to
Sequence Sequence::Subseq(size_t from) const {
    return Subseq(from, size());
}

Sequence Sequence::First(size_t count) const {
    return Subseq(0, count);
}

Sequence Sequence::Last(size_t count) const {
    return Subseq(size() - count);
}

/**
 * @todo : must be KMP or hashing instead of this
 */
size_t Sequence::find(const Sequence &t, size_t from) const {
    for (size_t i = from; i <= size() - t.size(); i++) {
        if (Subseq(i, i + t.size()) == t)
            return i;
    }
    return -1ULL;
}

/**
 * @todo optimize
 */
Sequence Sequence::operator+(const Sequence &s) const {
    return Sequence(str() + s.str());
}

std::string Sequence::str() const {
    std::string res(size(), '-');
    for (size_t i = 0; i < size(); ++i)
        res[i] = nucl(this->operator[](i));
    return res;
}

std::string Sequence::err() const {
    std::ostringstream oss;
    if (is_short()) {
        oss << "{ short seq *data=" << static_cast<const void *>(bytes())
            << ", from_=" << from()
            << ", size_=" << size()
            << ", rtl_=" << int(rtl()) << " }";
        return oss.str();
    }
    oss << "{ *data=" << rep_.l.data_->data()
        << ", from_=" << from()
        << ", size_=" << size()
        << ", rtl_=" << int(rtl())
        << ", empty_nucls_=" << rep_.l.data_->empty_nucls_.bits()
        << ", allNs=" << int(rep_.l.data_->empty_nucls_.allNs()) << " }";
    return oss.str();
}

std::ostream &operator<<(std::ostream &os, const Sequence &s) {
    os << s.str();
    return os;
}

#if defined(__GCC__)
#pragma GCC diagnostic pop
#endif

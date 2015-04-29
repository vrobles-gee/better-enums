/// @file EnumInternal.h
/// Internal definitions for the enum type generator in `Enum.h`.
///
/// Several definitions must precede the public `ENUM` macro and the interface
/// defined in it. This includes helper classes and all `constexpr` functions,
/// which cannot be forward-declared. In order to make `Enum.h` more readable,
/// these definitions are placed into this file, which is included from
/// `Enum.h`.
///
/// Throughout the internal code, macro and template parameters named `EnumType`
/// stand for the class types generated by the `ENUM` macro, while parameters
/// named `EnumValue` stand for the internal C++ enum types. Roughly,
/// `EnumValue == EnumType::_Value`.
///
/// @todo Consider simplifying compile-time function signatures by combining
///     arguments that don't change into a single `constexpr` object.
/// @todo There is a way to perform all computation on the names and values
///     arrays in a single pass, by requiring that all the special constants
///     (such as `_bad`) appear at the end, and working back to front. It's not
///     clear what kind of performance improvement this will give, as the
///     current passes are already pretty fast, and the compile time is
///     dominated by parsing and type checking of other code.
/// @todo It's possible that reducing the number of redundant array accesses
///     will improve compile time, but a stand-alone test suggests that the cost
///     of these accesses is very small.
/// @todo Generating the values array using the `_eat_assign` template is
///     expensive, and the cost seems to be due to the instantiation of
///     compile-time objects, not due to templates. Trying statement expressions
///     (a GNU extension) didn't work, because statement expressions aren't
///     allowed "at file scope" (in this case, within a class type declared at
///     file scope).
/// @todo `_enum::_special_names::_find` can terminate early after finding all
///     four special names' indices.
/// @todo Compile time is currently dominated by the cost of static
///     instantiation. Try to reduce this cost by statically instantiating data
///     structures for each type, then dynamically passing them to a small
///     number of actual processing functions - which only have to be
///     instantiated once for every different underlying type. Underlying types
///     are very likely to collide.

// TODO Make it possible for enums to be map keys.
// TODO Rename internal functions to match public interface conventions.



#pragma once

#include <cstddef>          // For size_t.
#include <cstring>          // For string and memory routines.
#include <stdexcept>
#include <type_traits>

#include "EnumPreprocessorMap.h"



/// Internal namespace for compile-time and private run-time functions used by
/// the enum class generator.
namespace _enum {



/// Weak symbols to allow the same data structures to be defined statically in
/// multiple translation units, then be collapsed to one definition by the
/// linker.
#define _ENUM_WEAK      __attribute__((weak))



// Forward declaration of _Internal, for use in a friend declation in _Iterable.
template <typename EnumType> class _Implementation;

// TODO Simplify - names iteration will need a custom iterator if processing
// strings lazily, while values can be done by simply iterating over the values
// array directly.
/// Template for iterable objects over enum names and values.
///
/// The iterables are intended for use with C++11 `for-each` syntax. They are
/// returned by each enum type's static `names()` and `values()` methods. For
/// example, `EnumType::values()` is an iterable over valid values of type
/// `EnumType`, and allows the following form:
///
/// ~~~{.cc}
/// for (EnumType e : EnumType::values()) {
///     // ...
/// }
/// ~~~
///
/// The iterable class is templated to reuse code between the name and value
/// iterables.
///
/// @tparam Element Type of element returned during iteration: either the enum
///     type (for iterables over `values()`) or `const char*` (for iterables
///     over `names()`).
/// @tparam EnumType The enum type.
/// @tparam ArrayType Type of the array actually being iterated over. The reason
///     this is a type parameter is because for the iterable over `values()`,
///     the underlying array type is `const EnumType::_value * const`, instead
///     of `const EnumType * const`, as one might first expect. Objects of type
///     `EnumType` are constructed on the fly during iteration from values of
///     type `EnumType::_value` (this is a no-op at run-time). For iterables
///     over `names()`, `ArrayType` is simply `const char * const`, as would be
///     expeted.
///
/// @todo Consider making `_Iterable` `constexpr`.
///
/// @internal
///
/// An `_Iterable` stores a reference to the array (of either names or values)
/// that will be iterated over. `_Iterable::iterator` additionally stores an
/// index into the array. The iterator begins at the first valid index. Each
/// time it is incremented, the iterator advances to the next valid index. The
/// `end()` iterator stores an index equal to the size of the array. Values are
/// considered valid if they are not equal to the bad value, are not below the
/// minimum value, and are not above the maximum value. Names are valid if they
/// are the name of a valid value.
template <typename Element, typename EnumType, typename ArrayType>
class _Iterable {
  public:
    /// Iterators for iterating over enum names or values.
    class iterator {
      public:
        /// Returns the current name or value.
        Element operator *() const { return (Element)_arrayPointer[_index]; }

        /// Advances the iterator to the next valid name or value. If there is
        /// no such value, the iterator becomes equal to the result of
        /// `_Iterable::end()`.
        /// @return A reference to itself.
        iterator& operator ++()
        {
            if (_index < EnumType::_size)
                ++_index;

            return *this;
        }

        /// Compares two iterators for equality.
        /// @param other Another iterator over the same array.
        bool operator ==(const iterator &other) const
        {
            return (other._arrayPointer == _arrayPointer) &&
                   (other._index == _index);
        }

        /// Compares two iterators for equality - negated comparison.
        /// @param other Another iterator over the same array.
        bool operator !=(const iterator &other) const
            { return !(*this == other); }

      public:
        /// An iterator can be declared without initialization - in this case,
        /// its state is undefined.
        iterator() = default;

      private:
        /// Constructs an iterator over the given array, with the given starting
        /// index. This method is used only be the enclosing `_Iterable` class.
        /// @param arrayPointer Array that will be iterated over.
        /// @param index Initial index into the array. This must be the index of
        ///     a valid value.
        iterator(ArrayType arrayPointer, size_t index) :
            _arrayPointer(arrayPointer), _index(index) { }

        /// Reference to the array being iterated.
        ArrayType       _arrayPointer;
        /// Current index into the array. This is always either the index of a
        /// valid value or else it is equal to the size of the array.
        size_t          _index;

        /// Permit `_Iterable` to create iterators.
        friend class _Iterable;
    };

    /// Returns an iterator to the beginning of the name or value array.
    iterator begin() const
    {
        return iterator(_arrayPointer, 0);
    }

    /// Returns an iterator to the end of the name or value array.
    iterator end() const
    {
        return iterator(_arrayPointer, EnumType::_size);
    }

    /// Returns the number of valid elements (names or values) in the iterable -
    /// the number of times an iterator starting at `begin()` can be
    /// dereferenced and then advanced before reaching `end()`.
    size_t size() const { return EnumType::size(); }

  private:
    /// Creates an `_Iterable` object over an array.
    _Iterable(ArrayType arrayPointer) : _arrayPointer(arrayPointer) { }

    /// The array over which iteration will be performed.
    ArrayType           _arrayPointer;

    /// Permit the enum class itself to create `_Iterable` objects.
    friend class _Implementation<EnumType>;
};



/// Compile-time helper class used to transform expressions of the forms `A` and
/// `A = 42` into values of type `UnderlyingType` that can be used in
/// initializer lists. The `ENUM` macro is passed a mixture of simple enum
/// constants (`A`) and constants with an explicitly-assigned value (`A = 42`).
/// Both must be turned into expressions of type `UnderlyingType` in order to be
/// usable in initializer lists of the values array. This is done by prepending
/// a cast to the expression, as follows:
/// ~~~{.cc}
/// (_eat_assign<UnderlyingType>)A
/// (_eat_assign<UnderlyingType>)A = 42
/// ~~~
/// The second case is the interesting one. At compile time, the value `A` is
/// first converted to an equivalent `_eat_assign<UnderlyingType>` object, that
/// stores the value. This object has an overriden assignment operator, which
/// "eats" the `= 42` and returns the stored value of `A`, which is then used in
/// the initializer list.
/// @tparam UnderlyingType Final type used in the values array.
template <typename UnderlyingType>
class _eat_assign {
  private:
    UnderlyingType  _value;

  public:
    explicit constexpr _eat_assign(UnderlyingType value) : _value(value) { }
    template <typename Any>
    constexpr UnderlyingType operator =(Any dummy) const
        { return _value; }
    constexpr operator UnderlyingType () const { return _value; }
};

/// Prepends its second argument with the cast `(_eat_assign<UnderlyingType>)`
/// in order to make it usable in initializer lists. See `_eat_assign`.
#define _ENUM_EAT_ASSIGN_SINGLE(UnderlyingType, expression)                    \
    ((_enum::_eat_assign<UnderlyingType>)expression)

/// Prepends each of its arguments with the casts
/// `(_eat_assign<UnderlyingType>)`, creating the elements of an initializer
/// list of objects of type `UnderlyingType`.
#define _ENUM_EAT_ASSIGN(UnderlyingType, ...)                                  \
    _ENUM_PP_MAP(_ENUM_EAT_ASSIGN_SINGLE, UnderlyingType, __VA_ARGS__)



/// Stringizes its second argument. The first argument is not used - it is there
/// only because `_ENUM_PP_MAP` expects it.
#define _ENUM_STRINGIZE_SINGLE(ignored, expression)     #expression

/// Stringizes each of its arguments.
#define _ENUM_STRINGIZE(...)                                                   \
    _ENUM_PP_MAP(_ENUM_STRINGIZE_SINGLE, ignored, __VA_ARGS__)



/// Symbols that end a constant name. Constant can be defined in several ways,
/// for example:
/// ~~~{.cc}
/// A
/// A = AnotherConstant
/// A = 42
/// A=42
/// ~~~
/// These definitions are stringized in their entirety by `_ENUM_STRINGIZE`.
/// This means that in addition to the actual constant names, the raw `_names`
/// arrays potentially contain additional trailing symbols. `_ENUM_NAME_ENDERS`
/// defines an array of symbols that would end the part of the string that is
/// the actual constant name. Note that it is important that the null terminator
/// is implicitly present in this array.
#define _ENUM_NAME_ENDERS   "= \t\n"

/// Compile-time function that determines whether a character terminates the
/// name portion of an enum constant definition.
///
/// Call as `_endsName(c)`.
///
/// @param c Character to be tested.
/// @param index Current index into the `_ENUM_NAME_ENDERS` array.
/// @return `true` if and only if `c` is one of the characters in
///     `_ENUM_NAME_ENDERS`, including the implicit null terminator in that
///     array.
constexpr bool _endsName(char c, size_t index = 0)
{
    return
        // First, test whether c is equal to the current character in
        // _ENUM_NAME_ENDERS. In the case where c is the null terminator, this
        // will cause _endsName to return true when it has exhausted
        // _ENUM_NAME_ENDERS.
        c == _ENUM_NAME_ENDERS[index]    ? true  :
        // If _ENUM_NAME_ENDERS has been exhausted and c never matched, return
        // false.
        _ENUM_NAME_ENDERS[index] == '\0' ? false :
        // Otherwise, go on to the next character in _ENUM_ENDERS.
        _endsName(c, index + 1);
}

constexpr char _toLowercaseAscii(char c)
{
    return c >= 0x41 && c <= 0x5A ? c + 0x20 : c;
}

/// Compile-time function that matches a stringized name (with potential
/// trailing spaces and equals signs) against a reference name (a regular
/// null-terminated string).
///
/// Call as `_namesMatch(stringizedName, referenceName)`.
///
/// @param stringizedName A stringized constant name, potentially terminated by
///     one of the symbols in `_ENUM_NAME_ENDERS` instead of a null terminator.
/// @param referenceName A name of interest. Null-terminated.
/// @param index Current index into both names.
/// @return `true` if and only if the portion of `stringizedName` before any of
///     the symbols in `_ENUM_NAME_ENDERS` exactly matches `referenceName`.
constexpr bool _namesMatch(const char *stringizedName,
                           const char *referenceName,
                           size_t index = 0)
{
    return
        // If the current character in the stringized name is a name ender,
        // return true if the reference name ends as well, and false otherwise.
        _endsName(stringizedName[index]) ? referenceName[index] == '\0' :
        // The current character in the stringized name is not a name ender. If
        // the reference name ended, then it is too short, so return false.
        referenceName[index] == '\0'     ? false                        :
        // Neither name has ended. If the two current characters don't match,
        // return false.
        stringizedName[index] !=
            referenceName[index]         ? false                        :
        // Otherwise, if the characters match, continue by comparing the rest of
        // the names.
        _namesMatch(stringizedName, referenceName, index + 1);
}

constexpr bool _namesMatchNocase(const char *stringizedName,
                                 const char *referenceName,
                                 size_t index = 0)
{
    return
        _endsName(stringizedName[index]) ? referenceName[index] == '\0' :
        referenceName[index] == '\0' ? false :
        _toLowercaseAscii(stringizedName[index]) !=
            _toLowercaseAscii(referenceName[index]) ? false :
        _namesMatchNocase(stringizedName, referenceName, index + 1);
}

#define _ENUM_NOT_FOUND     ((size_t)-1)



/// Functions and types used to compute range properties such as the minimum and
/// maximum declared enum values, and the total number of valid enum values.
namespace _range {

template <typename UnderlyingType>
constexpr UnderlyingType _findMin(const UnderlyingType *values,
                                  size_t valueCount, size_t index,
                                  UnderlyingType best)
{
    return
        index == valueCount ? best :
        values[index] < values[valueCount] ?
            _findMin(values, valueCount, index + 1, values[index]) :
            _findMin(values, valueCount, index + 1, best);
}

template <typename UnderlyingType>
constexpr UnderlyingType _findMax(const UnderlyingType *values,
                                  size_t valueCount, size_t index,
                                  UnderlyingType best)
{
    return
        index == valueCount ? best :
        values[index] > values[valueCount] ?
            _findMax(values, valueCount, index + 1, values[index]) :
            _findMax(values, valueCount, index + 1, best);
}

// TODO This can probably now be replaced with a sizeof on the array.
/// Compile-time function that finds the "size" of the enum names and values
/// arrays. The size is the number of constants that would be returned when
/// iterating over the enum. Constants are returned when they are not special
/// (`_bad`, `_def`, `_min`, or `_max`), not bad (not equal to `_bad` if `_bad`
/// is defined, or not the last non-special constant otherwise), not less than
/// the minimum constant, and not less than the maximum constant.
///
/// Call as `_size(values, count, special, specialCount, bad, min, max)`.
///
/// @tparam Underlying enum type.
/// @param values Enum values.
/// @param valueCount Size of the `values` array.
/// @param specialIndices Indices of the special constants.
/// @param specialIndexCount Number of special indices.
/// @param badValue The bad value.
/// @param min Minimum value.
/// @param max Maximum value.
/// @param index Current index in the scan over `values`.
/// @param accumulator Number of valid constants found so far.
template <typename UnderlyingType>
constexpr size_t _size(const UnderlyingType *values, size_t valueCount,
                       size_t index = 0, size_t accumulator = 0)
{
    return
        // If the index has reached the end of values, return the number of
        // valid constants found.
        index == valueCount         ? accumulator             :
        // If the current index is none of the above, continue at the next index
        // and increment the accumulator to account for the current value.
        _size(values, valueCount, index + 1, accumulator + 1);
}

} // namespace _range

} // namespace _enum

// TODO Document reliance on the order of strings and constants being the same.
// TODO Document naming convention: raw, blank, processed.
// TODO Note that the static_assert for _rawSize > 0 never really gets a chance
// to fail in practice, because the preprocessor macros break before that.
// TODO Argue why there is always a first regular and a last regular.
// TODO Document clang WAR for min and max.
// TODO Default should be the first index that is not the invalid index.
// TODO static asserts about the underlying type being an integral type. Allow
// only the types supported by C++11 enum class.



namespace _enum {

// TODO Consider reserving memory statically. This will probably entail a great
// compile-time slowdown, however.
static inline const char * const* _processNames(const char * const *rawNames,
                                                size_t count)
{
    // Allocate the replacement names array.
    const char      **processedNames = new const char*[count];
    if (processedNames == nullptr)
        return nullptr;

    // Count the number of bytes needed in the replacement names array (an upper
    // bound).
    size_t          bytesNeeded = 0;
    for (size_t index = 0; index < count; ++index)
        bytesNeeded += std::strlen(rawNames[index]) + 1;

    // Allocate memory for the string data.
    char            *nameStorage = new char[bytesNeeded];
    if (nameStorage == nullptr) {
        delete[] processedNames;
        return nullptr;
    }

    // Trim each name and place the result in storage, then save a pointer to
    // it.
    char            *writePointer = nameStorage;
    for (size_t index = 0; index < count; ++index) {
        const char  *nameEnd =
            std::strpbrk(rawNames[index], _ENUM_NAME_ENDERS);

        size_t      symbolCount =
            nameEnd == nullptr ?
                std::strlen(rawNames[index]) :
                nameEnd - rawNames[index];

        std::strncpy(writePointer, rawNames[index], symbolCount);
        processedNames[index] = writePointer;
        writePointer += symbolCount;

        *writePointer = '\0';
        ++writePointer;
    }

    return processedNames;
}

template <typename EnumType> class _GeneratedArrays;

// TODO Move definitions to last macro.

#define _ENUM_ARRAYS(EnumType, UnderlyingType, ...)                            \
    class EnumType;                                                            \
                                                                               \
    namespace _enum {                                                          \
                                                                               \
    template <>                                                                \
    class _GeneratedArrays<EnumType> {                                         \
      protected:                                                               \
        using _Integral = UnderlyingType;                                      \
                                                                               \
      public:                                                                  \
        enum _Enumerated : _Integral { __VA_ARGS__ };                          \
                                                                               \
      protected:                                                               \
        static constexpr _Enumerated        _value_array[] =                   \
            { _ENUM_EAT_ASSIGN(_Enumerated, __VA_ARGS__) };                    \
                                                                               \
        static constexpr const char         *_name_array[] =                   \
            { _ENUM_STRINGIZE(__VA_ARGS__) };                                  \
                                                                               \
        static constexpr size_t             _size =                            \
            _ENUM_PP_COUNT(__VA_ARGS__);                                       \
    };                                                                         \
                                                                               \
    constexpr _GeneratedArrays<EnumType>::_Enumerated _ENUM_WEAK               \
        _GeneratedArrays<EnumType>::_value_array[];                            \
                                                                               \
    constexpr const char * _ENUM_WEAK                                          \
        _GeneratedArrays<EnumType>::_name_array[];                             \
                                                                               \
    template <>                                                                \
    const char * const * _ENUM_WEAK                                            \
        _Implementation<EnumType>::_processedNames = nullptr;                  \
                                                                               \
    }

template <typename EnumType>
class _Implementation : public _GeneratedArrays<EnumType> {
  protected:
    using _arrays = _GeneratedArrays<EnumType>;
    using _arrays::_value_array;
    using _arrays::_name_array;

  public:
    using typename _arrays::_Enumerated;
    using typename _arrays::_Integral;

    using _arrays::_size;
    static_assert(_size > 0, "no constants defined in enum type");

    static const EnumType   _first;

    _Implementation() = delete;
    constexpr _Implementation(_Enumerated constant) : _value(constant) { }

    constexpr _Integral to_int() const
    {
        return _value;
    }

    constexpr static EnumType _from_int(_Integral value)
    {
        return _value_array[_from_int_loop(value, true)];
    }

    constexpr static EnumType _from_int_unchecked(_Integral value)
    {
        return (_Enumerated)value;
    }

    const char* to_string() const
    {
        _processNames();

        for (size_t index = 0; index < _size; ++index) {
            if (_value_array[index] == _value)
                return _processedNames[index];
        }

        throw std::domain_error("Enum::_to_string: invalid enum value");
    }

    constexpr static EnumType _from_string(const char *name)
    {
        return _value_array[_from_string_loop(name, true)];
    }

    constexpr static EnumType _from_string_nocase(const char *name)
    {
        return _value_array[_from_string_nocase_loop(name, true)];
    }

    constexpr static bool _is_valid(_Integral value)
    {
        return _from_int_loop(value, false) != _ENUM_NOT_FOUND;
    }

    constexpr static bool _is_valid(const char *name)
    {
        return _from_string_loop(name, false) != _ENUM_NOT_FOUND;
    }

    constexpr static bool _is_valid_nocase(const char *name)
    {
        return _from_string_nocase_loop(name, false) != _ENUM_NOT_FOUND;
    }

    operator _Enumerated() { return _value; }

  protected:
    _Enumerated                         _value;

    static const char * const           *_processedNames;

    static void _processNames()
    {
        if (_processedNames == nullptr)
            _processedNames = _enum::_processNames(_name_array, _size);
    }

    using _ValueIterable =
        _Iterable<const EnumType, EnumType, const _Enumerated * const>;
    using _NameIterable =
        _Iterable<const char*, EnumType, const char * const*>;

  public:
    static _ValueIterable _values()
    {
        return _ValueIterable(_value_array);
    }

    static _NameIterable _names()
    {
        _processNames();

        return _NameIterable(_processedNames);
    }

  protected:
    constexpr static size_t _from_int_loop(_Integral value,
                                           bool throw_exception,
                                           size_t index = 0)
    {
        return
            index == _size ?
                (throw_exception ?
                    throw std::runtime_error(
                        "Enum::from_int: invalid integer value") :
                    _ENUM_NOT_FOUND) :
            _value_array[index] == value ? index :
            _from_int_loop(value, throw_exception, index + 1);
    }

    constexpr static size_t _from_string_loop(const char *name,
                                              bool throw_exception,
                                              size_t index = 0)
    {
        return
            index == _size ?
                (throw_exception ?
                    throw std::runtime_error(
                        "Enum::_from_string: invalid string argument") :
                    _ENUM_NOT_FOUND) :
            _namesMatch(_name_array[index], name) ? index :
            _from_string_loop(name, throw_exception, index + 1);
    }

    constexpr static size_t _from_string_nocase_loop(const char *name,
                                                     bool throw_exception,
                                                     size_t index = 0)
    {
        return
            index == _size ?
                (throw_exception ?
                    throw std::runtime_error(
                        "Enum::_from_string_nocase: invalid string argument") :
                    _ENUM_NOT_FOUND) :
                _namesMatchNocase(_name_array[index], name) ? index :
                _from_string_nocase_loop(name, throw_exception, index + 1);
    }

    // TODO Remove static casts wherever reasonable.
  public:
    bool operator ==(const EnumType &other) const
        { return static_cast<const EnumType&>(*this)._value == other._value; }
    bool operator ==(const _Enumerated value) const
        { return static_cast<const EnumType&>(*this)._value == value; }
    template <typename T> bool operator ==(T other) const = delete;

    bool operator !=(const EnumType &other) const
        { return !(*this == other); }
    bool operator !=(const _Enumerated value) const
        { return !(*this == value); }
    template <typename T> bool operator !=(T other) const = delete;

    bool operator <(const EnumType &other) const
        { return static_cast<const EnumType&>(*this)._value < other._value; }
    bool operator <(const _Enumerated value) const
        { return static_cast<const EnumType&>(*this)._value < value; }
    template <typename T> bool operator <(T other) const = delete;

    bool operator <=(const EnumType &other) const
        { return static_cast<const EnumType&>(*this)._value <= other._value; }
    bool operator <=(const _Enumerated value) const
        { return static_cast<const EnumType&>(*this)._value <= value; }
    template <typename T> bool operator <=(T other) const = delete;

    bool operator >(const EnumType &other) const
        { return static_cast<const EnumType&>(*this)._value > other._value; }
    bool operator >(const _Enumerated value) const
        { return static_cast<const EnumType&>(*this)._value > value; }
    template <typename T> bool operator >(T other) const = delete;

    bool operator >=(const EnumType &other) const
        { return static_cast<const EnumType&>(*this)._value >= other._value; }
    bool operator >=(const _Enumerated value) const
        { return static_cast<const EnumType&>(*this)._value >= value; }
    template <typename T> bool operator >=(T other) const = delete;

    int operator -() const = delete;
    template <typename T> int operator +(T other) const = delete;
    template <typename T> int operator -(T other) const = delete;
    template <typename T> int operator *(T other) const = delete;
    template <typename T> int operator /(T other) const = delete;
    template <typename T> int operator %(T other) const = delete;

    template <typename T> int operator <<(T other) const = delete;
    template <typename T> int operator >>(T other) const = delete;

    int operator ~() const = delete;
    template <typename T> int operator &(T other) const = delete;
    template <typename T> int operator |(T other) const = delete;
    template <typename T> int operator ^(T other) const = delete;

    int operator !() const = delete;
    template <typename T> int operator &&(T other) const = delete;
    template <typename T> int operator ||(T other) const = delete;
};

#define _ENUM_STATIC_DEFINITIONS(EnumType)                                     \
    namespace _enum {                                                          \
                                                                               \
    template <>                                                                \
    constexpr EnumType _Implementation<EnumType>::_first =                     \
        EnumType::_from_int(EnumType::_value_array[0]);                        \
                                                                               \
    }

}

#pragma once

#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <storages/postgres/detail/is_decl_complete.hpp>
#include <storages/postgres/io/pg_types.hpp>
#include <utils/void_t.hpp>

namespace storages::postgres {

class UserTypes;

namespace io {

/// @page pg_io µPg: Parsers and Formatters
///
/// @todo Decribe the system of parser, formatters and their customization
///

/// Category of buffer contents.
///
/// Applied to binary parsers and deduced from field's data type.
enum class BufferCategory {
  kKeepCategory = -1,  //!< kKeepCategory keep current buffer category
  kNoParser = 0,  //!< kNoParser the data type doesn't have a parser defined
  kVoid,  //!< kVoid there won't be a buffer for this field, but the category is
          //!< required for correctly handling void results
  kPlainBuffer,      //!< kPlainBuffer the buffer is a single plain value
  kArrayBuffer,      //!< kArrayBuffer the buffer contains an array of values
  kCompositeBuffer,  //!< kCompositeBuffer the buffer contains a user-defined
                     //!< composite type
  kRangeBuffer       //!< kRangeBuffer the buffer contains a range type
};

const std::string& ToString(BufferCategory);

template <BufferCategory Category>
using BufferCategoryConstant = std::integral_constant<BufferCategory, Category>;

using TypeBufferCategory = std::unordered_map<Oid, BufferCategory>;

BufferCategory GetTypeBufferCategory(const TypeBufferCategory&, Oid);

struct BufferCategoryHash {
  using IntegerType = std::underlying_type_t<BufferCategory>;
  std::size_t operator()(BufferCategory val) const {
    return std::hash<IntegerType>{}(static_cast<IntegerType>(val));
  }
};

constexpr int kPgBinaryDataFormat = 1;

/// Fields that are null are denoted by specifying their length == -1
constexpr const Integer kPgNullBufferSize = -1;

struct FieldBuffer {
  static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

  bool is_null = false;
  BufferCategory category = BufferCategory::kPlainBuffer;
  std::size_t length = 0;
  const std::uint8_t* buffer = nullptr;

  std::string ToString() const {
    return {reinterpret_cast<const char*>(buffer), length};
  }
  constexpr FieldBuffer GetSubBuffer(
      std::size_t offset, std::size_t size = npos,
      BufferCategory cat = BufferCategory::kKeepCategory) const;

  template <typename T>
  std::size_t Read(T&& value,
                   BufferCategory cat = BufferCategory::kKeepCategory,
                   std::size_t length = sizeof(T));
  template <typename T>
  std::size_t Read(T&& value, const TypeBufferCategory& categories,
                   std::size_t length = sizeof(T),
                   BufferCategory cat = BufferCategory::kKeepCategory);

  // Read 'raw' postgres buffer - first read the size, then read the value
  template <typename T>
  std::size_t ReadRaw(T&& value, const TypeBufferCategory& categories,
                      BufferCategory cat = BufferCategory::kKeepCategory);
};

/// @brief Primary template for Postgre buffer parser.
/// Specialisations should provide call operators that parse FieldBuffer.
template <typename T, typename Enable = ::utils::void_t<>>
struct BufferParser;

/// @brief Primary template for Postgre buffer formatter
/// Specialisations should provide call operators that write to a buffer.
template <typename T, typename Enable = ::utils::void_t<>>
struct BufferFormatter;

namespace traits {

/// Customisation point for parsers
template <typename T, typename Enable = ::utils::void_t<>>
struct Input {
  using type = BufferParser<T>;
};

/// Customisation point for formatters
template <typename T, typename Enable = ::utils::void_t<>>
struct Output {
  using type = BufferFormatter<T>;
};

/// @brief A default deducer of parsers/formatters for a type/data format.
/// Can be specialised for a type/format pair providing custom
/// parsers/formatters.
template <typename T>
struct IO {
  using ParserType = typename Input<T>::type;
  using FormatterType = typename Output<T>::type;
};

/// @brief Metafunction to detect if a type has a parser.
template <typename T>
struct HasParser : utils::IsDeclComplete<typename IO<T>::ParserType> {};

/// @brief Metafunction to detect if a type has a formatter.
template <typename T>
struct HasFormatter : utils::IsDeclComplete<typename IO<T>::FormatterType> {};

//@{
/** @name Shortcut metafunction result values */
template <typename T>
constexpr bool kHasParser = HasParser<T>::value;
template <typename T>
constexpr bool kHasFormatter = HasFormatter<T>::value;
//@}

/// Buffer category for parser
template <typename T>
struct ParserBufferCategory
    : BufferCategoryConstant<BufferCategory::kPlainBuffer> {};
template <typename T>
using ParserBufferCategoryType = typename ParserBufferCategory<T>::type;
template <typename T>
constexpr BufferCategory kParserBufferCategory = ParserBufferCategory<T>::value;

//@{
/** @name Buffer category for a type */
namespace detail {
template <typename T>
constexpr auto DetectBufferCategory() {
  if constexpr (kHasParser<T>) {
    return ParserBufferCategoryType<typename IO<T>::ParserType>{};
  } else {
    return BufferCategoryConstant<BufferCategory::kNoParser>{};
  }
};
}  // namespace detail
template <typename T>
struct TypeBufferCategory : decltype(detail::DetectBufferCategory<T>()) {};
template <typename T>
constexpr BufferCategory kTypeBufferCategory = TypeBufferCategory<T>::value;
//@}

namespace detail {

template <typename T>
struct CustomParserDefined : utils::IsDeclComplete<BufferParser<T>> {};
template <typename T>
constexpr bool kCustomParserDefined = CustomParserDefined<T>::value;

template <typename T>
struct CustomFormatterDefined : utils::IsDeclComplete<BufferFormatter<T>> {};
template <typename T>
constexpr bool kCustomFormatterDefined = CustomFormatterDefined<T>::value;

}  // namespace detail

}  // namespace traits

}  // namespace io
}  // namespace storages::postgres
#include <instantee/scene_ir/canonicalize.hpp>

#include <yyjson.h>

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "canonicalize", message};
}

struct DocGuard {
  yyjson_doc *doc = nullptr;
  ~DocGuard() { yyjson_doc_free(doc); }
  DocGuard(const DocGuard &) = delete;
  DocGuard &operator=(const DocGuard &) = delete;
  explicit DocGuard(yyjson_doc *owned) : doc(owned) {}
};

void append_escaped(std::string &out, std::string_view text) {
  out.push_back('"');
  for (const unsigned char ch : text) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (ch < 0x20U) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
        out += buf;
      } else {
        out.push_back(static_cast<char>(ch));
      }
    }
  }
  out.push_back('"');
}

void append_number(std::string &out, yyjson_val *value) {
  if (yyjson_is_uint(value)) {
    out += std::to_string(yyjson_get_uint(value));
    return;
  }
  if (yyjson_is_sint(value)) {
    out += std::to_string(yyjson_get_sint(value));
    return;
  }
  // Fixed round-trip style for reals; never emit NaN/Inf.
  const double number = yyjson_get_real(value);
  char buf[64];
  const int written = std::snprintf(buf, sizeof(buf), "%.17g", number);
  if (written <= 0 || written >= static_cast<int>(sizeof(buf))) {
    out += "0";
    return;
  }
  out.append(buf, static_cast<std::size_t>(written));
}

std::expected<void, diagnostics::Error> write_value(std::string &out,
                                                    yyjson_val *value);

std::expected<void, diagnostics::Error> write_object(std::string &out,
                                                     yyjson_val *obj) {
  std::vector<std::pair<std::string, yyjson_val *>> members;
  yyjson_obj_iter iter;
  yyjson_obj_iter_init(obj, &iter);
  yyjson_val *key;
  while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
    yyjson_val *val = yyjson_obj_iter_get_val(key);
    const char *text = yyjson_get_str(key);
    const std::size_t len = yyjson_get_len(key);
    if (text == nullptr) {
      return std::unexpected(err("invalid_json", "Object key is not a string"));
    }
    members.emplace_back(std::string(text, len), val);
  }
  std::sort(
      members.begin(), members.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
  out.push_back('{');
  for (std::size_t index = 0; index < members.size(); ++index) {
    if (index != 0U) {
      out.push_back(',');
    }
    append_escaped(out, members[index].first);
    out.push_back(':');
    if (auto ok = write_value(out, members[index].second); !ok) {
      return ok;
    }
  }
  out.push_back('}');
  return {};
}

std::expected<void, diagnostics::Error> write_array(std::string &out,
                                                    yyjson_val *arr) {
  out.push_back('[');
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(arr, &iter);
  yyjson_val *value;
  bool first = true;
  while ((value = yyjson_arr_iter_next(&iter)) != nullptr) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    if (auto ok = write_value(out, value); !ok) {
      return ok;
    }
  }
  out.push_back(']');
  return {};
}

std::expected<void, diagnostics::Error> write_value(std::string &out,
                                                    yyjson_val *value) {
  if (yyjson_is_null(value)) {
    out += "null";
    return {};
  }
  if (yyjson_is_bool(value)) {
    out += yyjson_get_bool(value) ? "true" : "false";
    return {};
  }
  if (yyjson_is_num(value)) {
    append_number(out, value);
    return {};
  }
  if (yyjson_is_str(value)) {
    const char *text = yyjson_get_str(value);
    const std::size_t len = yyjson_get_len(value);
    append_escaped(out, std::string_view(text, len));
    return {};
  }
  if (yyjson_is_obj(value)) {
    return write_object(out, value);
  }
  if (yyjson_is_arr(value)) {
    return write_array(out, value);
  }
  return std::unexpected(err("invalid_json", "Unsupported JSON value"));
}

} // namespace

std::expected<std::string, diagnostics::Error>
canonicalize_json(std::string_view json) {
  if (json.empty()) {
    return std::unexpected(err("invalid_json", "Empty JSON"));
  }
  std::string buffer(json);
  yyjson_read_err read_err{};
  DocGuard guard{
      yyjson_read_opts(buffer.data(), buffer.size(), 0, nullptr, &read_err)};
  if (guard.doc == nullptr) {
    return std::unexpected(err("invalid_json", read_err.msg != nullptr
                                                   ? read_err.msg
                                                   : "JSON parse failed"));
  }
  std::string out;
  out.reserve(json.size());
  if (auto ok = write_value(out, yyjson_doc_get_root(guard.doc)); !ok) {
    return std::unexpected(ok.error());
  }
  return out;
}

std::expected<std::string, diagnostics::Error>
sha256_hex(std::span<const std::byte> bytes) {
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  CC_SHA256(reinterpret_cast<const unsigned char *>(bytes.data()),
            static_cast<CC_LONG>(bytes.size()), digest.data());
  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.resize(digest.size() * 2U);
  for (std::size_t index = 0; index < digest.size(); ++index) {
    hex[index * 2U] = kHex[digest[index] >> 4U];
    hex[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  return hex;
}

std::expected<std::string, diagnostics::Error>
canonical_sha256_hex(std::string_view json) {
  auto canonical = canonicalize_json(json);
  if (!canonical) {
    return std::unexpected(canonical.error());
  }
  const auto *data = reinterpret_cast<const std::byte *>(canonical->data());
  return sha256_hex(std::span<const std::byte>(data, canonical->size()));
}

} // namespace instantee::scene_ir

#include <instantee/asset_pipeline/svg_import.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace instantee::asset_pipeline {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "asset_pipeline", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view code,
                                             std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "asset_pipeline", message};
}

[[nodiscard]] char ascii_lower(char c) noexcept {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] std::string to_lower_ascii(std::string_view in) {
  std::string out;
  out.resize(in.size());
  std::transform(in.begin(), in.end(), out.begin(), ascii_lower);
  return out;
}

[[nodiscard]] bool contains_ci(std::string_view haystack_lower,
                               std::string_view needle_lower) noexcept {
  return haystack_lower.find(needle_lower) != std::string_view::npos;
}

// True when `href` / `xlink:href` value is a remote or file URL. Local
// fragment references ("#id") and relative same-document refs without a
// scheme are allowed by the subset; anything with a scheme or "//" is not.
[[nodiscard]] bool is_external_url_value(std::string_view value) noexcept {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  if (value.empty()) {
    return false;
  }
  if (value.front() == '#') {
    return false; // fragment-only local reference
  }
  if (value.size() >= 2 && value[0] == '/' && value[1] == '/') {
    return true; // protocol-relative
  }
  const auto colon = value.find(':');
  if (colon == std::string_view::npos) {
    return false; // relative path without scheme — still not fetched by us,
                  // but treated as non-external for the "no remote load" rule
  }
  // scheme present
  const auto scheme = value.substr(0, colon);
  return !(scheme == "data"); // data: also rejected separately as complex
}

[[nodiscard]] bool has_external_href(std::string_view lower) {
  // Scan attribute-like patterns without a full XML parser.
  constexpr std::string_view keys[] = {"xlink:href=", "href="};
  for (const auto key : keys) {
    std::size_t pos = 0;
    while ((pos = lower.find(key, pos)) != std::string_view::npos) {
      pos += key.size();
      if (pos >= lower.size()) {
        break;
      }
      const char quote = lower[pos];
      if (quote != '"' && quote != '\'') {
        continue;
      }
      const auto end = lower.find(quote, pos + 1);
      if (end == std::string_view::npos) {
        return true; // malformed — fail closed
      }
      const auto value = lower.substr(pos + 1, end - (pos + 1));
      if (is_external_url_value(value) || value.starts_with("data:") ||
          value.starts_with("javascript:")) {
        return true;
      }
      pos = end + 1;
    }
  }
  return false;
}

} // namespace

std::expected<void, diagnostics::Error>
validate_svg_security(std::span<const std::uint8_t> bytes,
                      const SvgImportLimits &limits) {
  if (bytes.size() > limits.max_input_bytes) {
    return std::unexpected(
        make_budget("svg.input.limit", "SVG exceeds max input bytes"));
  }
  if (bytes.empty()) {
    return std::unexpected(make_invalid("svg.empty", "SVG input is empty"));
  }

  const std::string lower = to_lower_ascii(std::string_view(
      reinterpret_cast<const char *>(bytes.data()), bytes.size()));

  // Must look like SVG markup (namespace optional).
  if (!contains_ci(lower, "<svg")) {
    return std::unexpected(
        make_invalid("svg.root.missing", "document has no <svg> root"));
  }

  // Executable / active content.
  if (contains_ci(lower, "<script") || contains_ci(lower, "javascript:") ||
      contains_ci(lower, "onload=") || contains_ci(lower, "onclick=") ||
      contains_ci(lower, "onerror=") || contains_ci(lower, "onmouse")) {
    return std::unexpected(make_invalid("svg.script.forbidden",
                                        "SVG scripts/handlers are forbidden"));
  }

  // Hosted foreign content.
  if (contains_ci(lower, "<foreignobject") || contains_ci(lower, "<iframe") ||
      contains_ci(lower, "<object") || contains_ci(lower, "<embed")) {
    return std::unexpected(make_invalid(
        "svg.foreign.forbidden", "foreign/embedded host content is forbidden"));
  }

  // Stylesheet remote import.
  if (contains_ci(lower, "@import") || contains_ci(lower, "url(http:") ||
      contains_ci(lower, "url(https:") || contains_ci(lower, "url(//") ||
      contains_ci(lower, "url('http:") || contains_ci(lower, "url(\"http:") ||
      contains_ci(lower, "url('https:") || contains_ci(lower, "url(\"https:")) {
    return std::unexpected(make_invalid(
        "svg.external.style", "remote CSS/url() references are forbidden"));
  }

  if (has_external_href(lower)) {
    return std::unexpected(
        make_invalid("svg.external.href",
                     "external href/xlink:href references are forbidden"));
  }

  // Images and filters are outside the static path subset — fail closed
  // rather than partially importing with missing media.
  if (contains_ci(lower, "<image") || contains_ci(lower, "<filter") ||
      contains_ci(lower, "<fe") || contains_ci(lower, "<mask") ||
      contains_ci(lower, "<use")) {
    return std::unexpected(make_invalid(
        "svg.complex.unsupported",
        "SVG uses unsupported complex elements (image/filter/mask/use)"));
  }

  return {};
}

} // namespace instantee::asset_pipeline

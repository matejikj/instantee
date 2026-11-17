#include <instantee/typography/kinetic_layout.hpp>

#include <unicode/ubrk.h>
#include <unicode/ustring.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace instantee::typography {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "typography", message};
}

struct Utf16Map {
  std::vector<UChar> utf16{};
  // utf16 index → utf8 byte offset (size utf16_len + 1 for end sentinel).
  std::vector<std::uint32_t> utf8_at_utf16{};
};

[[nodiscard]] std::expected<Utf16Map, diagnostics::Error>
utf8_to_utf16_map(std::string_view utf8) {
  Utf16Map out;
  if (utf8.empty()) {
    out.utf8_at_utf16.push_back(0);
    return out;
  }
  if (utf8.size() >
      static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
    return std::unexpected(
        make_invalid("text_too_large", "UTF-8 text exceeds ICU int32 limits"));
  }
  UErrorCode status = U_ZERO_ERROR;
  const int32_t src_len = static_cast<int32_t>(utf8.size());
  int32_t needed = 0;
  u_strFromUTF8(nullptr, 0, &needed, utf8.data(), src_len, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    return std::unexpected(
        make_invalid("utf8_decode", "ICU UTF-8 to UTF-16 conversion failed"));
  }
  status = U_ZERO_ERROR;
  out.utf16.resize(static_cast<std::size_t>(needed));
  int32_t utf16_len = 0;
  u_strFromUTF8(out.utf16.data(), needed, &utf16_len, utf8.data(), src_len,
                &status);
  if (U_FAILURE(status)) {
    return std::unexpected(
        make_invalid("utf8_decode", "ICU UTF-8 to UTF-16 conversion failed"));
  }
  out.utf16.resize(static_cast<std::size_t>(utf16_len));

  // Walk UTF-8 to build utf16→utf8 offsets (BMP + surrogate pairs).
  out.utf8_at_utf16.assign(static_cast<std::size_t>(utf16_len) + 1, 0);
  std::size_t u8 = 0;
  std::size_t u16 = 0;
  while (u8 < utf8.size() && u16 < static_cast<std::size_t>(utf16_len)) {
    out.utf8_at_utf16[u16] = static_cast<std::uint32_t>(u8);
    const unsigned char lead = static_cast<unsigned char>(utf8[u8]);
    std::size_t seq = 1;
    if (lead >= 0xF0) {
      seq = 4;
    } else if (lead >= 0xE0) {
      seq = 3;
    } else if (lead >= 0xC0) {
      seq = 2;
    }
    if (u8 + seq > utf8.size()) {
      return std::unexpected(
          make_invalid("utf8_invalid", "truncated UTF-8 sequence"));
    }
    u8 += seq;
    // Non-BMP encodes as a surrogate pair in UTF-16.
    if (seq == 4) {
      if (u16 + 1 >= static_cast<std::size_t>(utf16_len)) {
        return std::unexpected(
            make_invalid("utf16_map", "UTF-16 map underflow on surrogate"));
      }
      out.utf8_at_utf16[u16 + 1] = out.utf8_at_utf16[u16];
      u16 += 2;
    } else {
      ++u16;
    }
  }
  out.utf8_at_utf16[static_cast<std::size_t>(utf16_len)] =
      static_cast<std::uint32_t>(utf8.size());
  if (u8 != utf8.size() || u16 != static_cast<std::size_t>(utf16_len)) {
    return std::unexpected(
        make_invalid("utf16_map", "UTF-8/UTF-16 offset map is inconsistent"));
  }
  return out;
}

struct ByteRange {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};

[[nodiscard]] std::expected<std::vector<ByteRange>, diagnostics::Error>
break_ranges(std::string_view utf8, const Utf16Map &map,
             UBreakIteratorType type, bool words_only) {
  std::vector<ByteRange> out;
  if (utf8.empty()) {
    return out;
  }
  UErrorCode status = U_ZERO_ERROR;
  UBreakIterator *it = ubrk_open(type, "cs", nullptr, 0, &status);
  if (U_FAILURE(status) || it == nullptr) {
    return std::unexpected(
        make_invalid("break_iterator", "ICU break iterator open failed"));
  }
  status = U_ZERO_ERROR;
  ubrk_setText(it, map.utf16.data(), static_cast<int32_t>(map.utf16.size()),
               &status);
  if (U_FAILURE(status)) {
    ubrk_close(it);
    return std::unexpected(
        make_invalid("break_iterator", "ICU break iterator setText failed"));
  }

  int32_t start = ubrk_first(it);
  for (;;) {
    const int32_t end = ubrk_next(it);
    if (end == UBRK_DONE) {
      break;
    }
    if (start < 0 || end < start ||
        static_cast<std::size_t>(end) >= map.utf8_at_utf16.size()) {
      ubrk_close(it);
      return std::unexpected(
          make_invalid("break_range", "ICU break offsets out of range"));
    }
    const std::uint32_t b = map.utf8_at_utf16[static_cast<std::size_t>(start)];
    const std::uint32_t e = map.utf8_at_utf16[static_cast<std::size_t>(end)];
    if (e < b || e > utf8.size()) {
      ubrk_close(it);
      return std::unexpected(
          make_invalid("break_range", "mapped UTF-8 break range is invalid"));
    }
    bool keep = e > b;
    if (keep && words_only) {
      // Drop pure whitespace/punctuation-only separators; keep spans with at
      // least one graphic byte so kinetic word stagger has stable targets.
      keep = false;
      for (std::uint32_t i = b; i < e; ++i) {
        const unsigned char ch = static_cast<unsigned char>(utf8[i]);
        if (ch > 0x20) {
          keep = true;
          break;
        }
      }
    }
    if (keep) {
      out.push_back(ByteRange{b, e});
    }
    start = end;
  }
  ubrk_close(it);
  return out;
}

[[nodiscard]] std::vector<ByteRange> line_ranges(std::string_view utf8) {
  std::vector<ByteRange> out;
  std::uint32_t begin = 0;
  for (std::uint32_t i = 0; i < utf8.size(); ++i) {
    if (utf8[i] == '\n') {
      out.push_back(ByteRange{begin, i});
      begin = i + 1;
    }
  }
  out.push_back(ByteRange{begin, static_cast<std::uint32_t>(utf8.size())});
  return out;
}

[[nodiscard]] std::vector<std::uint32_t>
glyphs_for_range(const ShapedText &shaped, std::uint32_t begin,
                 std::uint32_t end) {
  std::vector<std::uint32_t> indices;
  for (std::uint32_t gi = 0; gi < shaped.glyphs.size(); ++gi) {
    const std::uint32_t cluster = shaped.glyphs[gi].cluster;
    if (cluster >= begin && cluster < end) {
      indices.push_back(gi);
    }
  }
  return indices;
}

void fill_metrics(KineticUnit &unit, const ShapedText &shaped,
                  const KineticLayout &layout) {
  if (unit.glyph_indices.empty()) {
    unit.origin_x_26_6 = 0;
    unit.origin_y_26_6 = 0;
    unit.width_26_6 = 0;
    return;
  }
  const std::uint32_t first = unit.glyph_indices.front();
  unit.origin_x_26_6 = layout.glyph_origin_x_26_6[first];
  unit.origin_y_26_6 = layout.glyph_origin_y_26_6[first];
  std::int32_t max_right = unit.origin_x_26_6;
  for (std::uint32_t gi : unit.glyph_indices) {
    const std::int32_t right =
        layout.glyph_origin_x_26_6[gi] + shaped.glyphs[gi].x_advance_26_6;
    max_right = std::max(max_right, right);
  }
  unit.width_26_6 = max_right - unit.origin_x_26_6;
}

[[nodiscard]] KineticUnit make_unit(KineticLevel level, std::uint32_t index,
                                    ByteRange range, const ShapedText &shaped,
                                    KineticLayout &layout) {
  KineticUnit unit;
  unit.level = level;
  unit.index = index;
  unit.utf8_begin = range.begin;
  unit.utf8_end = range.end;
  unit.glyph_indices = glyphs_for_range(shaped, range.begin, range.end);
  fill_metrics(unit, shaped, layout);
  return unit;
}

} // namespace

std::span<const KineticUnit> kinetic_units_at(const KineticLayout &layout,
                                              KineticLevel level) noexcept {
  switch (level) {
  case KineticLevel::block:
    return std::span<const KineticUnit>(&layout.block, 1);
  case KineticLevel::line:
    return layout.lines;
  case KineticLevel::word:
    return layout.words;
  case KineticLevel::grapheme:
    return layout.graphemes;
  case KineticLevel::glyph:
    return layout.glyphs;
  }
  return {};
}

std::expected<KineticLayout, diagnostics::Error>
build_kinetic_layout(std::string_view utf8, const ShapedText &shaped) {
  if (utf8.find('\0') != std::string_view::npos) {
    return std::unexpected(
        make_invalid("text_embedded_nul", "UTF-8 text must not contain NUL"));
  }
  for (const GlyphPlacement &g : shaped.glyphs) {
    if (g.cluster > utf8.size()) {
      return std::unexpected(make_invalid(
          "cluster_out_of_range", "shaped cluster exceeds UTF-8 length"));
    }
  }

  KineticLayout layout;
  layout.glyph_origin_x_26_6.resize(shaped.glyphs.size());
  layout.glyph_origin_y_26_6.resize(shaped.glyphs.size());
  std::int32_t pen_x = 0;
  std::int32_t pen_y = 0;
  for (std::size_t i = 0; i < shaped.glyphs.size(); ++i) {
    layout.glyph_origin_x_26_6[i] = pen_x + shaped.glyphs[i].x_offset_26_6;
    layout.glyph_origin_y_26_6[i] = pen_y + shaped.glyphs[i].y_offset_26_6;
    pen_x += shaped.glyphs[i].x_advance_26_6;
    pen_y += shaped.glyphs[i].y_advance_26_6;
  }

  auto map = utf8_to_utf16_map(utf8);
  if (!map) {
    return std::unexpected(map.error());
  }

  const ByteRange whole{0, static_cast<std::uint32_t>(utf8.size())};
  layout.block = make_unit(KineticLevel::block, 0, whole, shaped, layout);

  const auto lines = line_ranges(utf8);
  layout.lines.reserve(lines.size());
  for (std::uint32_t i = 0; i < lines.size(); ++i) {
    layout.lines.push_back(
        make_unit(KineticLevel::line, i, lines[i], shaped, layout));
  }

  auto words = break_ranges(utf8, *map, UBRK_WORD, true);
  if (!words) {
    return std::unexpected(words.error());
  }
  layout.words.reserve(words->size());
  for (std::uint32_t i = 0; i < words->size(); ++i) {
    layout.words.push_back(
        make_unit(KineticLevel::word, i, (*words)[i], shaped, layout));
  }

  auto graphemes = break_ranges(utf8, *map, UBRK_CHARACTER, false);
  if (!graphemes) {
    return std::unexpected(graphemes.error());
  }
  layout.graphemes.reserve(graphemes->size());
  for (std::uint32_t i = 0; i < graphemes->size(); ++i) {
    KineticUnit unit;
    unit.level = KineticLevel::grapheme;
    unit.index = i;
    unit.utf8_begin = (*graphemes)[i].begin;
    unit.utf8_end = (*graphemes)[i].end;
    layout.graphemes.push_back(std::move(unit));
  }
  // Assign glyphs by overlap of [cluster, next_cluster) with grapheme spans so
  // ligatures covering multiple graphemes are referenced by each grapheme and
  // locked together during animation.
  for (std::uint32_t gi = 0; gi < shaped.glyphs.size(); ++gi) {
    const std::uint32_t begin = shaped.glyphs[gi].cluster;
    std::uint32_t end = static_cast<std::uint32_t>(utf8.size());
    for (std::uint32_t gj = gi + 1; gj < shaped.glyphs.size(); ++gj) {
      if (shaped.glyphs[gj].cluster > begin) {
        end = shaped.glyphs[gj].cluster;
        break;
      }
    }
    for (KineticUnit &grapheme : layout.graphemes) {
      const bool overlaps =
          begin < grapheme.utf8_end && end > grapheme.utf8_begin;
      if (overlaps) {
        grapheme.glyph_indices.push_back(gi);
      }
    }
  }
  for (KineticUnit &grapheme : layout.graphemes) {
    fill_metrics(grapheme, shaped, layout);
  }

  // Glyph units: each shaped glyph covers [cluster, next_cluster).
  layout.glyphs.reserve(shaped.glyphs.size());
  for (std::uint32_t gi = 0; gi < shaped.glyphs.size(); ++gi) {
    const std::uint32_t begin = shaped.glyphs[gi].cluster;
    std::uint32_t end = static_cast<std::uint32_t>(utf8.size());
    for (std::uint32_t gj = gi + 1; gj < shaped.glyphs.size(); ++gj) {
      if (shaped.glyphs[gj].cluster > begin) {
        end = shaped.glyphs[gj].cluster;
        break;
      }
    }
    KineticUnit unit;
    unit.level = KineticLevel::glyph;
    unit.index = gi;
    unit.utf8_begin = begin;
    unit.utf8_end = end;
    unit.glyph_indices = {gi};
    fill_metrics(unit, shaped, layout);
    layout.glyphs.push_back(std::move(unit));
  }

  return layout;
}

std::vector<std::vector<std::uint32_t>>
kinetic_cluster_locks(const KineticLayout &layout) {
  // Union-find over grapheme indices that share any shaped glyph.
  const std::size_t n = layout.graphemes.size();
  std::vector<std::uint32_t> parent(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    parent[i] = i;
  }
  auto find = [&](std::uint32_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  auto unite = [&](std::uint32_t a, std::uint32_t b) {
    a = find(a);
    b = find(b);
    if (a != b) {
      parent[b] = a;
    }
  };

  std::unordered_map<std::uint32_t, std::uint32_t> glyph_owner;
  for (std::uint32_t gi = 0; gi < layout.graphemes.size(); ++gi) {
    for (std::uint32_t glyph : layout.graphemes[gi].glyph_indices) {
      auto [it, inserted] = glyph_owner.emplace(glyph, gi);
      if (!inserted) {
        unite(it->second, gi);
      }
    }
  }

  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> groups;
  for (std::uint32_t i = 0; i < n; ++i) {
    groups[find(i)].push_back(i);
  }
  std::vector<std::vector<std::uint32_t>> out;
  out.reserve(groups.size());
  for (auto &entry : groups) {
    auto &members = entry.second;
    std::sort(members.begin(), members.end());
    out.push_back(std::move(members));
  }
  std::sort(out.begin(), out.end(),
            [](const std::vector<std::uint32_t> &a,
               const std::vector<std::uint32_t> &b) {
              return a.front() < b.front();
            });
  return out;
}

std::expected<std::vector<std::int32_t>, diagnostics::Error>
sample_kinetic_origins_x(const KineticLayout &layout, KineticLevel level,
                         std::span<const std::int32_t> unit_dx_26_6) {
  const auto units = kinetic_units_at(layout, level);
  if (unit_dx_26_6.size() != units.size()) {
    return std::unexpected(make_invalid(
        "kinetic_dx_size",
        "unit_dx_26_6 size must match the number of units at the level"));
  }

  std::vector<std::int32_t> dx_per_glyph(layout.glyph_origin_x_26_6.size(), 0);

  if (level == KineticLevel::grapheme) {
    const auto locks = kinetic_cluster_locks(layout);
    for (const auto &group : locks) {
      if (group.empty()) {
        continue;
      }
      const std::int32_t dx = unit_dx_26_6[group.front()];
      for (std::uint32_t grapheme_i : group) {
        for (std::uint32_t gi : layout.graphemes[grapheme_i].glyph_indices) {
          if (gi >= dx_per_glyph.size()) {
            return std::unexpected(make_invalid(
                "glyph_index", "kinetic unit references an invalid glyph"));
          }
          dx_per_glyph[gi] = dx;
        }
      }
    }
  } else {
    for (std::size_t ui = 0; ui < units.size(); ++ui) {
      for (std::uint32_t gi : units[ui].glyph_indices) {
        if (gi >= dx_per_glyph.size()) {
          return std::unexpected(make_invalid(
              "glyph_index", "kinetic unit references an invalid glyph"));
        }
        dx_per_glyph[gi] = unit_dx_26_6[ui];
      }
    }
  }

  std::vector<std::int32_t> out = layout.glyph_origin_x_26_6;
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] += dx_per_glyph[i];
  }
  return out;
}

} // namespace instantee::typography

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/document.hpp>
#include <instantee/scene_ir/scene_complexity.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using instantee::diagnostics::ErrorKind;
using instantee::scene_ir::check_scene_complexity;
using instantee::scene_ir::complexity_tier_p3_foundation_draft;
using instantee::scene_ir::lookup_complexity_tier;
using instantee::scene_ir::measure_scene_complexity;
using instantee::scene_ir::Node;
using instantee::scene_ir::NodeKind;
using instantee::scene_ir::SceneComplexityTier;
using instantee::scene_ir::SceneDocument;
using instantee::scene_ir::TextRun;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] SceneDocument base_doc() {
  SceneDocument doc;
  doc.schema_version = "1.0.0";
  doc.compiler_version = "0.1.0";
  doc.canvas_width = 1080;
  doc.canvas_height = 1920;
  doc.duration_frames = instantee::core::FrameIndex{1};
  Node cam;
  cam.id = "cam";
  cam.kind = NodeKind::camera;
  doc.nodes.push_back(cam);
  return doc;
}

} // namespace

int main() {
  {
    auto tier = complexity_tier_p3_foundation_draft();
    expect(tier.id == "p3-foundation-draft", "tier id");
    expect(tier.limits.max_nodes > 0 && tier.limits.max_canvas_width_px == 1080,
           "draft caps set");
    auto looked = lookup_complexity_tier("paper-b03-v1");
    expect(looked.has_value() && looked->id == "paper-b03-v1", "style alias");
    auto mixed = lookup_complexity_tier("mixed-b04-v1");
    expect(mixed.has_value() && mixed->id == "mixed-b04-v1", "mixed alias");
    auto worst = lookup_complexity_tier("worst-accepted-b05-v1");
    expect(worst.has_value() && worst->id == "worst-accepted-b05-v1",
           "worst-accepted alias");
    expect(!lookup_complexity_tier("no-such-tier").has_value(), "unknown");
  }

  {
    auto doc = base_doc();
    TextRun run;
    run.id = "t0";
    run.text = "ahoj";
    run.font_asset_id = "font0";
    run.font_size_px = 12;
    doc.text_runs.push_back(run);
    Node text;
    text.id = "txt";
    text.kind = NodeKind::text;
    text.text_run_id = "t0";
    doc.nodes.push_back(text);

    const auto metrics = measure_scene_complexity(doc);
    expect(metrics.nodes == 2 && metrics.text_runs == 1, "counts");
    expect(metrics.text_codepoints == 4, "codepoints");
    expect(metrics.draw_items == 1, "draw items");
    expect(check_scene_complexity(doc, complexity_tier_p3_foundation_draft())
               .has_value(),
           "within draft");
  }

  // Predictable reject before OOM: tiny custom tier.
  {
    auto doc = base_doc();
    for (int i = 0; i < 5; ++i) {
      Node n;
      n.id = "g" + std::to_string(i);
      n.kind = NodeKind::geometry;
      doc.nodes.push_back(n);
    }
    SceneComplexityTier tiny;
    tiny.id = "fixture-tiny";
    tiny.limits.max_draw_items = 2;
    auto failed = check_scene_complexity(doc, tiny);
    expect(!failed.has_value(), "over draw items");
    expect(failed.error().kind == ErrorKind::resource_budget_exceeded,
           "budget kind");
    expect(failed.error().code == "complexity.draw_items", "draw code");
  }

  {
    auto doc = base_doc();
    doc.canvas_width = 1920;
    doc.canvas_height = 1080; // landscape exceeds 1080×1920 draft
    auto failed =
        check_scene_complexity(doc, complexity_tier_p3_foundation_draft());
    expect(!failed.has_value(), "canvas width reject");
    expect(failed.error().code == "complexity.canvas_width", "width code");
  }

  {
    auto doc = base_doc();
    Node root;
    root.id = "root";
    root.kind = NodeKind::group;
    doc.nodes.push_back(root);
    std::string parent = "root";
    for (int i = 0; i < 30; ++i) {
      Node child;
      child.id = "d" + std::to_string(i);
      child.kind = NodeKind::group;
      child.parent_id = parent;
      parent = child.id;
      doc.nodes.push_back(child);
    }
    auto failed =
        check_scene_complexity(doc, complexity_tier_p3_foundation_draft());
    expect(!failed.has_value(), "depth reject");
    expect(failed.error().code == "complexity.node_depth", "depth code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

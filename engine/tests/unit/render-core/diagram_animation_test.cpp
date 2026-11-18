#include <instantee/render_core/diagram_animation.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(float a, float b, float eps = 1e-3f) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view contents) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << contents;
  return static_cast<bool>(out);
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    return true;
  }
  if (!write_if_missing(path, actual)) {
    return false;
  }
  return read_file(path) == actual;
}

[[nodiscard]] DiagramAnimationSpec make_explainer_spec() {
  DiagramAnimationSpec spec;
  spec.base.width_px = 280;
  spec.base.height_px = 160;
  spec.base.clear_color = Rgba8{255, 255, 255, 255};

  DiagramNode a{.id = "A", .cx = 50, .cy = 80, .width = 36, .height = 28};
  DiagramNode b{.id = "B", .cx = 140, .cy = 80, .width = 36, .height = 28};
  DiagramNode c{.id = "C", .cx = 230, .cy = 80, .width = 36, .height = 28};
  DiagramNode d{.id = "D", .cx = 140, .cy = 30, .width = 36, .height = 24};
  spec.base.nodes = {a, b, c, d};

  DiagramEdge ab{.id = "A-B",
                 .from_id = "A",
                 .to_id = "B",
                 .from_port = DiagramPort::east,
                 .to_port = DiagramPort::west,
                 .kind = DiagramEdgeKind::arrow,
                 .reveal = 0};
  DiagramEdge bc{.id = "B-C",
                 .from_id = "B",
                 .to_id = "C",
                 .from_port = DiagramPort::east,
                 .to_port = DiagramPort::west,
                 .kind = DiagramEdgeKind::arrow,
                 .reveal = 0};
  DiagramEdge bd{.id = "B-D",
                 .from_id = "B",
                 .to_id = "D",
                 .from_port = DiagramPort::north,
                 .to_port = DiagramPort::south,
                 .kind = DiagramEdgeKind::line,
                 .reveal = 0};
  spec.base.edges = {ab, bc, bd};

  // Staged reveal: A-B then B-C then B-D.
  spec.reveal_stages = {
      DiagramRevealStage{.edge_ids = {"A-B"}, .start_t = 0.0f, .end_t = 0.25f},
      DiagramRevealStage{.edge_ids = {"B-C"}, .start_t = 0.25f, .end_t = 0.5f},
      DiagramRevealStage{.edge_ids = {"B-D"}, .start_t = 0.5f, .end_t = 0.7f},
  };

  // Transform layout: pull D down and shift C right mid-timeline.
  spec.layout_poses = {
      DiagramLayoutPose{.t = 0.0f,
                        .nodes = {DiagramNodePose{"D", 140, 30, 0},
                                  DiagramNodePose{"C", 230, 80, 0}}},
      DiagramLayoutPose{.t = 0.7f,
                        .nodes = {DiagramNodePose{"D", 140, 30, 0},
                                  DiagramNodePose{"C", 230, 80, 0}}},
      DiagramLayoutPose{.t = 1.0f,
                        .nodes = {DiagramNodePose{"D", 140, 120, 0},
                                  DiagramNodePose{"C", 250, 60, 0.2f}}},
  };

  // Active path A→B→C after edges exist.
  spec.active_path = DiagramPathHighlight{
      .edge_ids = {"A-B", "B-C"},
      .node_ids = {"A", "B", "C"},
      .start_t = 0.55f,
      .end_t = 0.85f,
      .color = Rgba8{200, 40, 50, 255},
      .stroke_width_scale = 1.75f,
  };

  // Group compare A,B vs C,D near the end.
  spec.group_compare = DiagramGroupCompare{
      .group_a_node_ids = {"A", "B"},
      .group_b_node_ids = {"C", "D"},
      .start_t = 0.85f,
      .end_t = 1.0f,
      .group_a_fill = Rgba8{190, 220, 255, 255},
      .group_b_fill = Rgba8{255, 220, 180, 255},
      .dim_fill = Rgba8{235, 235, 235, 255},
  };
  return spec;
}

[[nodiscard]] const DiagramEdge *find_edge(const DiagramScene &scene,
                                           std::string_view id) {
  for (const auto &e : scene.edges) {
    if (e.id == id) {
      return &e;
    }
  }
  return nullptr;
}

[[nodiscard]] const DiagramNode *find_node(const DiagramScene &scene,
                                           std::string_view id) {
  for (const auto &n : scene.nodes) {
    if (n.id == id) {
      return &n;
    }
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr
        << "usage: diagram_animation_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }
  const auto golden_dir = root / "contracts/fixtures/diagram/goldens";

  const auto spec = make_explainer_spec();

  // Staged reveal progresses without touching the renderer API.
  {
    auto early = sample_diagram_animation(spec, 0.1f);
    auto mid = sample_diagram_animation(spec, 0.4f);
    auto late = sample_diagram_animation(spec, 0.65f);
    expect(early && mid && late, "sample reveal times");
    if (early && mid && late) {
      expect(near(find_edge(early->scene, "A-B")->reveal, 0.4f),
             "A-B mid first stage");
      expect(near(find_edge(early->scene, "B-C")->reveal, 0.0f),
             "B-C hidden early");
      expect(near(find_edge(mid->scene, "A-B")->reveal, 1.0f), "A-B done");
      expect(find_edge(mid->scene, "B-C")->reveal > 0.0f &&
                 find_edge(mid->scene, "B-C")->reveal < 1.0f,
             "B-C in progress");
      expect(near(find_edge(late->scene, "B-C")->reveal, 1.0f), "B-C done");
      expect(find_edge(late->scene, "B-D")->reveal > 0.0f, "B-D started");
    }
  }

  // Transform layout interpolates node poses; same layout_diagram path.
  {
    auto a = sample_diagram_animation(spec, 0.7f);
    auto b = sample_diagram_animation(spec, 1.0f);
    expect(a && b, "sample layout times");
    if (a && b) {
      expect(near(find_node(a->scene, "D")->cy, 30.0f), "D still high at 0.7");
      expect(near(find_node(b->scene, "D")->cy, 120.0f), "D moved down at 1.0");
      expect(near(find_node(b->scene, "C")->cx, 250.0f), "C shifted");
      auto la = layout_diagram(a->scene);
      auto lb = layout_diagram(b->scene);
      expect(la && lb, "layout both poses");
      if (la && lb) {
        const DiagramResolvedEdge *ra = nullptr;
        const DiagramResolvedEdge *rb = nullptr;
        for (const auto &e : la->edges) {
          if (e.id == "B-D") {
            ra = &e;
          }
        }
        for (const auto &e : lb->edges) {
          if (e.id == "B-D") {
            rb = &e;
          }
        }
        expect(ra && rb, "resolved B-D");
        if (ra && rb) {
          expect(!near(ra->to.y, rb->to.y),
                 "connector follows transformed D without renderer rewrite");
        }
      }
    }
  }

  // Active path highlight window.
  {
    auto on = sample_diagram_animation(spec, 0.7f);
    auto off = sample_diagram_animation(spec, 0.9f);
    expect(on && off, "path samples");
    if (on && off) {
      expect(on->path_active && !on->compare_active, "path window");
      expect(find_edge(on->scene, "A-B")->color.r == 200, "path color");
      expect(find_edge(on->scene, "A-B")->stroke_width > 2.0f, "path stroke");
      expect(!off->path_active && off->compare_active, "compare follows path");
    }
  }

  // Group compare recolors groups.
  {
    auto s = sample_diagram_animation(spec, 0.95f);
    expect(s.has_value() && s->compare_active, "compare active");
    if (s) {
      expect(find_node(s->scene, "A")->fill.b == 255, "group A fill");
      expect(find_node(s->scene, "C")->fill.r == 255 &&
                 find_node(s->scene, "C")->fill.b == 180,
             "group B fill");
    }
  }

  // State change does not invent new DrawOp kinds — still layout_diagram.
  {
    auto s0 = sample_diagram_animation(spec, 0.0f);
    auto s1 = sample_diagram_animation(spec, 1.0f);
    expect(s0 && s1, "endpoints");
    if (s0 && s1) {
      auto l0 = layout_diagram(s0->scene);
      auto l1 = layout_diagram(s1->scene);
      expect(l0 && l1, "layout endpoints");
      expect(l0 && l1 && !l0->ops.empty() && !l1->ops.empty(), "ops emitted");
    }
  }

  // Validation edges.
  {
    auto bad = spec;
    bad.reveal_stages.push_back(DiagramRevealStage{
        .edge_ids = {"A-B"}, .start_t = 0.8f, .end_t = 1.0f});
    expect(!sample_diagram_animation(bad, 0.5f).has_value(),
           "reject duplicate staged edge");
    bad = spec;
    bad.active_path.edge_ids = {"nope"};
    expect(!sample_diagram_animation(bad, 0.5f).has_value(),
           "reject unknown path edge");
    expect(!sample_diagram_animation(spec, std::nanf("")).has_value(),
           "reject nan t");
  }

  // Animation demo golden — multi-frame state dump through shared layout.
  {
    const float times[] = {0.0f, 0.2f, 0.4f, 0.6f, 0.75f, 0.9f, 1.0f};
    auto demo = format_diagram_animation_demo(spec, times);
    expect(demo.has_value(), "demo format");
    if (demo) {
      expect(
          check_golden(golden_dir / "animation-demo.txt", *demo, write_goldens),
          "animation-demo golden");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "diagram_animation_test ok\n";
  return EXIT_SUCCESS;
}

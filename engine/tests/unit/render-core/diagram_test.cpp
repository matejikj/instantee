#include <instantee/render_core/diagram.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

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

[[nodiscard]] DiagramScene make_kit_scene() {
  DiagramScene scene;
  scene.width_px = 240;
  scene.height_px = 140;
  scene.clear_color = Rgba8{255, 255, 255, 255};

  DiagramNode a;
  a.id = "A";
  a.cx = 50;
  a.cy = 70;
  a.width = 40;
  a.height = 28;
  a.shape = DiagramNodeShape::rounded_rect;
  a.reveal = 1.0f;

  DiagramNode b;
  b.id = "B";
  b.cx = 180;
  b.cy = 70;
  b.width = 40;
  b.height = 28;
  b.shape = DiagramNodeShape::ellipse;
  b.reveal = 1.0f;

  DiagramNode c;
  c.id = "C";
  c.cx = 115;
  c.cy = 30;
  c.width = 36;
  c.height = 24;
  c.rotation_rad = 0.0f;
  c.reveal = 1.0f;

  scene.nodes.push_back(a);
  scene.nodes.push_back(b);
  scene.nodes.push_back(c);

  DiagramEdge ab;
  ab.id = "A-B";
  ab.from_id = "A";
  ab.to_id = "B";
  ab.from_port = DiagramPort::east;
  ab.to_port = DiagramPort::west;
  ab.kind = DiagramEdgeKind::arrow;
  ab.reveal = 1.0f;

  DiagramEdge ac;
  ac.id = "A-C";
  ac.from_id = "A";
  ac.to_id = "C";
  ac.from_port = DiagramPort::north;
  ac.to_port = DiagramPort::west;
  ac.kind = DiagramEdgeKind::orthogonal;
  ac.reveal = 1.0f;

  scene.edges.push_back(ab);
  scene.edges.push_back(ac);

  DiagramGuide g;
  g.id = "baseline";
  g.horizontal = true;
  g.position = 70;
  g.origin = 10;
  g.length = 220;
  scene.guides.push_back(g);

  DiagramLabel lab;
  lab.id = "label-A";
  lab.node_id = "A";
  lab.x = 0;
  lab.y = -22;
  lab.width = 20;
  lab.height = 8;
  scene.labels.push_back(lab);

  DiagramCallout call;
  call.id = "note-B";
  call.target_id = "B";
  call.target_port = DiagramPort::south;
  call.box_cx = 180;
  call.box_cy = 115;
  call.box_w = 40;
  call.box_h = 18;
  scene.callouts.push_back(call);

  return scene;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: diagram_test <source_root> [--write-goldens]\n";
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

  // Baseline kit layout golden.
  {
    auto scene = make_kit_scene();
    auto layout = layout_diagram(scene);
    expect(layout.has_value(), "layout kit scene");
    if (layout) {
      expect(layout->edges.size() == 2, "two resolved edges");
      expect(!layout->ops.empty(), "has draw ops");
      const std::string dump =
          format_diagram_layout_golden("kit-baseline", scene, *layout);
      expect(check_golden(golden_dir / "kit-baseline.txt", dump, write_goldens),
             "kit-baseline golden");
    }
  }

  // Connectors react to node transforms without rewriting edge authoring.
  {
    auto scene = make_kit_scene();
    auto before = layout_diagram(scene);
    expect(before.has_value(), "layout before transform");

    // Translate B and rotate A; edge records still say A.east → B.west.
    for (auto &n : scene.nodes) {
      if (n.id == "B") {
        n.cx += 20.0f;
        n.cy -= 10.0f;
      }
      if (n.id == "A") {
        n.rotation_rad = static_cast<float>(3.141592653589793 / 2.0);
      }
    }
    auto after = layout_diagram(scene);
    expect(after.has_value(), "layout after transform");
    if (before && after) {
      const DiagramResolvedEdge *b_ab = nullptr;
      const DiagramResolvedEdge *a_ab = nullptr;
      for (const auto &e : before->edges) {
        if (e.id == "A-B") {
          b_ab = &e;
        }
      }
      for (const auto &e : after->edges) {
        if (e.id == "A-B") {
          a_ab = &e;
        }
      }
      expect(b_ab && a_ab, "found A-B before/after");
      if (b_ab && a_ab) {
        expect(!near(b_ab->from.x, a_ab->from.x) ||
                   !near(b_ab->from.y, a_ab->from.y),
               "from port moves with A rotation");
        expect(!near(b_ab->to.x, a_ab->to.x) || !near(b_ab->to.y, a_ab->to.y),
               "to port moves with B translate");
      }
      // Authoring edge fields unchanged.
      expect(scene.edges[0].from_id == "A" && scene.edges[0].to_id == "B" &&
                 scene.edges[0].from_port == DiagramPort::east &&
                 scene.edges[0].to_port == DiagramPort::west,
             "edge authoring unchanged");

      const std::string dump =
          format_diagram_layout_golden("kit-transformed", scene, *after);
      expect(
          check_golden(golden_dir / "kit-transformed.txt", dump, write_goldens),
          "kit-transformed golden");
    }
  }

  // Mask/reveal: node alpha scales; edge draws a prefix of the connector.
  {
    auto scene = make_kit_scene();
    scene.callouts.clear();
    scene.labels.clear();
    scene.guides.clear();
    scene.edges.resize(1);
    scene.nodes.resize(2);
    scene.nodes[0].reveal = 0.5f;
    scene.nodes[1].reveal = 0.0f; // fully masked
    scene.edges[0].reveal = 0.5f;
    scene.edges[0].kind = DiagramEdgeKind::line;

    auto layout = layout_diagram(scene);
    expect(layout.has_value(), "layout reveal scene");
    if (layout) {
      expect(layout->edges.size() == 1, "one edge resolved");
      expect(near(layout->edges[0].reveal, 0.5f), "edge reveal recorded");
      // Half reveal on A→B east→west: from (70,70) to (160,70) → end at x=115.
      bool saw_half = false;
      for (const auto &op : layout->ops) {
        if (op.kind == DrawOpKind::stroke_line && near(op.rect.x, 70.0f) &&
            near(op.rect.y, 70.0f) && near(op.x1, 115.0f) &&
            near(op.y1, 70.0f)) {
          saw_half = true;
        }
      }
      expect(saw_half, "edge stroke truncated to reveal prefix");

      // Hidden node B should not emit fill ops with its fill color alpha>0
      // at its center — no ellipse/rounded for B.
      std::size_t fills = 0;
      for (const auto &op : layout->ops) {
        if (op.kind == DrawOpKind::fill_ellipse ||
            op.kind == DrawOpKind::fill_rounded_rect) {
          ++fills;
        }
      }
      expect(fills == 1, "only revealed node emits fill");

      const std::string dump =
          format_diagram_layout_golden("kit-reveal", scene, *layout);
      expect(check_golden(golden_dir / "kit-reveal.txt", dump, write_goldens),
             "kit-reveal golden");
    }
  }

  // Validation: missing node / bad size / duplicate id.
  {
    DiagramScene bad = make_kit_scene();
    bad.edges[0].to_id = "missing";
    expect(!layout_diagram(bad).has_value(), "reject missing node");

    bad = make_kit_scene();
    bad.width_px = 0;
    expect(!layout_diagram(bad).has_value(), "reject zero size");

    bad = make_kit_scene();
    bad.nodes.push_back(bad.nodes.front());
    expect(!layout_diagram(bad).has_value(), "reject duplicate id");

    DiagramNode n;
    n.id = "X";
    n.cx = 0;
    n.cy = 0;
    n.width = 10;
    n.height = 10;
    auto east = diagram_port_point(n, DiagramPort::east);
    expect(east.has_value(), "port on valid node");
    expect(east && near(east->x, 5.0f) && near(east->y, 0.0f), "east port");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "diagram_test ok\n";
  return EXIT_SUCCESS;
}

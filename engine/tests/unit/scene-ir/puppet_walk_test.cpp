#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_skin.hpp>
#include <instantee/scene_ir/puppet_walk.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "scene-ir tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::scene_ir;
using instantee::core::nearly_equal;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
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
                                    std::string_view body) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] std::string phase_name(PuppetLocomotionPhase phase) {
  switch (phase) {
  case PuppetLocomotionPhase::idle:
    return "idle";
  case PuppetLocomotionPhase::start:
    return "start";
  case PuppetLocomotionPhase::cycle:
    return "cycle";
  case PuppetLocomotionPhase::stop:
    return "stop";
  }
  return "unknown";
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_walk_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path golden_dir =
      root / "contracts/fixtures/puppet-walk/goldens";
  const std::filesystem::path review_path =
      root / "contracts/fixtures/puppet-walk/visual-review/review-record.json";

  auto doc = make_biped_walk_rig();
  expect(validate_puppet_rig(doc).has_value(), "biped walk rig validates");
  auto bind = build_puppet_fk_skeleton(doc);
  expect(bind.has_value(), "bind skeleton");
  if (!bind) {
    return EXIT_FAILURE;
  }

  const auto retarget = make_biped_walk_retarget_map();
  PuppetWalkPlan plan;
  plan.travel_distance_px = 144.0; // three strides
  plan.timing.cycle_frames = 24.0;
  plan.timing.start_frames = 8.0;
  plan.timing.stop_frames = 8.0;
  plan.timing.stride_px = 48.0;
  plan.timing.step_height_px = 10.0;
  plan.timing.foot_slide_tol_px = 0.75;

  expect(validate_puppet_walk_plan(plan).has_value(), "plan validates");
  auto total = puppet_walk_total_frames(plan);
  expect(total.has_value(), "total frames");
  if (!total) {
    return EXIT_FAILURE;
  }
  // start 8 + 3*24 + stop 8 = 88
  expect(nearly_equal(*total, 88.0, 1e-9), "timing fixture: 88 frames");

  // Random-access reproducibility at a cruise frame.
  {
    auto a = sample_puppet_walk(plan, retarget, 40.0);
    auto b = sample_puppet_walk(plan, retarget, 40.0);
    expect(a && b, "sample cruise");
    if (a && b) {
      expect(a->phase == PuppetLocomotionPhase::cycle, "phase cycle");
      auto pa = evaluate_puppet_walk(doc, *bind, *a);
      auto pb = evaluate_puppet_walk(doc, *bind, *b);
      expect(pa && pb, "evaluate cruise");
      if (pa && pb) {
        for (std::size_t i = 0; i < pa->joints.size(); ++i) {
          expect(nearly_equal(puppet_fk_world_position(pa->joints[i]),
                              puppet_fk_world_position(pb->joints[i])),
                 "random-access pose match");
        }
      }
    }
  }

  // Start / stop phase coverage and terminal travel.
  {
    auto s0 = sample_puppet_walk(plan, retarget, 0.0);
    auto s4 = sample_puppet_walk(plan, retarget, 4.0);
    auto s20 = sample_puppet_walk(plan, retarget, 20.0);
    auto s84 = sample_puppet_walk(plan, retarget, 84.0);
    auto s100 = sample_puppet_walk(plan, retarget, 100.0);
    expect(s0 && s0->phase == PuppetLocomotionPhase::idle, "frame0 idle");
    expect(s4 && s4->phase == PuppetLocomotionPhase::start, "start phase");
    expect(s20 && s20->phase == PuppetLocomotionPhase::cycle, "cycle phase");
    expect(s84 && s84->phase == PuppetLocomotionPhase::stop, "stop phase");
    expect(s100 && s100->phase == PuppetLocomotionPhase::idle,
           "after stop idle");
    expect(s100 && nearly_equal(s100->root_travel_x, plan.travel_distance_px),
           "terminal travel distance");
  }

  // No-foot-sliding across cruise frames.
  auto slide =
      measure_puppet_walk_foot_slide(doc, *bind, plan, retarget, 8.0, 80.0);
  expect(slide.has_value(), "foot slide measure");
  if (slide) {
    expect(*slide <= plan.timing.foot_slide_tol_px, "foot slide within tol");
  }

  // Skin binding still resolves on a posed walk frame (pipeline composition).
  {
    auto sample = sample_puppet_walk(plan, retarget, 40.0);
    auto posed = evaluate_puppet_walk(doc, *bind, *sample);
    expect(posed.has_value(), "posed for skin");
    if (posed) {
      auto part = make_puppet_skin_layer_quad(doc, *bind, "leg_l", 8.0, 16.0);
      expect(part.has_value(), "skin quad");
      if (part) {
        auto mats = compute_puppet_skin_matrices(*bind, *posed);
        expect(mats.has_value(), "skin matrices");
        if (mats) {
          auto deformed = deform_puppet_skin_part(*part, *mats);
          expect(deformed.has_value(), "skin deform");
        }
      }
    }
  }

  // Missing retarget fails closed.
  {
    PuppetWalkRetargetMap incomplete{{"root", "root"}};
    auto bad = sample_puppet_walk(plan, incomplete, 10.0);
    expect(!bad.has_value(), "incomplete retarget rejected");
  }

  // Reference pose goldens at start / mid-cycle / stop.
  struct GoldenCase {
    const char *name;
    double frame;
  };
  const GoldenCase cases[] = {
      {"start-f4", 4.0},
      {"cycle-f40", 40.0},
      {"stop-f84", 84.0},
  };
  double reported_slide = slide ? *slide : 0.0;
  for (const auto &gc : cases) {
    auto sample = sample_puppet_walk(plan, retarget, gc.frame);
    expect(sample.has_value(), "golden sample");
    if (!sample) {
      continue;
    }
    auto posed = evaluate_puppet_walk(doc, *bind, *sample);
    expect(posed.has_value(), "golden pose");
    if (!posed) {
      continue;
    }
    // Per-frame slide probe around the golden (local window).
    auto local_slide = measure_puppet_walk_foot_slide(
        doc, *bind, plan, retarget, gc.frame, gc.frame + 2.0);
    const double slide_v = local_slide ? *local_slide : reported_slide;
    const std::string body =
        format_puppet_walk_golden(gc.name, *sample, *posed, slide_v);
    const auto path = golden_dir / (std::string(gc.name) + ".txt");
    expect(write_if_missing(path, body), "write golden");
    expect(read_file(path) == body, "golden matches");
    expect(!phase_name(sample->phase).empty(), "phase named");
  }

  // Signed review record must exist and declare approval method.
  expect(std::filesystem::exists(review_path), "review record present");
  if (std::filesystem::exists(review_path)) {
    const auto review = read_file(review_path);
    expect(review.find("\"status\": \"approved\"") != std::string::npos,
           "review approved");
    expect(review.find("subjective") != std::string::npos,
           "review rejects subjective-only");
    expect(review.find("foot_slide") != std::string::npos,
           "review covers foot slide");
    expect(review.find("timing_fixture") != std::string::npos,
           "review covers timing");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

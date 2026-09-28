#pragma once

namespace mif {

enum class FusionMethod { GuidedFilter, LaplacianPyramid };
enum class FocusMeasure { ModifiedLaplacian, Tenengrad };
enum class Alignment { None, Translation, Affine };

struct FusionOptions {
    FusionMethod method = FusionMethod::GuidedFilter;
    FocusMeasure focus_measure = FocusMeasure::ModifiedLaplacian;
    Alignment alignment = Alignment::None;
    int focus_window = 9;       // Odd window width for local focus energy.
    int base_radius = 15;       // Base-layer blur and base-weight guide radius.
    int detail_radius = 3;
    double base_epsilon = 0.01; // Regularization on [0, 1] grayscale values.
    double detail_epsilon = 0.0001;
    int pyramid_levels = 5;     // Automatically limited by image dimensions.
    int alignment_iterations = 150;
    double alignment_epsilon = 1e-5;
    int alignment_max_size = 1200;
    bool keep_weight_maps = false;
};

} // namespace mif


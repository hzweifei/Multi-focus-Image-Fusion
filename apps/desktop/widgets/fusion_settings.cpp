#include "fusion_settings.hpp"
#include "parameter_form.hpp"

#include <mif/fusion_options.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVariant>
#include <QVBoxLayout>
#include <stdexcept>

namespace mif::desktop {
namespace {

/// 正则项作用于 [0, 1] 灰度图的方差；下限避免官方浮点引导滤波在平坦区除零。
QDoubleSpinBox* createEpsilon(QWidget* parent, const QString& name, double default_value) {
    auto* field = new ScrollSafeWidget<QDoubleSpinBox>(parent);
    field->setObjectName(name);
    field->setDecimals(8);
    field->setRange(1e-6, 1.0);
    field->setSingleStep(default_value);
    field->setValue(default_value);
    field->setToolTip(QStringLiteral(
        "范围：0.000001–1。引导滤波的正则项，与归一化灰度图的局部方差相加。\n"
        "数值越大，通常越倾向于平滑融合权重；数值越小，越贴近引导图中的边缘。\n"
        "最小值受官方引导滤波的浮点精度限制，防止平坦区域发生除零。"));
    return field;
}

/// 半径按输入图像的原始像素计，实际窗口边长为 2 × 半径 + 1。
QSpinBox* createRadius(QWidget* parent, const QString& name, int default_value) {
    auto* field = new ScrollSafeWidget<QSpinBox>(parent);
    field->setObjectName(name);
    field->setRange(1, 255);
    field->setSuffix(QStringLiteral(" px"));
    field->setValue(default_value);
    return field;
}

/// 各类局部统计窗口均要求奇数；提交偶数时向上调整，逐字输入期间保留用户文本。
QSpinBox* createOddWindow(QWidget* parent, const QString& name, int maximum, int default_value) {
    auto* field = new ScrollSafeWidget<QSpinBox>(parent);
    field->setObjectName(name);
    field->setRange(1, maximum);
    field->setSingleStep(2);
    field->setKeyboardTracking(false);
    field->setValue(default_value);
    QObject::connect(field, qOverload<int>(&QSpinBox::valueChanged), field,
                     [field](int value) { if (value % 2 == 0) field->setValue(value + 1); });
    return field;
}

/// 比例和归一化差异阈值均允许端点 0 与 1，六位小数便于微调较小阈值。
QDoubleSpinBox* createRatio(QWidget* parent, const QString& name, double default_value, double step) {
    auto* field = new ScrollSafeWidget<QDoubleSpinBox>(parent);
    field->setObjectName(name);
    field->setDecimals(6);
    field->setRange(0.0, 1.0);
    field->setSingleStep(step);
    field->setValue(default_value);
    return field;
}

} // 匿名命名空间

FusionSettings::FusionSettings(QWidget* parent) : QGroupBox(parent) {
    const GuidedFilterFusionOptions guided_defaults;
    const LaplacianPyramidFusionOptions pyramid_defaults;
    const BlockVarianceFusionOptions block_defaults;
    const DtcwtFusionOptions dtcwt_defaults;
    const GfgFgfFusionOptions gfg_defaults;
    setObjectName("fusionSettings");
    setTitle(QString());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 14, 12, 14);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignTop);

    auto* method_label = new QLabel(QStringLiteral("融合方法"), this);
    method_ = new ScrollSafeWidget<QComboBox>(this);
    method_->setObjectName("fusionMethod");
    method_->addItem(QStringLiteral("双尺度引导滤波"), static_cast<int>(FusionMethod::GuidedFilter));
    method_->addItem(QStringLiteral("拉普拉斯金字塔"), static_cast<int>(FusionMethod::LaplacianPyramid));
    method_->addItem(QStringLiteral("块方差"), static_cast<int>(FusionMethod::BlockVariance));
    method_->addItem(QStringLiteral("双树复小波（DTCWT）"), static_cast<int>(FusionMethod::Dtcwt));
    method_->addItem(QStringLiteral("GFG-FGF 梯度融合"), static_cast<int>(FusionMethod::GfgFgf));
    method_->setCurrentIndex(method_->findData(static_cast<int>(FusionMethod::GuidedFilter)));
    method_->setToolTip(QStringLiteral("将各张图片中的清晰区域合成一张图像；各方法的参数分别保存。"));
    method_label->setBuddy(method_);
    method_->setAccessibleName(method_label->text());
    auto* method_layout = new QVBoxLayout;
    method_layout->setSpacing(6);
    method_layout->addWidget(method_label);
    method_layout->addWidget(method_);
    layout->addLayout(method_layout);
    method_hint_ = new QLabel(this);
    method_hint_->setObjectName("parameterHint");
    method_hint_->setWordWrap(true);
    layout->addWidget(method_hint_);

    const int label_width = fontMetrics().horizontalAdvance(QStringLiteral("细节正则项"));
    // 重用构造步骤，但每次都创建独立控件；清晰度配置也不会在两种方法之间串用。
    auto createMethodForm = [&](MethodFields& fields, const QString& prefix, const QString& panel_name,
                                const FocusMeasureOptions& focus, int radius, double epsilon) {
        fields.panel = new QWidget(this);
        fields.panel->setObjectName(panel_name);
        auto* form = createParameterForm(fields.panel);
        fields.focus = new ScrollSafeWidget<QComboBox>(fields.panel);
        fields.focus->setObjectName(prefix + "FocusMeasure");
        fields.focus->addItem(QStringLiteral("改进拉普拉斯"), static_cast<int>(FocusMeasure::ModifiedLaplacian));
        fields.focus->addItem(QStringLiteral("Tenengrad 梯度"), static_cast<int>(FocusMeasure::Tenengrad));
        fields.focus->setCurrentIndex(fields.focus->findData(static_cast<int>(focus.measure)));
        fields.focus->setToolTip(QStringLiteral(
            "比较各张图片局部的清晰程度。改进拉普拉斯使用二阶差分，Tenengrad 使用梯度能量。\n"
            "该指标只属于当前融合方法，另一种方法可以使用不同设置。"));
        addParameter(form, QStringLiteral("清晰度指标"), fields.focus, label_width);
        fields.window_size = createOddWindow(fields.panel, prefix + "FocusWindow", 255, focus.window_size);
        fields.window_size->setSuffix(QStringLiteral(" px"));
        fields.window_size->setToolTip(QStringLiteral(
            "范围：1–255 px 的奇数。清晰度响应的局部平均窗口边长。\n"
            "增大可抑制噪声，但可能损失细小结构；输入偶数时会调整为下一个奇数。"));
        addParameter(form, QStringLiteral("统计窗口"), fields.window_size, label_width);
        fields.detail_radius = createRadius(fields.panel, prefix + "DetailRadius", radius);
        fields.detail_radius->setToolTip(QStringLiteral(
            "范围：1–255 px。细节权重图的引导滤波半径，实际窗口边长为 2 × 半径 + 1。\n"
            "增大可平滑权重过渡，但可能使细小边界变宽。"));
        addParameter(form, QStringLiteral("细节半径"), fields.detail_radius, label_width);
        fields.detail_epsilon = createEpsilon(fields.panel, prefix + "DetailEpsilon", epsilon);
        addParameter(form, QStringLiteral("细节正则项"), fields.detail_epsilon, label_width);
        layout->addWidget(fields.panel);
        return form;
    };

    auto* guided_form = createMethodForm(guided_, QStringLiteral("guided"), QStringLiteral("guidedFilterFields"),
                                         guided_defaults.focus, guided_defaults.detail_radius, guided_defaults.detail_epsilon);
    base_radius_ = createRadius(guided_.panel, QStringLiteral("guidedBaseRadius"), guided_defaults.base_radius);
    base_radius_->setToolTip(QStringLiteral(
        "范围：1–255 px。基础层均值滤波和基础权重引导滤波共用的半径。\n"
        "实际窗口边长为 2 × 半径 + 1；仅双尺度引导滤波使用。"));
    // 基础层参数位于清晰度之后、细节层之前，表单顺序与分解和重建的阅读顺序一致。
    addParameter(guided_form, QStringLiteral("基础半径"), base_radius_, label_width, 2);
    base_epsilon_ = createEpsilon(guided_.panel, QStringLiteral("guidedBaseEpsilon"), guided_defaults.base_epsilon);
    addParameter(guided_form, QStringLiteral("基础正则项"), base_epsilon_, label_width, 3);

    auto* pyramid_form = createMethodForm(pyramid_, QStringLiteral("pyramid"), QStringLiteral("laplacianPyramidFields"),
                                          pyramid_defaults.focus, pyramid_defaults.detail_radius, pyramid_defaults.detail_epsilon);
    max_levels_ = new ScrollSafeWidget<QSpinBox>(pyramid_.panel);
    max_levels_->setObjectName("pyramidLevels");
    max_levels_->setRange(1, 16);
    max_levels_->setValue(pyramid_defaults.max_levels);
    max_levels_->setToolTip(QStringLiteral(
        "范围：1–16，包含最粗层。实际层数还受图片尺寸限制。\n"
        "设为 1 时直接进行单尺度加权；更高层数同时处理更多尺度的细节。"));
    addParameter(pyramid_form, QStringLiteral("金字塔层数"), max_levels_, label_width);

    auto createPanel = [&](QWidget*& panel, const QString& name) {
        panel = new QWidget(this);
        panel->setObjectName(name);
        layout->addWidget(panel);
        return createParameterForm(panel);
    };
    auto* block_variance_form = createPanel(block_variance_.panel, QStringLiteral("dctFields"));
    block_variance_.block_size = new ScrollSafeWidget<QSpinBox>(block_variance_.panel);
    block_variance_.block_size->setObjectName("dctBlockSize");
    block_variance_.block_size->setRange(2, 128);
    block_variance_.block_size->setSuffix(QStringLiteral(" px"));
    block_variance_.block_size->setValue(block_defaults.block_size);
    block_variance_.block_size->setToolTip(QStringLiteral(
        "范围：2–128 px。以不重叠方块的像素方差比较清晰度，每个方块选择一个来源。\n"
        "较小方块可区分细小区域，较大方块的统计更稳定。"));
    addParameter(block_variance_form, QStringLiteral("方块边长"), block_variance_.block_size, label_width);
    block_variance_.consistency_window_size = createOddWindow(block_variance_.panel, QStringLiteral("dctConsistencyWindow"),
                                             31, block_defaults.consistency_window_size);
    block_variance_.consistency_window_size->setSuffix(QStringLiteral(" 块"));
    block_variance_.consistency_window_size->setToolTip(QStringLiteral(
        "范围：1–31 块的奇数。对块级来源决策连续进行两次中值滤波，减少孤立选择。\n"
        "1 表示不做平滑；偶数会调整为下一个奇数。"));
    addParameter(block_variance_form, QStringLiteral("一致性窗口"), block_variance_.consistency_window_size, label_width);

    auto* dtcwt_form = createPanel(dtcwt_.panel, QStringLiteral("dtcwtFields"));
    dtcwt_.max_levels = new ScrollSafeWidget<QSpinBox>(dtcwt_.panel);
    dtcwt_.max_levels->setObjectName("dtcwtLevels");
    dtcwt_.max_levels->setRange(1, 16);
    dtcwt_.max_levels->setValue(dtcwt_defaults.max_levels);
    dtcwt_.max_levels->setToolTip(QStringLiteral(
        "范围：1–16。双树复小波的分解层数，实际可用层数受图像尺寸限制。\n"
        "增加层数可同时比较更大尺度的结构。"));
    addParameter(dtcwt_form, QStringLiteral("分解层数"), dtcwt_.max_levels, label_width);
    dtcwt_.activity_window_size = createOddWindow(dtcwt_.panel, QStringLiteral("dtcwtActivityWindow"),
                                            31, dtcwt_defaults.activity_window_size);
    dtcwt_.activity_window_size->setToolTip(QStringLiteral(
        "范围：1–31 的奇数。小波系数活动度的局部统计窗口，作用于各层系数。\n"
        "较大的窗口可稳定系数选择；偶数会调整为下一个奇数。"));
    addParameter(dtcwt_form, QStringLiteral("活动度窗口"), dtcwt_.activity_window_size, label_width);

    auto* gfg_fgf_form = createPanel(gfg_fgf_.panel, QStringLiteral("gfgfgfFields"));
    gfg_fgf_.local_mean_window_size = createOddWindow(gfg_fgf_.panel, QStringLiteral("gfgfgfDifferenceWindow"),
                                               255, gfg_defaults.local_mean_window_size);
    gfg_fgf_.local_mean_window_size->setSuffix(QStringLiteral(" px"));
    gfg_fgf_.local_mean_window_size->setToolTip(QStringLiteral(
        "范围：1–255 px 的奇数。计算图像与其局部均值之差时使用的平均窗口。\n"
        "偶数会调整为下一个奇数。"));
    addParameter(gfg_fgf_form, QStringLiteral("差异窗口"), gfg_fgf_.local_mean_window_size, label_width);
    gfg_fgf_.selection_ratio = createRatio(gfg_fgf_.panel, QStringLiteral("gfgfgfSelectionRatio"),
                                         gfg_defaults.selection_ratio, 0.01);
    gfg_fgf_.selection_ratio->setToolTip(QStringLiteral(
        "范围：0–1。可选的全局 Scharr 梯度筛帧比例，0 保留全部输入。\n"
        "数值越大，筛选越严格，可能排除只含少量清晰区域的图片。"));
    addParameter(gfg_fgf_form, QStringLiteral("可选筛帧比"), gfg_fgf_.selection_ratio, label_width);
    gfg_fgf_.gfg_threshold = createRatio(gfg_fgf_.panel, QStringLiteral("gfgfgfDifferenceThreshold"),
                                              gfg_defaults.gfg_threshold, 0.001);
    gfg_fgf_.gfg_threshold->setToolTip(QStringLiteral(
        "范围：0–1。原始梯度达到此阈值的图片优先参与当前位置的清晰度比较。\n"
        "只有全部图片都未达到阈值，才比较均值残差；两路评分分别滤波后比较。"));
    addParameter(gfg_fgf_form, QStringLiteral("梯度阈值"), gfg_fgf_.gfg_threshold, label_width);
    gfg_fgf_.guided_radius = createRadius(gfg_fgf_.panel, QStringLiteral("gfgfgfGuidedRadius"),
                                        gfg_defaults.guided_radius);
    gfg_fgf_.guided_radius->setToolTip(QStringLiteral(
        "范围：1–255 px。两阶段快速引导滤波共用半径，按原图像素计。\n"
        "先分别优化梯度与均值残差，再优化融合权重。"));
    addParameter(gfg_fgf_form, QStringLiteral("引导半径"), gfg_fgf_.guided_radius, label_width);
    gfg_fgf_.guided_epsilon = createEpsilon(gfg_fgf_.panel, QStringLiteral("gfgfgfGuidedEpsilon"),
                                          gfg_defaults.guided_epsilon);
    addParameter(gfg_fgf_form, QStringLiteral("引导正则项"), gfg_fgf_.guided_epsilon, label_width);
    gfg_fgf_.guided_subsample_factor = new ScrollSafeWidget<QSpinBox>(gfg_fgf_.panel);
    gfg_fgf_.guided_subsample_factor->setObjectName("gfgfgfGuidedSubsample");
    gfg_fgf_.guided_subsample_factor->setRange(1, 16);
    gfg_fgf_.guided_subsample_factor->setValue(gfg_defaults.guided_subsample_factor);
    gfg_fgf_.guided_subsample_factor->setToolTip(QStringLiteral(
        "范围：1–16。快速引导滤波计算局部系数时的下采样倍数。\n"
        "1 使用完整分辨率；较大值减少计算量，可能影响细小边界。"));
    addParameter(gfg_fgf_form, QStringLiteral("下采样倍数"), gfg_fgf_.guided_subsample_factor, label_width);

    auto* reset = new QPushButton(QStringLiteral("恢复当前方法默认值"), this);
    reset->setObjectName("resetFusionOptions");
    reset->setToolTip(QStringLiteral("只恢复当前方法的参数，保留方法选择及其他方法的配置。"));
    layout->addWidget(reset);
    connect(method_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateMethodVisibility(); });
    connect(reset, &QPushButton::clicked, this, [this] { resetCurrentMethod(); });
    updateMethodVisibility();
}

std::unique_ptr<FusionOptionsBase> FusionSettings::options() const {
    auto readFocus = [](const MethodFields& fields) {
        FocusMeasureOptions focus;
        focus.measure = static_cast<FocusMeasure>(fields.focus->currentData().toInt());
        focus.window_size = fields.window_size->value();
        return focus;
    };
    // 只创建当前方法的配置；全部类型默认不保留权重图，桌面只展示融合图片。
    switch (static_cast<FusionMethod>(method_->currentData().toInt())) {
    case FusionMethod::GuidedFilter: {
        auto result = std::make_unique<GuidedFilterFusionOptions>();
        result->focus = readFocus(guided_);
        result->base_radius = base_radius_->value();
        result->base_epsilon = base_epsilon_->value();
        result->detail_radius = guided_.detail_radius->value();
        result->detail_epsilon = guided_.detail_epsilon->value();
        return result;
    }
    case FusionMethod::LaplacianPyramid: {
        auto result = std::make_unique<LaplacianPyramidFusionOptions>();
        result->focus = readFocus(pyramid_);
        result->detail_radius = pyramid_.detail_radius->value();
        result->detail_epsilon = pyramid_.detail_epsilon->value();
        result->max_levels = max_levels_->value();
        return result;
    }
    case FusionMethod::BlockVariance: {
        auto result = std::make_unique<BlockVarianceFusionOptions>();
        result->block_size = block_variance_.block_size->value();
        result->consistency_window_size = block_variance_.consistency_window_size->value();
        return result;
    }
    case FusionMethod::Dtcwt: {
        auto result = std::make_unique<DtcwtFusionOptions>();
        result->max_levels = dtcwt_.max_levels->value();
        result->activity_window_size = dtcwt_.activity_window_size->value();
        return result;
    }
    case FusionMethod::GfgFgf: {
        auto result = std::make_unique<GfgFgfFusionOptions>();
        result->local_mean_window_size = gfg_fgf_.local_mean_window_size->value();
        result->selection_ratio = gfg_fgf_.selection_ratio->value();
        result->gfg_threshold = gfg_fgf_.gfg_threshold->value();
        result->guided_radius = gfg_fgf_.guided_radius->value();
        result->guided_epsilon = gfg_fgf_.guided_epsilon->value();
        result->guided_subsample_factor = gfg_fgf_.guided_subsample_factor->value();
        return result;
    }
    }
    throw std::logic_error("Unknown desktop fusion selection");
}

void FusionSettings::updateMethodVisibility() {
    const auto method = static_cast<FusionMethod>(method_->currentData().toInt());
    guided_.panel->setVisible(method == FusionMethod::GuidedFilter);
    pyramid_.panel->setVisible(method == FusionMethod::LaplacianPyramid);
    block_variance_.panel->setVisible(method == FusionMethod::BlockVariance);
    dtcwt_.panel->setVisible(method == FusionMethod::Dtcwt);
    gfg_fgf_.panel->setVisible(method == FusionMethod::GfgFgf);
    switch (method) {
    case FusionMethod::GuidedFilter:
        method_hint_->setText(QStringLiteral("使用 OpenCV 官方引导滤波，分别优化基础层和细节层权重后重建。"));
        break;
    case FusionMethod::LaplacianPyramid:
        method_hint_->setText(QStringLiteral("对不同尺度的细节分别加权，再重建清晰图像。"));
        break;
    case FusionMethod::BlockVariance:
        method_hint_->setText(QStringLiteral("按方块的像素方差选取清晰来源，再平滑块级决策。"));
        break;
    case FusionMethod::Dtcwt:
        method_hint_->setText(QStringLiteral("用双树复小波比较各尺度、各方向的细节，再重建图像。"));
        break;
    case FusionMethod::GfgFgf:
        method_hint_->setText(QStringLiteral("优先比较梯度达标的图片，全部未达标时比较均值残差，再平滑权重融合；筛帧默认关闭。"));
        break;
    }
}

void FusionSettings::resetCurrentMethod() {
    const GuidedFilterFusionOptions guided_defaults;
    const LaplacianPyramidFusionOptions pyramid_defaults;
    const BlockVarianceFusionOptions block_defaults;
    const DtcwtFusionOptions dtcwt_defaults;
    const GfgFgfFusionOptions gfg_defaults;
    auto resetCommon = [](MethodFields& fields, const FocusMeasureOptions& focus, int radius, double epsilon) {
        fields.focus->setCurrentIndex(fields.focus->findData(static_cast<int>(focus.measure)));
        fields.window_size->setValue(focus.window_size);
        fields.detail_radius->setValue(radius);
        fields.detail_epsilon->setValue(epsilon);
    };
    switch (static_cast<FusionMethod>(method_->currentData().toInt())) {
    case FusionMethod::GuidedFilter: {
        const auto& options = guided_defaults;
        resetCommon(guided_, options.focus, options.detail_radius, options.detail_epsilon);
        base_radius_->setValue(options.base_radius);
        base_epsilon_->setValue(options.base_epsilon);
        break;
    }
    case FusionMethod::LaplacianPyramid: {
        const auto& options = pyramid_defaults;
        resetCommon(pyramid_, options.focus, options.detail_radius, options.detail_epsilon);
        max_levels_->setValue(options.max_levels);
        break;
    }
    case FusionMethod::BlockVariance:
        block_variance_.block_size->setValue(block_defaults.block_size);
        block_variance_.consistency_window_size->setValue(block_defaults.consistency_window_size);
        break;
    case FusionMethod::Dtcwt:
        dtcwt_.max_levels->setValue(dtcwt_defaults.max_levels);
        dtcwt_.activity_window_size->setValue(dtcwt_defaults.activity_window_size);
        break;
    case FusionMethod::GfgFgf:
        gfg_fgf_.local_mean_window_size->setValue(gfg_defaults.local_mean_window_size);
        gfg_fgf_.selection_ratio->setValue(gfg_defaults.selection_ratio);
        gfg_fgf_.gfg_threshold->setValue(gfg_defaults.gfg_threshold);
        gfg_fgf_.guided_radius->setValue(gfg_defaults.guided_radius);
        gfg_fgf_.guided_epsilon->setValue(gfg_defaults.guided_epsilon);
        gfg_fgf_.guided_subsample_factor->setValue(gfg_defaults.guided_subsample_factor);
        break;
    }
}

} // 命名空间 mif::desktop
